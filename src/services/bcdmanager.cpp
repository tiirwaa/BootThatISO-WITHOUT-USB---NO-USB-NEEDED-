#include "bcdmanager.h"
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"
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

constexpr const char *BCD_CMD_PATH        = "C:\\Windows\\System32\\bcdedit.exe";
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
    std::string        output = Utils::exec((std::string(BCD_CMD_PATH) + " /enum {bootmgr}").c_str());
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

WORD BCDManager::GetMachineType(const std::string &filePath) {
    HANDLE hFile =
        CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;

    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMapping) {
        CloseHandle(hFile);
        return 0;
    }

    LPVOID lpBase = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!lpBase) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)lpBase;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        UnmapViewOfFile(lpBase);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((BYTE *)lpBase + dosHeader->e_lfanew);
    if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
        UnmapViewOfFile(lpBase);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    WORD machine = ntHeader->FileHeader.Machine;
    UnmapViewOfFile(lpBase);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return machine;
}

std::string BCDManager::configureBCD(const std::string &driveLetter, const std::string &espDriveLetter,
                                     BootStrategy &strategy) {
    captureBootmgrStateIfNeeded();
    const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";
    if (eventManager)
        eventManager->notifyLogUpdate(
            LocalizedOrUtf8("log.bcd.configuring", "Configurando Boot Configuration Data (BCD)...") + "\r\n");

    // Get volume GUID for data partition
    WCHAR        dataVolumeName[MAX_PATH];
    std::wstring wDriveLetter = Utils::utf8_to_wstring(driveLetter);
    if (wDriveLetter.empty() || wDriveLetter.back() != L'\\')
        wDriveLetter += L'\\';

    // Try GetVolumeNameForVolumeMountPointW with a few retries (transient mount timing issues)
    const int MAX_ATTEMPTS  = 3;
    BOOL      gotVolumeName = FALSE;
    DWORD     lastErr       = 0;

    // Prepare a small timestamp helper and a log file for detailed BCD config diagnostics
    auto getTS = []() -> std::string {
        char        buffer[64];
        std::time_t now = std::time(nullptr);
        std::tm     localTime;
        localtime_s(&localTime, &now);
        std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
        return std::string(buffer);
    };

    // Prepare a log file for detailed BCD config diagnostics
    std::string bcdLogDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(bcdLogDir.c_str(), NULL);
    std::ofstream dbgLog((bcdLogDir + "\\" + BCD_CONFIG_LOG_FILE).c_str(), std::ios::app);
    dbgLog << getTS() << "Attempting GetVolumeNameForVolumeMountPointW for: " << Utils::wstring_to_utf8(wDriveLetter)
           << std::endl;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        if (GetVolumeNameForVolumeMountPointW(wDriveLetter.c_str(), dataVolumeName, MAX_PATH)) {
            gotVolumeName = TRUE;
            dbgLog << getTS() << "GetVolumeNameForVolumeMountPointW succeeded on attempt " << attempt << std::endl;
            break;
        } else {
            lastErr = GetLastError();
            dbgLog << getTS() << "GetVolumeNameForVolumeMountPointW failed on attempt " << attempt
                   << ", error=" << lastErr << std::endl;
            // small delay before retry
            Sleep(500);
        }
    }

    if (!gotVolumeName) {
        if (eventManager)
            eventManager->notifyLogUpdate("GetVolumeNameForVolumeMountPointW failed for " + driveLetter +
                                          ", trying fallback enumeration...\r\n");
        dbgLog << getTS() << "Starting fallback volume enumeration to find mount point matching: "
               << Utils::wstring_to_utf8(wDriveLetter) << std::endl;

        BOOL   found = FALSE;
        WCHAR  volName[MAX_PATH];
        HANDLE hVol = FindFirstVolumeW(volName, MAX_PATH);
        if (hVol != INVALID_HANDLE_VALUE) {
            do {
                // Ensure volume name ends with backslash for GetVolumePathNamesForVolumeNameW
                std::wstring volNameStr = volName;
                if (volNameStr.back() != L'\\')
                    volNameStr.push_back(L'\\');

                dbgLog << getTS() << "Enumerating volume: " << Utils::wstring_to_utf8(volNameStr) << std::endl;

                // Get mount points for this volume
                DWORD returnLen = 0;
                BOOL  got       = GetVolumePathNamesForVolumeNameW(volNameStr.c_str(), NULL, 0, &returnLen);
                DWORD gle       = GetLastError();
                dbgLog << getTS() << "  GetVolumePathNamesForVolumeNameW initial call returned " << got
                       << " lastErr=" << gle << " needLen=" << returnLen << std::endl;

                if (!got && gle == ERROR_MORE_DATA && returnLen > 0) {
                    std::vector<WCHAR> buf(returnLen);
                    if (GetVolumePathNamesForVolumeNameW(volNameStr.c_str(), buf.data(), returnLen, &returnLen)) {
                        // buffer contains multi-string of path names
                        WCHAR *p = buf.data();
                        while (*p) {
                            std::wstring mountPoint(p);
                            if (mountPoint.back() != L'\\')
                                mountPoint.push_back(L'\\');
                            dbgLog << getTS() << "    Found mount point: " << Utils::wstring_to_utf8(mountPoint)
                                   << std::endl;
                            if (mountPoint == wDriveLetter) {
                                // Found matching volume
                                wcsncpy_s(dataVolumeName, volName, MAX_PATH);
                                found = TRUE;
                                dbgLog << getTS() << "    -> MATCH for " << Utils::wstring_to_utf8(wDriveLetter)
                                       << std::endl;
                                break;
                            }
                            p += wcslen(p) + 1;
                        }
                    } else {
                        dbgLog << getTS()
                               << "    GetVolumePathNamesForVolumeNameW failed on populate, err=" << GetLastError()
                               << std::endl;
                    }
                }

                if (found)
                    break;
            } while (FindNextVolumeW(hVol, volName, MAX_PATH));
            FindVolumeClose(hVol);
        } else {
            dbgLog << getTS() << "FindFirstVolumeW failed, err=" << GetLastError() << std::endl;
        }

        if (!found) {
            dbgLog << getTS() << "Fallback enumeration did not find a matching mount point for "
                   << Utils::wstring_to_utf8(wDriveLetter) << std::endl;
            dbgLog.close();
            return "Error al obtener el nombre del volumen de datos";
        }
        dbgLog << getTS() << "Fallback found volume: " << Utils::wstring_to_utf8(dataVolumeName) << std::endl;
        dbgLog.close();
    } else {
        // success from initial call; log and close debug log
        dbgLog << getTS() << "Volume name: " << Utils::wstring_to_utf8(dataVolumeName) << std::endl;
        dbgLog.close();
    }
    char narrowDataVolumeName[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, dataVolumeName, -1, narrowDataVolumeName, sizeof(narrowDataVolumeName), NULL, NULL);
    std::string dataDevice = narrowDataVolumeName;

    // Get volume GUID for ESP
    WCHAR        espVolumeName[MAX_PATH];
    std::wstring wEspDriveLetter = Utils::utf8_to_wstring(espDriveLetter);
    if (wEspDriveLetter.empty() || wEspDriveLetter.back() != L'\\')
        wEspDriveLetter += L'\\';
    if (!GetVolumeNameForVolumeMountPointW(wEspDriveLetter.c_str(), espVolumeName, MAX_PATH)) {
        return "Error al obtener el nombre del volumen ESP";
    }
    char narrowEspVolumeName[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, espVolumeName, -1, narrowEspVolumeName, sizeof(narrowEspVolumeName), NULL, NULL);
    std::string espDevice = narrowEspVolumeName;

    // Set default to current to avoid issues with deleting the default entry
    Utils::exec((BCD_CMD + " /default {current}").c_str());

    // Before creating new entry, preserve the current Windows entry by copying it
    // This ensures we always have a valid Windows entry to fall back to
    std::string preserveWindowsCmd = BCD_CMD + " /copy {default} /d \"Windows (System)\"";
    std::string preserveResult     = Utils::exec(preserveWindowsCmd.c_str());
    std::string windowsGuid;
    if (preserveResult.find("{") != std::string::npos && preserveResult.find("}") != std::string::npos) {
        size_t pos = preserveResult.find("{");
        size_t end = preserveResult.find("}", pos);
        if (end != std::string::npos) {
            windowsGuid = preserveResult.substr(pos, end - pos + 1);
            // Add the preserved Windows entry to display order
            Utils::exec((BCD_CMD + " /displayorder " + windowsGuid + " /addlast").c_str());
        }
    }

    // Delete any existing ISOBOOT entries to avoid duplicates
    // Use block parsing and case-insensitive search to handle localized bcdedit output
    std::string enumOutput = Utils::exec((BCD_CMD + " /enum all").c_str());
    auto        blocks     = split(enumOutput, '\n');
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
    for (const auto &blk : entryBlocks) {
        if (icontains(blk, labelToFind)) {
            // find GUID in block
            size_t pos = blk.find('{');
            if (pos != std::string::npos) {
                size_t end = blk.find('}', pos);
                if (end != std::string::npos) {
                    std::string guid      = blk.substr(pos, end - pos + 1);
                    std::string deleteCmd = BCD_CMD + " /delete " + guid + " /f"; // force remove
                    Utils::exec(deleteCmd.c_str());
                }
            }
        }
    }

    std::string bcdLabel = strategy.getBCDLabel();

    // Create appropriate BCD entry based on boot mode
    std::string output;
    if (strategy.getType() == "ramdisk") {
        // For ramdisk mode, create a new OSLOADER entry that supports ramdisk parameters
        output = Utils::exec((BCD_CMD + " /create /application OSLOADER /d \"" + bcdLabel + "\"").c_str());
    } else {
        // For other modes, copy the default entry
        output = Utils::exec((BCD_CMD + " /copy {default} /d \"" + bcdLabel + "\"").c_str());
    }

    // Validate by checking for GUID pattern (more reliable than searching for localized "error")
    // bcdedit always returns a GUID like {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} on success
    if (output.find("{") == std::string::npos || output.find("}") == std::string::npos)
        return "Error al crear/copiar entrada BCD";
    size_t pos = output.find("{");
    size_t end = output.find("}", pos);
    if (end == std::string::npos)
        return "Error al extraer GUID de la nueva entrada";
    std::string guid = output.substr(pos, end - pos + 1);

    // Find the EFI boot file in ESP - prioritize based on boot mode
    std::string efiBootFile;

    // Build candidate lists per mode and choose the best candidate by validating architecture
    std::vector<std::string> candidates;
    if (strategy.getType() == "extracted") {
        // Installation mode: prefer BOOTX64 first for Linux compatibility, then Microsoft bootmgfw
        candidates = {espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI", espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootx64.efi",
                      espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"};
    } else if (strategy.getType() == "ramdisk") {
        // Ramdisk mode: prefer Microsoft bootmgfw first, then BOOTX64
        candidates = {espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI", espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootx64.efi"};
    } else {
        candidates = {espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootx64.efi",
                      espDriveLetter + "\\EFI\\boot\\BOOTIA32.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootia32.efi",
                      espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgr.efi",
                      espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"};
    }

    // Evaluate candidates: collect existing candidates and their architectures
    int              bestIndex       = -1;
    int              bestScore       = -1; // 2 = amd64, 1 = i386, 0 = exists
    int              firstAmd64Index = -1;
    int              firstI386Index  = -1;
    std::vector<int> existingIndices;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const std::string &c = candidates[i];
        if (GetFileAttributesA(c.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        existingIndices.push_back((int)i);
        WORD m     = GetMachineType(c);
        int  score = 0;
        if (m == IMAGE_FILE_MACHINE_AMD64)
            score = 2;
        else if (m == IMAGE_FILE_MACHINE_I386)
            score = 1;
        else
            score = 0;
        if (firstAmd64Index == -1 && m == IMAGE_FILE_MACHINE_AMD64)
            firstAmd64Index = (int)i;
        if (firstI386Index == -1 && m == IMAGE_FILE_MACHINE_I386)
            firstI386Index = (int)i;
        if (eventManager) {
            std::ostringstream oss;
            oss << std::hex << std::uppercase << m;
            eventManager->notifyLogUpdate("Candidate EFI: " + c + ", machine=0x" + oss.str() +
                                          " score=" + std::to_string(score) + "\r\n");
        }
        if (score > bestScore) {
            bestScore = score;
            bestIndex = (int)i;
            if (bestScore == 2)
                break; // best possible
        }
    }

    // If we found both architectures available, ask the user which one to use
    if (firstAmd64Index != -1 && firstI386Index != -1) {
        std::string  amdPath  = candidates[firstAmd64Index];
        std::string  i386Path = candidates[firstI386Index];
        std::wstring wmsg     = L"Se encontraron EFI de ambas arquitecturas en la ESP:\n\n";
        wmsg += Utils::utf8_to_wstring(std::string("x64: ") + amdPath) + L"\n";
        wmsg += Utils::utf8_to_wstring(std::string("x86: ") + i386Path) + L"\n\n";
        std::wstring selectionPrompt = LocalizedOrW("message.selectEfiArchitecturePrompt",
                                                    L"?Cu?l desea usar? Seleccione S? para x64, No para x86.");
        wmsg += selectionPrompt;
        std::wstring selectionTitle = LocalizedOrW("title.selectEfiArchitecture", L"Seleccionar arquitectura EFI");
        int          res = MessageBoxW(NULL, wmsg.c_str(), selectionTitle.c_str(), MB_YESNO | MB_ICONQUESTION);
        if (res == IDYES) {
            bestIndex = firstAmd64Index;
        } else {
            bestIndex = firstI386Index;
        }
    }

    if (bestIndex != -1) {
        efiBootFile = candidates[bestIndex];
    } else if (!existingIndices.empty()) {
        // fallback: pick the first existing candidate
        efiBootFile = candidates[existingIndices[0]];
    } else {
        return "Archivo EFI boot no encontrado en ESP";
    }

    WORD machine = GetMachineType(efiBootFile);
    if (machine != IMAGE_FILE_MACHINE_AMD64 && machine != IMAGE_FILE_MACHINE_I386) {
        if (eventManager)
            eventManager->notifyLogUpdate(
                LocalizedOrUtf8("log.bcd.unsupportedArchitecture", "Error: Arquitectura EFI no soportada.") + "\r\n");
        return "Arquitectura EFI no soportada";
    }

    // Validate EFI architecture matches system architecture
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    WORD sysArch = (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)   ? IMAGE_FILE_MACHINE_AMD64
                   : (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) ? IMAGE_FILE_MACHINE_I386
                                                                                      : 0;
    if (machine != sysArch) {
        std::string errorMsg = "Error: Arquitectura EFI (0x" + std::to_string(machine) +
                               ") no coincide con la del sistema (0x" + std::to_string(sysArch) + ").\r\n";
        if (eventManager)
            eventManager->notifyLogUpdate(errorMsg);
        return "Arquitectura EFI no compatible con el sistema";
    }

    // Log selected file and mode
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL); // Create logs directory if it doesn't exist
    std::ofstream logFile((logDir + "\\" + BCD_CONFIG_LOG_FILE).c_str(), std::ios::app);
    logFile << "Selected EFI boot file: " << efiBootFile << std::endl;
    logFile << "Selected mode: " << bcdLabel << std::endl;
    logFile << "ESP drive letter: " << espDriveLetter << std::endl;

    // Log detailed file checking for debugging
    if (strategy.getType() == "extracted") {
        logFile << "EFI file selection for extracted mode:" << std::endl;
        std::string candidates[] = {
            espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI", espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\bootx64.efi", espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"};
        for (const auto &candidate : candidates) {
            bool exists = (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES);
            logFile << "  " << candidate << " - " << (exists ? "EXISTS" : "NOT FOUND");
            if (candidate == efiBootFile)
                logFile << " [SELECTED]";
            logFile << std::endl;
        }
    } else if (strategy.getType() == "ramdisk") {
        logFile << "EFI file selection for ramdisk mode:" << std::endl;
        std::string candidates[] = {
            espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI", espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\bootx64.efi", espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"};
        for (const auto &candidate : candidates) {
            bool exists = (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES);
            logFile << "  " << candidate << " - " << (exists ? "EXISTS" : "NOT FOUND");
            if (candidate == efiBootFile)
                logFile << " [SELECTED]";
            logFile << std::endl;
        }
    }

    if (eventManager) {
        std::string message = LocalizedOrUtf8("log.bcd.efiSelected", "Archivo EFI seleccionado: {0}");
        size_t      pos     = message.find("{0}");
        if (pos != std::string::npos) {
            message.replace(pos, 3, efiBootFile);
        }
        eventManager->notifyLogUpdate(message + "\r\n");
    }

    // Compute the relative path for BCD
    std::string efiPath = efiBootFile.substr(espDriveLetter.length());
    // efiPath matches the case in the file system

    logFile << "EFI path: " << efiPath << "\n";

    // For extracted mode, verify SDI file exists (only for Windows ISOs that have bootmgr)
    if (strategy.getType() == "extracted") {
        std::string bootmgrPath = driveLetter + "\\bootmgr";
        bool        hasBootmgr  = (GetFileAttributesA(bootmgrPath.c_str()) != INVALID_FILE_ATTRIBUTES);
        if (hasBootmgr) {
            std::string sdiPath = driveLetter + "\\boot\\boot.sdi";
            if (GetFileAttributesA(sdiPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                std::string errorMsg = "Error: Archivo SDI no encontrado en " + sdiPath + "\r\n";
                if (eventManager)
                    eventManager->notifyLogUpdate(errorMsg);
                logFile << errorMsg;
                logFile.close();
                return "Archivo SDI no encontrado para extracted boot";
            }
            logFile << "SDI file verified: " << sdiPath << std::endl;
        }
    } else if (strategy.getType() == "ramdisk") {
        // For ramdisk mode, verify boot.wim and boot.sdi were staged on the data partition
        std::string bootWimPath   = driveLetter + "\\sources\\boot.wim";
        std::string bootSdiPath   = driveLetter + "\\boot\\boot.sdi";
        bool        bootWimExists = GetFileAttributesA(bootWimPath.c_str()) != INVALID_FILE_ATTRIBUTES;
        bool        bootSdiExists = GetFileAttributesA(bootSdiPath.c_str()) != INVALID_FILE_ATTRIBUTES;

        if (!bootWimExists || !bootSdiExists) {
            std::string errorMsg = "Error: Faltan archivos requeridos para RAM disk: ";
            bool        first    = true;
            if (!bootWimExists) {
                errorMsg += "boot.wim (" + bootWimPath + ")";
                first = false;
            }
            if (!bootSdiExists) {
                if (!first)
                    errorMsg += ", ";
                errorMsg += "boot.sdi (" + bootSdiPath + ")";
            }
            errorMsg += "\r\n";
            if (eventManager)
                eventManager->notifyLogUpdate(errorMsg);
            logFile << errorMsg;
            logFile.close();
            return "Archivos necesarios para RAM disk incompletos";
        }
        logFile << "boot.wim verificado: " << bootWimPath << std::endl;
        logFile << "boot.sdi verificado: " << bootSdiPath << std::endl;
    }

    // Pass simple drive letters (e.g., "Z:") to strategies so they can use partition= syntax
    strategy.configureBCD(guid, driveLetter, espDriveLetter, efiPath);

    // Log after strategy configuration
    logFile << "Strategy configuration completed. Proceeding with final BCD setup...\n";

    // Remove systemroot for EFI booting (ignore error if it doesn't exist - normal for OSLOADER entries)
    if (strategy.getType() != "ramdisk") {
        std::string cmd4    = BCD_CMD + " /deletevalue " + guid + " systemroot";
        std::string result4 = Utils::exec(cmd4.c_str());
        logFile << "Remove systemroot command: " << cmd4 << "\nResult: " << result4 << "\n";
        // Note: This may fail for OSLOADER entries that don't have systemroot - that's OK
    }

    // Check if this is a Linux EXTRACT entry (device points to ESP)
    std::string deviceQuery    = BCD_CMD + " /enum " + guid + " | findstr \"device\"";
    std::string deviceOutput   = Utils::exec(deviceQuery.c_str());
    bool        isLinuxExtract = (deviceOutput.find(espDriveLetter) != std::string::npos);

    if (!isLinuxExtract) {
        std::string cmd6    = BCD_CMD + " /default " + guid;
        std::string result6 = Utils::exec(cmd6.c_str());
        logFile << "Set default command: " << cmd6 << "\nResult: " << result6 << "\n";
        // Check for common error indicators in multiple languages
        // Note: bcdedit typically produces minimal output on success, verbose output on error
        bool hasError = (result6.find("error") != std::string::npos || result6.find("Error") != std::string::npos ||
                         result6.find("Fehler") != std::string::npos || // German
                         result6.find("erreur") != std::string::npos || // French
                         result6.find("erro") != std::string::npos);    // Portuguese
        if (hasError && result6.length() > 50) {                        // If verbose error message
            if (eventManager) {
                std::string message = LocalizedOrUtf8("log.bcd.errorDefault", "Error al configurar default: {0}");
                size_t      pos     = message.find("{0}");
                if (pos != std::string::npos) {
                    message.replace(pos, 3, result6);
                }
                eventManager->notifyLogUpdate(message + "\r\n");
            }
            logFile << "ERROR: Failed to set default boot entry. Result: " << result6 << "\n";
            logFile.close();
            return "Error al configurar default: " + result6;
        }
    } else {
        logFile << "Skipping set default for Linux EXTRACT entry (device: " << deviceOutput << ")\n";
    }

    // Add to display order so it appears in boot menu
    std::string cmdDisplay    = BCD_CMD + " /displayorder " + guid + " /addfirst";
    std::string resultDisplay = Utils::exec(cmdDisplay.c_str());
    logFile << "Displayorder command: " << cmdDisplay << "\nResult: " << resultDisplay << "\n";
    // Check for error indicators in multiple languages
    bool hasDisplayError =
        (resultDisplay.find("error") != std::string::npos || resultDisplay.find("Error") != std::string::npos ||
         resultDisplay.find("Fehler") != std::string::npos || resultDisplay.find("erreur") != std::string::npos ||
         resultDisplay.find("erro") != std::string::npos);
    if (hasDisplayError && resultDisplay.length() > 50) {
        if (eventManager) {
            std::string message = LocalizedOrUtf8("log.bcd.errorDisplayorder", "Error al configurar displayorder: {0}");
            size_t      pos     = message.find("{0}");
            if (pos != std::string::npos) {
                message.replace(pos, 3, resultDisplay);
            }
            eventManager->notifyLogUpdate(message + "\r\n");
        }
        logFile << "ERROR: Failed to add to displayorder. Result: " << resultDisplay << "\n";
        logFile.close();
        return "Error al configurar displayorder: " + resultDisplay;
    }

    // Set timeout to allow boot selection
    std::string cmdTimeout    = BCD_CMD + " /set {bootmgr} timeout 30";
    std::string resultTimeout = Utils::exec(cmdTimeout.c_str());
    logFile << "Set timeout command: " << cmdTimeout << "\nResult: " << resultTimeout << "\n";
    // Check for error indicators in multiple languages
    bool hasTimeoutError =
        (resultTimeout.find("error") != std::string::npos || resultTimeout.find("Error") != std::string::npos ||
         resultTimeout.find("Fehler") != std::string::npos || resultTimeout.find("erreur") != std::string::npos ||
         resultTimeout.find("erro") != std::string::npos);
    if (hasTimeoutError && resultTimeout.length() > 50) {
        if (eventManager) {
            std::string message = LocalizedOrUtf8("log.bcd.errorTimeout", "Error al configurar timeout: {0}");
            size_t      pos     = message.find("{0}");
            if (pos != std::string::npos) {
                message.replace(pos, 3, resultTimeout);
            }
            eventManager->notifyLogUpdate(message + "\r\n");
        }
        logFile << "ERROR: Failed to set timeout. Result: " << resultTimeout << "\n";
        logFile.close();
        return "Error al configurar timeout: " + resultTimeout;
    }

    logFile << "BCD configuration completed successfully\n";

    // For extracted mode, export the configured BCD to ESP so bootmgfw.efi can find it
    if (strategy.getType() == "extracted") {
        std::string exportPath   = espDriveLetter + "\\EFI\\Microsoft\\Boot\\BCD";
        std::string exportCmd    = BCD_CMD + " /export \"" + exportPath + "\"";
        std::string exportResult = Utils::exec(exportCmd.c_str());
        logFile << "Export BCD to ESP command: " << exportCmd << "\nResult: " << exportResult << "\n";
    }

    logFile.close();

    return "";
}

bool BCDManager::restoreBCD() {
    const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";
    std::string       output  = Utils::exec((BCD_CMD + " /enum all").c_str());
    auto              blocks  = split(output, '\n');
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

    return deletedAny;
}

void BCDManager::cleanBootThatISOEntries() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream bcdLog((logDir + "\\bcd_cleanup_log.log").c_str(), std::ios::app);

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
    std::string enumOutput = Utils::exec((std::string(BCD_CMD_PATH) + " /enum all").c_str());

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

        // Check if contains ISOBOOT in description
        if (icontains(blk, "isoboot")) {
            shouldDelete = true;
            reason       = "Contains ISOBOOT in description";
        }
        // Check if device points to boot.wim (RAMDisk entry from BootThatISO)
        else if (icontains(blk, "boot.wim") && icontains(blk, "ramdisk")) {
            shouldDelete = true;
            reason       = "RAMDisk entry pointing to boot.wim";
        }
        // Check if it's a Windows Setup entry with boot.wim device
        else if (icontains(blk, "windows setup") && icontains(blk, "[boot]\\sources\\boot.wim")) {
            shouldDelete = true;
            reason       = "Windows Setup entry with BootThatISO boot.wim";
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
                    if (guid == "{current}" || guid == "{bootmgr}") {
                        isProtected = true;
                    } else if (guid == "{default}") {
                        // Only protect {default} if it doesn't contain ISOBOOT
                        isProtected = !icontains(blk, "isoboot");
                    } else if (icontains(blk, "windows (system)") || icontains(blk, "windows 10")) {
                        // Protect preserved Windows entries
                        isProtected = true;
                        reason      = "Skipping protected Windows entry: " + guid;
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

                    std::string deleteCmd    = std::string(BCD_CMD_PATH) + " /delete " + guid + " /f";
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
