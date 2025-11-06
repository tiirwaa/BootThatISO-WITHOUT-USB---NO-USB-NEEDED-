#include "VolumeDetector.h"
#include <windows.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <set>
#include <vector>
#include <algorithm>
#include <oleauto.h>
#include <winioctl.h>
#pragma comment(lib, "oleaut32.lib")
#include "../utils/Utils.h"
#include "../utils/constants.h"

VolumeDetector::VolumeDetector(EventManager *eventManager)
    : eventManager_(eventManager), volumeManager_(std::make_unique<VolumeManager>()),
      diskLogger_(std::make_unique<DiskLogger>(eventManager)) {
    logDiskStructure();
}

VolumeDetector::~VolumeDetector() {}

bool VolumeDetector::partitionExists() {
    return volumeManager_->partitionExists(VOLUME_LABEL, eventManager_);
}

bool VolumeDetector::efiPartitionExists() {
    // Return true only if EFI partition exists with correct size
    std::string drive = getEfiPartitionDriveLetter();
    if (drive.empty())
        return false;
    int size = getEfiPartitionSizeMB();
    return size == REQUIRED_EFI_SIZE_MB;
}

std::string VolumeDetector::getPartitionDriveLetter() {
    return volumeManager_->getPartitionDriveLetter(VOLUME_LABEL, eventManager_);
}

std::string VolumeDetector::getEfiPartitionDriveLetter() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\debug_efi_check.log").c_str(), std::ios::app);
    // Try EFI labels in order: prefer ISOEFI first, then SYSTEM/EFI/ESP
    std::vector<std::string> labels = {EFI_VOLUME_LABEL, "SYSTEM", "EFI", "ESP"};
    for (const auto &label : labels) {
        std::string driveLetter = volumeManager_->getPartitionDriveLetter(label.c_str(), eventManager_);
        if (logFile) {
            logFile << "Trying EFI label '" << label << "': '" << driveLetter << "'\n";
        }
        if (!driveLetter.empty()) {
            if (logFile) {
                logFile << "Found EFI partition with label '" << label << "' at " << driveLetter << "\n";
                logFile.close();
            }
            return driveLetter;
        }
    }
    if (logFile) {
        logFile << "No EFI partition found with any known labels\n";
        logFile.close();
    }
    return "";
}

std::string VolumeDetector::getPartitionFileSystem() {
    return volumeManager_->getPartitionFileSystem(VOLUME_LABEL, eventManager_);
}

int VolumeDetector::getEfiPartitionSizeMB() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\" + EFI_PARTITION_SIZE_LOG_FILE).c_str());

    debugLog << "Getting EFI partition size...\n";

    // First try to get drive letter
    std::string efiDrive = getEfiPartitionDriveLetter();
    if (!efiDrive.empty()) {
        debugLog << "EFI drive found: " << efiDrive << "\n";

        // Remove trailing backslash for CreateFile
        std::string driveForHandle = efiDrive;
        if (!driveForHandle.empty() && driveForHandle.back() == '\\') {
            driveForHandle.pop_back();
        }

        // Open volume handle
        std::string volumePath = "\\\\.\\" + driveForHandle;
        HANDLE      hVolume    = CreateFileA(volumePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                             OPEN_EXISTING, 0, NULL);

        if (hVolume != INVALID_HANDLE_VALUE) {
            debugLog << "Volume handle opened successfully\n";

            // Get partition information
            PARTITION_INFORMATION_EX partInfo;
            DWORD                    bytesReturned = 0;

            if (DeviceIoControl(hVolume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partInfo, sizeof(partInfo),
                                &bytesReturned, NULL)) {

                ULONGLONG sizeBytes = partInfo.PartitionLength.QuadPart;
                int       sizeMB    = static_cast<int>(sizeBytes / (1024 * 1024));

                debugLog << "Partition size: " << sizeBytes << " bytes (" << sizeMB << " MB)\n";
                CloseHandle(hVolume);
                debugLog.close();
                return sizeMB;
            } else {
                debugLog << "DeviceIoControl IOCTL_DISK_GET_PARTITION_INFO_EX failed, error: " << GetLastError()
                         << "\n";
            }

            CloseHandle(hVolume);
        } else {
            debugLog << "Failed to open volume handle, error: " << GetLastError() << "\n";
        }
    } else {
        debugLog << "EFI partition not found\n";
    }

    debugLog << "Returning 0 (could not determine size)\n";
    debugLog.close();
    return 0; // Could not determine size
}

int VolumeDetector::countEfiPartitions() {
    return volumeManager_->countEfiPartitions(eventManager_);
}

bool VolumeDetector::isWindowsUsingEfiPartition() {
    std::string efiDrive = getEfiPartitionDriveLetter();
    std::string logDir   = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\debug_efi_check.log").c_str(), std::ios::app);
    if (logFile) {
        logFile << "Checking if Windows is using EFI partition\n";
        logFile << "EFI drive letter: '" << efiDrive << "'\n";
    }
    if (!efiDrive.empty()) {
        std::string bootmgfwPath = efiDrive + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
        if (logFile) {
            logFile << "Checking path: " << bootmgfwPath << "\n";
        }
        DWORD attrs = GetFileAttributesA(bootmgfwPath.c_str());
        if (logFile) {
            logFile << "File attributes: " << attrs << " (INVALID_FILE_ATTRIBUTES = " << INVALID_FILE_ATTRIBUTES
                    << ")\n";
        }
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            if (logFile) {
                logFile << "bootmgfw.efi found - Windows is using EFI\n";
                logFile.close();
            }
            return true;
        } else {
            if (logFile) {
                logFile << "bootmgfw.efi NOT found on labeled EFI partition\n";
            }
        }
    } else {
        if (logFile) {
            logFile << "EFI drive letter is empty\n";
        }
    }

    // Fallback: search all mounted drives for bootmgfw.efi
    if (logFile) {
        logFile << "Searching all drives for bootmgfw.efi as fallback\n";
    }
    for (char drive = 'A'; drive <= 'Z'; ++drive) {
        std::string driveStr     = std::string(1, drive) + ":\\";
        std::string bootmgfwPath = driveStr + "EFI\\Microsoft\\Boot\\bootmgfw.efi";
        DWORD       attrs        = GetFileAttributesA(bootmgfwPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            // Check if it's FAT32 and size ~500MB
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
                    // Check filesystem
                    char fsName[256];
                    if (GetVolumeInformationA(driveStr.c_str(), NULL, 0, NULL, NULL, NULL, fsName, sizeof(fsName))) {
                        if (logFile) {
                            logFile << "Found bootmgfw.efi on " << driveStr << ", size " << sizeMB << "MB, FS "
                                    << fsName << "\n";
                        }
                        if (sizeMB >= 100 && sizeMB <= 1024 && strcmp(fsName, "FAT32") == 0) {
                            if (logFile) {
                                logFile << "EFI partition found via fallback on " << driveStr
                                        << " - Windows is using EFI\n";
                                logFile.close();
                            }
                            CloseHandle(hVolume);
                            return true;
                        }
                    }
                }
                CloseHandle(hVolume);
            }
        }
    }

    if (logFile) {
        logFile << "Windows is NOT using EFI partition\n";
        logFile.close();
    }
    return false;
}

void VolumeDetector::logDiskStructure() {
    diskLogger_->logDiskStructure();
}

std::string VolumeDetector::getIsoEfiPartitionDriveLetter() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\debug_efi_check.log").c_str(), std::ios::app);

    std::string driveLetter = volumeManager_->getPartitionDriveLetter(EFI_VOLUME_LABEL, eventManager_);
    if (logFile) {
        logFile << "Trying ISOEFI label '" << EFI_VOLUME_LABEL << "': '" << driveLetter << "'\n";
        if (!driveLetter.empty()) {
            logFile << "Found ISOEFI partition at " << driveLetter << "\n";
        } else {
            logFile << "No ISOEFI partition found\n";
        }
        logFile.close();
    }
    return driveLetter;
}
