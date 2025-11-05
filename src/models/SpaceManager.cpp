#include "SpaceManager.h"
#include "VolumeDetector.h"
#include "../services/bcdmanager.h"
#include "../utils/LocalizationHelpers.h"
#include <windows.h>
#include <vds.h>
#include <Objbase.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <algorithm>
#include "../utils/Utils.h"
#include "../utils/constants.h"

namespace {
std::string normalizeDriveRoot(const std::string &drive) {
    if (drive.empty()) {
        return "";
    }
    std::string normalized = drive;
    if (normalized.size() >= 2 && normalized[1] == ':') {
        normalized = normalized.substr(0, 2);
    }
    if (normalized.size() == 1 && (normalized[0] == '\\' || normalized[0] == '/')) {
        return normalized;
    }
    if (!normalized.empty() && normalized.back() != '\\' && normalized.back() != '/') {
        normalized += "\\";
    }
    return normalized;
}

std::string detectSystemDrive() {
    char  buffer[MAX_PATH] = {0};
    DWORD length           = GetEnvironmentVariableA("SystemDrive", buffer, MAX_PATH);
    if (length >= 2 && buffer[1] == ':') {
        return normalizeDriveRoot(std::string(buffer, length));
    }
    char windowsDir[MAX_PATH] = {0};
    UINT written              = GetWindowsDirectoryA(windowsDir, MAX_PATH);
    if (written >= 2 && windowsDir[1] == ':') {
        return normalizeDriveRoot(std::string(windowsDir, windowsDir + 2));
    }
    return "C:\\";
}
} // namespace

SpaceManager::SpaceManager(EventManager *eventManager)
    : eventManager_(eventManager), monitoredDrive_(detectSystemDrive()) {}

SpaceManager::~SpaceManager() {}

SpaceValidationResult SpaceManager::validateAvailableSpace() {
    long long availableGB = getAvailableSpaceGB();

    SpaceValidationResult result;
    result.availableGB = availableGB;
    result.isValid     = availableGB >= 10;

    // Log to file
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + SPACE_VALIDATION_LOG_FILE).c_str());
    if (logFile) {
        logFile << "Available space: " << availableGB << " GB\n";
        logFile << "Is valid: " << (result.isValid ? "yes" : "no") << "\n";
        if (!result.isValid) {
            std::ostringstream oss;
            oss << "No hay suficiente espacio disponible. Se requieren al menos 10 GB, pero solo hay " << availableGB
                << " GB disponibles.";
            result.errorMessage = oss.str();
            logFile << "Error: " << result.errorMessage << "\n";
        }
        logFile.close();
    }

    return result;
}

long long SpaceManager::getAvailableSpaceGB(const std::string &driveRoot) {
    std::string target = driveRoot.empty() ? monitoredDrive_ : normalizeDriveRoot(driveRoot);
    if (target.empty()) {
        target          = detectSystemDrive();
        monitoredDrive_ = target;
    }

    ULARGE_INTEGER freeBytesAvailable{}, totalNumberOfBytes{}, totalNumberOfFreeBytes{};
    if (GetDiskFreeSpaceExA(target.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return static_cast<long long>(freeBytesAvailable.QuadPart / (1024LL * 1024 * 1024));
    }
    return 0;
}

bool SpaceManager::performSpaceRecovery() {
    // Always recover space to ensure clean state
    if (eventManager_)
        eventManager_->notifyLogUpdate("Recuperando espacio para particiones...\r\n");
    if (!recoverSpace()) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.partition.space_recovery_failed", "Error: Falló la recuperación de espacio.\r\n"));
        return false;
    }
    return true;
}

bool SpaceManager::recoverSpace() {
    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.space.starting_recovery", "Iniciando recuperación de espacio...\r\n"));

    // Open log file
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + RECOVER_LOG_FILE).c_str(), std::ios::app);
    logFile << "Starting space recovery at " << __DATE__ << " " << __TIME__ << "\n";
    logFile.flush();

    try {
        logFile << "Entered try block\n";
        logFile.flush();

        // Check if Windows is using the ISOEFI partition
        logFile << "About to create VolumeDetector\n";
        logFile.flush();
        VolumeDetector volumeDetector(eventManager_);
        logFile << "VolumeDetector created\n";
        logFile.flush();
        logFile << "About to call isWindowsUsingEfiPartition\n";
        logFile.flush();
        bool windowsUsingEfi = volumeDetector.isWindowsUsingEfiPartition();
        logFile << "isWindowsUsingEfiPartition returned: " << (windowsUsingEfi ? "yes" : "no") << "\n";
        logFile.flush();

        if (windowsUsingEfi) {
            if (eventManager_)
                eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.partition.windows_using_efi",
                                                               "ADVERTENCIA: Windows está usando la partición ISOEFI. "
                                                               "Solo se eliminará ISOBOOT, ISOEFI se preservará.\r\n"));
            logFile << "Warning: Windows is using ISOEFI partition. Only ISOBOOT will be deleted.\n";
        }

        // Use Windows Storage API to find and delete volumes
        logFile << "Using Windows Storage API to enumerate volumes\n";
        logFile.flush();

        // First pass: collect volumes to delete
        std::vector<std::pair<std::string, std::string>> volumesToDelete; // pair<label, driveLetter>
        char                                             volumeName[MAX_PATH];
        HANDLE                                           hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));
        if (hVolume == INVALID_HANDLE_VALUE) {
            logFile << "FindFirstVolumeA failed, error: " << GetLastError() << "\n";
            logFile.close();
            if (eventManager_)
                eventManager_->notifyLogUpdate("Error: No se pudieron enumerar volúmenes.\r\n");
            return false;
        }

        do {
            // Remove trailing backslash
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }

            // Get volume information - need to add backslash for proper path
            std::string volumePath = std::string(volumeName) + "\\";
            logFile << "Checking volume path: " << volumePath << "\n";
            logFile.flush();

            // Get volume name/label
            char volumeLabel[MAX_PATH] = {0};
            if (GetVolumeInformationA(volumePath.c_str(), volumeLabel, sizeof(volumeLabel), nullptr, nullptr, nullptr,
                                      nullptr, 0)) {
                std::string label = volumeLabel;
                logFile << "Volume label: '" << label << "'\n";
                logFile.flush();

                if (eventManager_)
                    eventManager_->notifyLogUpdate(LocalizedFormatUtf8(
                        "log.space.found_volume", {Utils::utf8_to_wstring(label)}, "Volumen encontrado: {0}\r\n"));

                if (label == VOLUME_LABEL || label == EFI_VOLUME_LABEL) {
                    // Check if it's system EFI
                    bool isSystemEfi = false;
                    if (label == EFI_VOLUME_LABEL) {
                        // Try to mount temporarily to check for bootmgfw.efi
                        char mountPoint[] = "Z:\\";
                        if (SetVolumeMountPointA(mountPoint, volumeName)) {
                            std::string bootmgfwPath = std::string(mountPoint) + "EFI\\Microsoft\\Boot\\bootmgfw.efi";
                            if (GetFileAttributesA(bootmgfwPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                isSystemEfi = true;
                            }
                            DeleteVolumeMountPointA(mountPoint);
                        }
                    }

                    if (!isSystemEfi) {
                        // Find the drive letter for this volume
                        char driveLetter = 0;
                        char driveStrings[256];
                        logFile << "Looking for drive letter for volume: " << volumeName << "\n";
                        logFile.flush();
                        if (GetLogicalDriveStringsA(sizeof(driveStrings), driveStrings)) {
                            logFile << "Available drives: ";
                            char *drive = driveStrings;
                            while (*drive) {
                                logFile << drive << " ";
                                drive += strlen(drive) + 1;
                            }
                            logFile << "\n";
                            logFile.flush();

                            // Reset drive pointer
                            drive = driveStrings;
                            while (*drive) {
                                if (GetDriveTypeA(drive) != DRIVE_NO_ROOT_DIR) {
                                    char volumeGuid[MAX_PATH];
                                    // Use GetVolumeNameForVolumeMountPoint to get the volume GUID
                                    logFile << "Checking drive " << drive << "\n";
                                    logFile.flush();
                                    if (GetVolumeNameForVolumeMountPointA(drive, volumeGuid, sizeof(volumeGuid))) {
                                        logFile << "  GetVolumeNameForVolumeMountPoint returned: " << volumeGuid
                                                << "\n";
                                        // Remove trailing backslash from volumeName for comparison
                                        std::string volumeNameForCompare = volumeName;
                                        if (!volumeNameForCompare.empty() && volumeNameForCompare.back() == '\\') {
                                            volumeNameForCompare.pop_back();
                                        }
                                        // Compare with our volume name (handle null terminators)
                                        std::string guidStr = volumeGuid;
                                        size_t      minLen  = (guidStr.length() < volumeNameForCompare.length())
                                                                  ? guidStr.length()
                                                                  : volumeNameForCompare.length();
                                        logFile << "  Comparing first " << minLen << " chars\n";
                                        logFile.flush();
                                        if (guidStr.substr(0, minLen) == volumeNameForCompare.substr(0, minLen)) {
                                            logFile << "  SUBSTRING MATCH!\n";
                                            driveLetter = drive[0];
                                            logFile << "  Found matching drive letter: " << driveLetter << "\n";
                                            logFile.flush();
                                            break;
                                        } else {
                                            logFile << "  SUBSTRING NO MATCH\n";
                                            logFile.flush();
                                        }
                                    } else {
                                        logFile << "  GetVolumeNameForVolumeMountPoint failed for drive " << drive
                                                << ", error: " << GetLastError() << "\n";
                                        logFile.flush();
                                    }
                                } else {
                                    logFile << "  Drive " << drive << " has type DRIVE_NO_ROOT_DIR\n";
                                    logFile.flush();
                                }
                                drive += strlen(drive) + 1;
                            }
                        } else {
                            logFile << "GetLogicalDriveStrings failed, error: " << GetLastError() << "\n";
                            logFile.flush();
                        }

                        if (driveLetter != 0) {
                            volumesToDelete.push_back(std::make_pair(label, std::string(1, driveLetter)));
                            logFile << "Marked for deletion: " << label << " (Drive " << driveLetter << ":)\n";
                            logFile.flush();
                        } else {
                            // Try to assign a temporary drive letter for volumes without one
                            char tempDriveLetter = 0;
                            char availableDrives[256];
                            if (GetLogicalDriveStringsA(sizeof(availableDrives), availableDrives)) {
                                // Find an available drive letter (start from Z: backwards)
                                for (char letter = 'Z'; letter >= 'D'; --letter) {
                                    std::string driveCandidate = std::string(1, letter) + ":\\";
                                    if (GetDriveTypeA(driveCandidate.c_str()) == DRIVE_NO_ROOT_DIR) {
                                        // Try to assign this drive letter to the volume
                                        std::string mountPoint = std::string(1, letter) + ":";
                                        if (SetVolumeMountPointA(mountPoint.c_str(), volumeName)) {
                                            tempDriveLetter = letter;
                                            logFile << "Assigned temporary drive letter " << tempDriveLetter << ": to volume " << label << "\n";
                                            logFile.flush();
                                            break;
                                        }
                                    }
                                }
                            }
                            
                            if (tempDriveLetter != 0) {
                                volumesToDelete.push_back(std::make_pair(label, std::string(1, tempDriveLetter)));
                                logFile << "Marked for deletion: " << label << " (Temp Drive " << tempDriveLetter << ":)\n";
                                logFile.flush();
                            } else {
                                logFile << "Could not assign temporary drive letter for volume: " << label << "\n";
                                logFile.flush();
                            }
                        }
                    }
                }
            } else {
                logFile << "GetVolumeInformationA failed for: " << volumePath << "\n";
                logFile.flush();
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));

        FindVolumeClose(hVolume);

        // Second pass: delete volumes in reverse order (to avoid index shifting)
        for (auto it = volumesToDelete.rbegin(); it != volumesToDelete.rend(); ++it) {
            const std::string &label       = it->first;
            const std::string &driveLetter = it->second;

            logFile << "Attempting to delete volume: " << label << " (Drive " << driveLetter << ":)\n";
            logFile.flush();

            // Use diskpart to delete the volume by drive letter
            std::string   deleteCmd = "diskpart /s " + Utils::getExeDirectory() + "delete_volume.txt";
            std::ofstream deleteScript((Utils::getExeDirectory() + "delete_volume.txt").c_str());
            if (deleteScript) {
                // Use drive letter to select and delete
                deleteScript << "select volume=" << driveLetter << "\n";
                deleteScript << "delete volume override\n";
                deleteScript.close();

                STARTUPINFOA        si = {sizeof(si)};
                PROCESS_INFORMATION pi;
                si.dwFlags     = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                if (CreateProcessA(NULL, const_cast<char *>(deleteCmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si,
                                   &pi)) {
                    WaitForSingleObject(pi.hProcess, INFINITE);
                    DWORD exitCode;
                    GetExitCodeProcess(pi.hProcess, &exitCode);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);

                    if (exitCode == 0) {
                        if (eventManager_)
                            eventManager_->notifyLogUpdate(LocalizedFormatUtf8("log.space.volume_deleted",
                                                                               {Utils::utf8_to_wstring(label)},
                                                                               "Volumen {0} eliminado.\r\n"));
                        
                        // If this was a temporary drive letter, remove the mount point
                        // Check if this drive letter was assigned by us (not originally mounted)
                        char volumeGuid[MAX_PATH];
                        std::string mountPoint = driveLetter + ":\\";
                        if (GetVolumeNameForVolumeMountPointA(mountPoint.c_str(), volumeGuid, sizeof(volumeGuid))) {
                            // If GetVolumeNameForVolumeMountPoint succeeds, the volume still exists
                            // This means deletion failed, so don't remove the mount point
                            logFile << "Volume still exists after deletion attempt, keeping mount point\n";
                        } else {
                            // Volume was deleted, safe to remove mount point
                            std::string mountPointToRemove = driveLetter + ":";
                            if (DeleteVolumeMountPointA(mountPointToRemove.c_str())) {
                                logFile << "Removed temporary mount point " << mountPointToRemove << "\n";
                            } else {
                                logFile << "Failed to remove temporary mount point " << mountPointToRemove << ", error: " << GetLastError() << "\n";
                            }
                        }
                        logFile.flush();
                    } else {
                        if (eventManager_)
                            eventManager_->notifyLogUpdate(LocalizedFormatUtf8("log.space.volume_delete_failed",
                                                                               {Utils::utf8_to_wstring(label)},
                                                                               "Error al eliminar volumen {0}.\r\n"));
                    }
                } else {
                    logFile << "Failed to start diskpart process\n";
                    logFile.flush();
                }
            }
        }

        // Log if no volumes were deleted
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8(
                "log.space.no_volumes_deleted", "No se encontraron volúmenes ISOEFI o ISOBOOT para eliminar.\r\n"));

        // Resize C: partition
        // For simplicity, use diskpart for resize
        if (eventManager_)
            eventManager_->notifyLogUpdate("Redimensionando partición C:...\r\n");

        // Execute diskpart for resize
        STARTUPINFOA        si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        si.dwFlags            = STARTF_USESHOWWINDOW;
        si.wShowWindow        = SW_HIDE;
        std::string resizeCmd = "diskpart /s " + Utils::getExeDirectory() + "resize_script.txt";
        // Create resize script
        std::ofstream resizeScript((Utils::getExeDirectory() + "resize_script.txt").c_str());
        if (resizeScript) {
            resizeScript << "select disk 0\n";
            resizeScript << "select partition 3\n"; // Assume C: is partition 3
            resizeScript << "extend\n";
            resizeScript.close();
        }
        if (CreateProcessA(NULL, const_cast<char *>(resizeCmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (eventManager_)
                eventManager_->notifyLogUpdate("Redimensionamiento completado.\r\n");
        } else {
            if (eventManager_)
                eventManager_->notifyLogUpdate("Error al redimensionar.\r\n");
        }

        // Clean BCD entries created by BootThatISO
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.space.cleaning_bcd", "Limpiando entradas BCD...\r\n"));

        BCDManager &bcdManager = BCDManager::getInstance();
        bcdManager.setEventManager(eventManager_);
        bcdManager.cleanBootThatISOEntries();

        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.space.recovery_successful", "Espacio recuperado exitosamente.\r\n"));
        return true;
    } catch (const std::exception &e) {
        logFile << "Exception caught: " << e.what() << "\n";
        logFile.close();
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: Exception during space recovery: " + std::string(e.what()) + "\r\n");
        return false;
    } catch (...) {
        logFile << "Unknown exception caught\n";
        logFile.close();
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: Unknown exception during space recovery\r\n");
        return false;
    }
}