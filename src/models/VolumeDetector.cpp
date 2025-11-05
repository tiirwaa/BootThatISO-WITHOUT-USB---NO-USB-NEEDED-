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
    // Return true if any EFI partition exists (ISOEFI or SYSTEM)
    return !getEfiPartitionDriveLetter().empty();
}

std::string VolumeDetector::getPartitionDriveLetter() {
    return volumeManager_->getPartitionDriveLetter(VOLUME_LABEL, eventManager_);
}

std::string VolumeDetector::getEfiPartitionDriveLetter() {
    // Try ISOEFI first, then SYSTEM
    std::string driveLetter = volumeManager_->getPartitionDriveLetter(EFI_VOLUME_LABEL, eventManager_);
    if (!driveLetter.empty()) {
        return driveLetter;
    }
    return volumeManager_->getPartitionDriveLetter("SYSTEM", eventManager_);
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
    return volumeManager_->isWindowsUsingEfiPartition(eventManager_);
}

void VolumeDetector::logDiskStructure() {
    diskLogger_->logDiskStructure();
}
