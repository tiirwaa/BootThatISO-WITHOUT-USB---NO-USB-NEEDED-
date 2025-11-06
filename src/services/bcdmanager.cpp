#include "bcdmanager.h"
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"
#include "../models/LinuxBootStrategy.h"
#include "BCDVolumeManager.h"
#include "EFIManager.h"
#include "BCDEntryManager.h"
#include "BCDLogger.h"
#include <windows.h>
#include <winnt.h>
#include <string>
#include <ctime>
#include <vector>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <optional>

namespace {
namespace fs = std::filesystem;

constexpr const char *BCD_CMD_PATH        = nullptr; // Deprecated, use Utils::getBcdeditPath()
constexpr const char *BOOTMGR_BACKUP_FILE = "bootmgr_backup.ini";

std::string trimString(const std::string &input) {
    const auto start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return std::string();
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

struct BootmgrState {
    std::string                defaultId;
    std::optional<std::string> timeout;
};

std::optional<BootmgrState> queryBootmgrState() {
    std::string        output = Utils::exec((Utils::getBcdeditPath() + " /enum {bootmgr}").c_str());
    BootmgrState       state;
    bool               foundDefault = false;
    bool               foundTimeout = false;
    std::istringstream stream(output);
    std::string        line;
    while (std::getline(stream, line)) {
        std::string trimmed = trimString(line);
        if (trimmed.empty()) {
            continue;
        }
        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (!foundDefault && lower.rfind("default", 0) == 0) {
            const auto pos = trimmed.find_first_of(" \t");
            if (pos != std::string::npos) {
                std::string value = trimString(trimmed.substr(pos));
                if (!value.empty()) {
                    state.defaultId = value;
                    foundDefault    = true;
                }
            }
        } else if (!foundTimeout && lower.rfind("timeout", 0) == 0) {
            const auto pos = trimmed.find_first_of(" \t");
            if (pos != std::string::npos) {
                std::string value = trimString(trimmed.substr(pos));
                if (!value.empty()) {
                    state.timeout = value;
                }
                foundTimeout = true;
            }
        }
        if (foundDefault && foundTimeout) {
            break;
        }
    }

    if (!foundDefault && !foundTimeout) {
        return std::nullopt;
    }
    return state;
}

std::string bootmgrBackupPath() {
    std::string dir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(dir.c_str(), NULL);
    return dir + "\\" + BOOTMGR_BACKUP_FILE;
}

void captureBootmgrStateIfNeeded() {
    std::string path = bootmgrBackupPath();
    if (fs::exists(path)) {
        return;
    }
    auto stateOpt = queryBootmgrState();
    if (!stateOpt.has_value()) {
        return;
    }
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        return;
    }
    if (!stateOpt->defaultId.empty()) {
        file << "default=" << stateOpt->defaultId << "\n";
    }
    if (stateOpt->timeout.has_value()) {
        file << "timeout=" << stateOpt->timeout.value() << "\n";
    }
}

std::optional<BootmgrState> loadBootmgrBackup() {
    std::string   path = bootmgrBackupPath();
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }
    BootmgrState state;
    std::string  line;
    while (std::getline(file, line)) {
        std::string trimmed = trimString(line);
        if (trimmed.empty()) {
            continue;
        }
        auto pos = trimmed.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key   = trimString(trimmed.substr(0, pos));
        std::string value = trimString(trimmed.substr(pos + 1));
        if (key == "default") {
            state.defaultId = value;
        } else if (key == "timeout") {
            if (!value.empty()) {
                state.timeout = value;
            }
        }
    }

    if (state.defaultId.empty() && !state.timeout.has_value()) {
        return std::nullopt;
    }
    return state;
}

void deleteBootmgrBackup() {
    std::string     path = bootmgrBackupPath();
    std::error_code ec;
    fs::remove(path, ec);
}

bool restoreBootmgrStateIfPresent(EventManager *eventManager) {
    std::optional<BootmgrState> stateOpt = loadBootmgrBackup();
    if (!stateOpt.has_value()) {
        return false;
    }
    if (eventManager) {
        eventManager->notifyLogUpdate(
            "Limpiando configuracion temporal del gestor de arranque guardada por BootThatISO...\r\n");
    }
    deleteBootmgrBackup();
    return true;
}

// Builder pattern for BCD configuration
class BCDConfigurator {
public:
    BCDConfigurator(EventManager *eventManager, const std::string &bcdCmdPath)
        : eventManager_(eventManager), bcdCmdPath_(bcdCmdPath), volumeManager_(), efiManager_(),
          entryManager_(bcdCmdPath) {}

    BCDConfigurator &setDriveLetters(const std::string &dataDrive, const std::string &espDrive) {
        dataDriveLetter_ = dataDrive;
        espDriveLetter_  = espDrive;
        return *this;
    }

    BCDConfigurator &setStrategy(BootStrategy *strategy) {
        strategy_ = strategy;
        return *this;
    }

    std::string build() {
        // Step 1: Capture bootmgr state
        captureBootmgrStateIfNeeded();

        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.bcd.configuring", "Configurando Boot Configuration Data (BCD)...") + "\r\n");

        // Step 2: Get volume GUIDs
        auto dataDeviceOpt = volumeManager_.getVolumeGUID(dataDriveLetter_);
        if (!dataDeviceOpt) {
            return "Error al obtener el nombre del volumen de datos";
        }
        dataDevice_ = *dataDeviceOpt;

        auto espDeviceOpt = volumeManager_.getVolumeGUID(espDriveLetter_);
        if (!espDeviceOpt) {
            return "Error al obtener el nombre del volumen ESP";
        }
        espDevice_ = *espDeviceOpt;

        // Step 3: Preserve Windows entries
        preserveWindowsEntries();

        // Step 4: Clean up existing entries
        cleanupExistingEntries();

        // Step 5: Create new entry
        auto guidOpt = entryManager_.createEntry(strategy_->getBCDLabel(), strategy_->getType());
        if (!guidOpt) {
            return "Error al crear/copiar entrada BCD";
        }
        guid_ = *guidOpt;

        // Step 5.5: Setup strategy (e.g., copy GRUB for Linux)
        if (strategy_->getType() == "linux") {
            strategy_->setup(espDriveLetter_);
        }

        // Step 6: Select EFI file
        auto efiFileOpt = efiManager_.selectEFIBootFile(espDriveLetter_, strategy_->getType(), eventManager_);
        if (!efiFileOpt) {
            return "Archivo EFI boot no encontrado en ESP";
        }
        efiBootFile_ = *efiFileOpt; // Step 6: Validate architecture
        if (!validateArchitecture()) {
            return "Arquitectura EFI no compatible con el sistema";
        }

        // Step 7: Configure strategy
        strategy_->configureBCD(guid_, dataDriveLetter_, espDriveLetter_, getEfiPath());

        // Step 8: Finalize BCD
        return finalizeBCD();
    }

private:
    EventManager *eventManager_;
    std::string   bcdCmdPath_;
    std::string   dataDriveLetter_;
    std::string   espDriveLetter_;
    BootStrategy *strategy_;
    std::string   dataDevice_;
    std::string   espDevice_;
    std::string   guid_;
    std::string   efiBootFile_;

    BCDVolumeManager volumeManager_;
    EFIManager       efiManager_;
    BCDEntryManager  entryManager_;

    void preserveWindowsEntries() {
        Utils::exec((bcdCmdPath_ + " /default {current}").c_str());
        std::string preserveResult = Utils::exec((bcdCmdPath_ + " /copy {default} /d \"Windows (System)\"").c_str());
        if (preserveResult.find("{") != std::string::npos && preserveResult.find("}") != std::string::npos) {
            size_t pos = preserveResult.find("{");
            size_t end = preserveResult.find("}", pos);
            if (end != std::string::npos) {
                std::string windowsGuid = preserveResult.substr(pos, end - pos + 1);
                entryManager_.addToDisplayOrder(windowsGuid, false);
            }
        }
    }

    void cleanupExistingEntries() {
        entryManager_.deleteEntriesByLabel("ISOBOOT");
    }

    std::string getEfiPath() {
        return efiBootFile_.substr(espDriveLetter_.length());
    }

    bool validateArchitecture() {
        WORD machine = efiManager_.getMachineType(efiBootFile_);
        if (machine != IMAGE_FILE_MACHINE_AMD64 && machine != IMAGE_FILE_MACHINE_I386) {
            if (eventManager_)
                eventManager_->notifyLogUpdate(
                    LocalizedOrUtf8("log.bcd.unsupportedArchitecture", "Error: Arquitectura EFI no soportada.") +
                    "\r\n");
            return false;
        }

        SYSTEM_INFO sysInfo;
        GetNativeSystemInfo(&sysInfo);
        WORD sysArch = (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)   ? IMAGE_FILE_MACHINE_AMD64
                       : (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) ? IMAGE_FILE_MACHINE_I386
                                                                                          : 0;
        if (machine != sysArch) {
            std::string errorMsg = "Error: Arquitectura EFI (0x" + std::to_string(machine) +
                                   ") no coincide con la del sistema (0x" + std::to_string(sysArch) + ").\r\n";
            if (eventManager_)
                eventManager_->notifyLogUpdate(errorMsg);
            return false;
        }
        return true;
    }

    std::string finalizeBCD() {
        // Remove systemroot
        if (strategy_->getType() != "ramdisk") {
            entryManager_.deleteValue(guid_, "systemroot");
        }

        // Set as default
        if (!entryManager_.setDefault(guid_)) {
            return "Error al configurar default";
        }

        // Add to display order
        if (!entryManager_.addToDisplayOrder(guid_, true)) {
            return "Error al configurar displayorder";
        }

        // Set timeout
        if (!entryManager_.setTimeout(30)) {
            return "Error al configurar timeout";
        }

        return ""; // Success
    }
};

} // namespace

BCDManager &BCDManager::getInstance() {
    static BCDManager instance;
    return instance;
}

BCDManager::BCDManager() : eventManager(nullptr) {}

BCDManager::~BCDManager() {}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream        ss(s);
    std::string              item;
    while (getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

std::string BCDManager::configureBCD(const std::string &driveLetter, const std::string &espDriveLetter,
                                     BootStrategy &strategy) {
    BCDConfigurator configurator(eventManager, Utils::getBcdeditPath());
    return configurator.setDriveLetters(driveLetter, espDriveLetter).setStrategy(&strategy).build();
}

bool BCDManager::restoreBCD() {
    const std::string BCD_CMD = Utils::getBcdeditPath();

    // First, try to restore the BCD file from backup if it exists
    std::string bcdPath = "C:\\Boot\\BCD"; // Default system BCD path
    // But since we exported to ESP, check if ESP has backup
    // For now, assume we need to find the ESP drive
    // This is a simplification; in practice, we might need to detect ESP again
    std::string espDrive   = "Z:"; // Placeholder, should be detected
    std::string exportPath = espDrive + "\\EFI\\Microsoft\\Boot\\BCD";
    std::string backupPath = exportPath + ".backup";

    if (GetFileAttributesA(backupPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (CopyFileA(backupPath.c_str(), exportPath.c_str(), FALSE)) {
            if (eventManager) {
                eventManager->notifyLogUpdate("Restaurado BCD desde backup.\r\n");
            }
            // Delete the backup after successful restore
            DeleteFileA(backupPath.c_str());
        } else {
            if (eventManager) {
                eventManager->notifyLogUpdate("Error al restaurar BCD desde backup.\r\n");
            }
        }
    }

    std::string output = Utils::exec((BCD_CMD + " /enum all").c_str());
    auto        blocks = split(output, '\n');
    // Parse into blocks separated by empty lines
    std::vector<std::string> entryBlocks;
    std::string              currentBlock;
    for (const auto &line : blocks) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            if (!currentBlock.empty()) {
                entryBlocks.push_back(currentBlock);
                currentBlock.clear();
            }
        } else {
            currentBlock += line + "\n";
        }
    }
    if (!currentBlock.empty())
        entryBlocks.push_back(currentBlock);

    auto icontains = [](const std::string &hay, const std::string &needle) {
        std::string h = hay;
        std::string n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return h.find(n) != std::string::npos;
    };

    std::string labelToFind = "ISOBOOT";
    bool        deletedAny  = false;
    for (const auto &blk : entryBlocks) {
        if (icontains(blk, labelToFind)) {
            // find GUID in block
            size_t pos = blk.find('{');
            if (pos != std::string::npos) {
                size_t end = blk.find('}', pos);
                if (end != std::string::npos) {
                    std::string guid = blk.substr(pos, end - pos + 1);

                    // Don't delete {current} or {bootmgr}
                    // For {default}, only protect it if it doesn't contain ISOBOOT (but since we're here because it
                    // contains ISOBOOT, we can delete it)
                    bool isProtected = (guid == "{current}" || guid == "{bootmgr}");
                    if (guid == "{default}") {
                        // Since we already checked icontains(blk, "isoboot") above, {default} with ISOBOOT should be
                        // deleted
                        isProtected = false;
                    }

                    if (isProtected) {
                        continue;
                    }

                    std::string deleteCmd = BCD_CMD + " /delete " + guid + " /f"; // force remove
                    Utils::exec(deleteCmd.c_str());
                    deletedAny = true;
                }
            }
        }
    }

    bool bootmgrStateRestored = restoreBootmgrStateIfPresent(eventManager);
    bool shouldResetDefaults  = deletedAny || bootmgrStateRestored;

    if (shouldResetDefaults) {
        if (eventManager) {
            eventManager->notifyLogUpdate(LocalizedOrUtf8("log.bcd.settingWindowsDefault",
                                                          "Estableciendo Windows como entrada predeterminada...") +
                                          "\r\n");
        }

        // Try to find a preserved Windows entry first, then fall back to {current}
        std::string windowsEntryGuid = "{current}";
        for (const auto &blk : entryBlocks) {
            if (icontains(blk, "windows (system)") || icontains(blk, "windows 10")) {
                size_t pos = blk.find('{');
                if (pos != std::string::npos) {
                    size_t end = blk.find('}', pos);
                    if (end != std::string::npos) {
                        windowsEntryGuid = blk.substr(pos, end - pos + 1);
                        break;
                    }
                }
            }
        }

        std::string defaultResult = Utils::exec((BCD_CMD + " /default " + windowsEntryGuid).c_str());
        std::string timeoutResult = Utils::exec((BCD_CMD + " /timeout 0").c_str());

        if (eventManager) {
            if (!defaultResult.empty()) {
                eventManager->notifyLogUpdate(LocalizedFormatUtf8(
                    "log.bcd.resultDefault", {Utils::utf8_to_wstring(Utils::ansi_to_utf8(defaultResult))},
                    "Resultado /default: {0}\r\n"));
            }
            if (!timeoutResult.empty()) {
                eventManager->notifyLogUpdate(LocalizedFormatUtf8(
                    "log.bcd.resultTimeout", {Utils::utf8_to_wstring(Utils::ansi_to_utf8(timeoutResult))},
                    "Resultado /timeout: {0}\r\n"));
            }
        }
    }

    // If we deleted entries, re-export the cleaned BCD to ESP
    if (deletedAny) {
        // Detect ESP drive by finding FAT32 partition with bootmgfw.efi
        std::string espDrive;
        for (char drive = 'A'; drive <= 'Z'; ++drive) {
            std::string driveStr     = std::string(1, drive) + ":\\";
            std::string bootmgfwPath = driveStr + "EFI\\Microsoft\\Boot\\bootmgfw.efi";
            DWORD       attrs        = GetFileAttributesA(bootmgfwPath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) {
                // Check if FAT32 and size 100-1024MB
                std::string volumePath = "\\\\.\\" + std::string(1, drive) + ":";
                HANDLE hVolume = CreateFileA(volumePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                             OPEN_EXISTING, 0, NULL);
                if (hVolume != INVALID_HANDLE_VALUE) {
                    PARTITION_INFORMATION_EX partInfo;
                    DWORD                    bytesReturned = 0;
                    if (DeviceIoControl(hVolume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partInfo, sizeof(partInfo),
                                        &bytesReturned, NULL)) {
                        ULONGLONG sizeBytes = partInfo.PartitionLength.QuadPart;
                        int       sizeMB    = static_cast<int>(sizeBytes / (1024 * 1024));
                        char      fsName[256];
                        if (GetVolumeInformationA(driveStr.c_str(), NULL, 0, NULL, NULL, NULL, fsName,
                                                  sizeof(fsName))) {
                            if (sizeMB >= 100 && sizeMB <= 1024 && strcmp(fsName, "FAT32") == 0) {
                                espDrive = driveStr;
                                CloseHandle(hVolume);
                                break;
                            }
                        }
                    }
                    CloseHandle(hVolume);
                }
            }
        }
        if (!espDrive.empty()) {
            std::string exportPath   = espDrive + "EFI\\Microsoft\\Boot\\BCD";
            std::string exportCmd    = BCD_CMD + " /export \"" + exportPath + "\"";
            std::string exportResult = Utils::exec(exportCmd.c_str());
            if (eventManager) {
                eventManager->notifyLogUpdate("Re-exportado BCD limpio a ESP.\r\n");
            }
        } else {
            if (eventManager) {
                eventManager->notifyLogUpdate("No se pudo detectar la particion ESP para re-exportar BCD.\r\n");
            }
        }
    }

    return deletedAny;
}

void BCDManager::cleanBootThatISOEntries() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream bcdLog((logDir + "\\" + BCD_CLEANUP_LOG_FILE).c_str(), std::ios::app);

    if (bcdLog) {
        time_t    now = time(nullptr);
        char      timeStr[100];
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        bcdLog << "\n========================================\n";
        bcdLog << "BCD Cleanup - " << timeStr << "\n";
        bcdLog << "========================================\n";
    }

    if (eventManager) {
        eventManager->notifyLogUpdate(
            LocalizedOrUtf8("log.bcd.cleaning_entries", "Limpiando entradas BCD de BootThatISO...\r\n"));
    }

    // Enumerate all BCD entries
    std::string enumOutput = Utils::exec((Utils::getBcdeditPath() + " /enum all").c_str());

    if (bcdLog) {
        bcdLog << "BCD enum output:\n" << enumOutput << "\n\n";
    }

    // Parse into blocks
    auto                     blocks = split(enumOutput, '\n');
    std::vector<std::string> entryBlocks;
    std::string              currentBlock;
    for (const auto &line : blocks) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            if (!currentBlock.empty()) {
                entryBlocks.push_back(currentBlock);
                currentBlock.clear();
            }
        } else {
            currentBlock += line + "\n";
        }
    }
    if (!currentBlock.empty())
        entryBlocks.push_back(currentBlock);

    auto icontains = [](const std::string &hay, const std::string &needle) {
        std::string h = hay;
        std::string n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return h.find(n) != std::string::npos;
    };

    int deletedCount = 0;

    // Look for entries to delete
    for (const auto &blk : entryBlocks) {
        bool        shouldDelete = false;
        std::string reason;

        // Check if contains ISOBOOT in description (only delete clear BootThatISO entries)
        if (icontains(blk, "isoboot")) {
            shouldDelete = true;
            reason       = "Contains ISOBOOT in description";
        }

        if (shouldDelete) {
            // Extract GUID
            size_t pos = blk.find('{');
            if (pos != std::string::npos) {
                size_t end = blk.find('}', pos);
                if (end != std::string::npos) {
                    std::string guid = blk.substr(pos, end - pos + 1);

                    // Don't delete {current}, {bootmgr}, or preserved Windows entries
                    bool isProtected = false;
                    if (guid == "{current}" || guid == "{bootmgr}" || guid == "{default}") {
                        isProtected = true;
                    } else if (icontains(blk, "windows") || icontains(blk, "boot manager") ||
                               icontains(blk, "bootmgr")) {
                        // Protect any entry that mentions Windows or boot manager
                        isProtected = true;
                        reason      = "Skipping protected system entry: " + guid;
                    }

                    if (isProtected) {
                        if (bcdLog) {
                            bcdLog << "Skipping protected entry: " << guid << " - " << reason << "\n";
                        }
                        continue;
                    }

                    if (bcdLog) {
                        bcdLog << "Deleting entry " << guid << " - Reason: " << reason << "\n";
                        bcdLog << "Entry details:\n" << blk << "\n";
                    }

                    std::string deleteCmd    = Utils::getBcdeditPath() + " /delete " + guid + " /f";
                    std::string deleteResult = Utils::exec(deleteCmd.c_str());

                    if (bcdLog) {
                        bcdLog << "Delete command: " << deleteCmd << "\n";
                        bcdLog << "Delete result: " << deleteResult << "\n\n";
                    }

                    if (eventManager) {
                        eventManager->notifyLogUpdate(LocalizedFormatUtf8(
                            "log.bcd.entry_deleted", {Utils::utf8_to_wstring(guid)}, "Eliminada entrada BCD: {0}\r\n"));
                    }

                    deletedCount++;
                }
            }
        }
    }

    if (bcdLog) {
        bcdLog << "Total entries deleted: " << deletedCount << "\n";
        bcdLog.close();
    }

    if (eventManager) {
        if (deletedCount > 0) {
            eventManager->notifyLogUpdate(LocalizedFormatUtf8("log.bcd.cleanup_complete",
                                                              {Utils::utf8_to_wstring(std::to_string(deletedCount))},
                                                              "Limpieza BCD completada. Entradas eliminadas: {0}\r\n"));
        } else {
            eventManager->notifyLogUpdate(LocalizedOrUtf8(
                "log.bcd.no_entries_found", "No se encontraron entradas BCD de BootThatISO para eliminar.\r\n"));
        }
    }
}
