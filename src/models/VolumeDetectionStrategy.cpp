#include "VolumeDetectionStrategy.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include "../models/EventManager.h"
#include <windows.h>
#include <algorithm>
#include <set>
#include <cstdio>
#include <string>

// DriveLetterVolumeDetector implementation
std::vector<std::pair<std::string, std::string>> DriveLetterVolumeDetector::detectVolumes(EventManager *eventManager) {
    std::vector<std::pair<std::string, std::string>> volumes;

    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    char *drive = drives;
    while (*drive) {
        if (GetDriveTypeA(drive) == DRIVE_FIXED) {
            char  volumeName[MAX_PATH];
            char  fileSystem[MAX_PATH];
            DWORD serialNumber, maxComponentLen, fileSystemFlags;

            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen,
                                      &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                std::string label       = volumeName;
                std::string driveLetter = drive;
                // Remove trailing backslash
                if (!driveLetter.empty() && driveLetter.back() == '\\') {
                    driveLetter.pop_back();
                }

                volumes.emplace_back(label, driveLetter);

                if (eventManager) {
                    eventManager->notifyLogUpdate("Detected assigned volume: '" + label + "' at " + driveLetter +
                                                  ":\r\n");
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    return volumes;
}

bool DriveLetterVolumeDetector::partitionExists(const std::string &label, [[maybe_unused]] EventManager *eventManager) {
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    char *drive = drives;
    while (*drive) {
        if (GetDriveTypeA(drive) == DRIVE_FIXED) {
            char  volumeName[MAX_PATH];
            char  fileSystem[MAX_PATH];
            DWORD serialNumber, maxComponentLen, fileSystemFlags;

            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen,
                                      &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                if (_stricmp(volumeName, label.c_str()) == 0) {
                    return true;
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    return false;
}

// UnassignedVolumeDetector implementation
std::vector<std::pair<std::string, std::string>> UnassignedVolumeDetector::detectVolumes(EventManager *eventManager) {
    std::vector<std::pair<std::string, std::string>> volumes;

    char   volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));

    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }

            // Check if this volume has a drive letter assigned
            char driveStrings[256];
            bool hasDriveLetter = false;

            if (GetLogicalDriveStringsA(sizeof(driveStrings), driveStrings)) {
                char *drive = driveStrings;
                while (*drive) {
                    char volumeGuid[MAX_PATH];
                    if (GetVolumeNameForVolumeMountPointA(drive, volumeGuid, sizeof(volumeGuid))) {
                        std::string volumeNameForCompare = volumeName;
                        std::string guidStr              = volumeGuid;
                        size_t      minLen               = guidStr.length() < volumeNameForCompare.length()
                                                               ? guidStr.length()
                                                               : volumeNameForCompare.length();
                        if (guidStr.substr(0, minLen) == volumeNameForCompare.substr(0, minLen)) {
                            hasDriveLetter = true;
                            break;
                        }
                    }
                    drive += strlen(drive) + 1;
                }
            }

            // Only process volumes without drive letters
            if (!hasDriveLetter) {
                char        volName[MAX_PATH] = {0};
                char        fsName[MAX_PATH]  = {0};
                DWORD       serial, maxComp, flags;
                std::string volPath = std::string(volumeName) + "\\";

                if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                          sizeof(fsName))) {
                    std::string label      = volName;
                    std::string volumePath = volumeName; // Store the \\?\Volume{...} path

                    volumes.emplace_back(label, volumePath);

                    if (eventManager) {
                        eventManager->notifyLogUpdate("Detected unassigned volume: '" + label + "' at " + volumePath +
                                                      "\r\n");
                    }
                }
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));

        FindVolumeClose(hVolume);
    }

    return volumes;
}

bool UnassignedVolumeDetector::partitionExists(const std::string &label, [[maybe_unused]] EventManager *eventManager) {
    char   volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));

    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }

            char        volName[MAX_PATH] = {0};
            char        fsName[MAX_PATH]  = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volumeName) + "\\";

            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                if (_stricmp(volName, label.c_str()) == 0) {
                    FindVolumeClose(hVolume);
                    return true;
                }
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));

        FindVolumeClose(hVolume);
    }

    return false;
}

// AssignDriveLetterCommand implementation
AssignDriveLetterCommand::AssignDriveLetterCommand(const std::string &volumePath, const std::string &preferredLetter)
    : volumePath(volumePath), preferredLetter(preferredLetter) {}

bool AssignDriveLetterCommand::execute(EventManager *eventManager) {
    // First verify the volume exists (CreateFile needs path without trailing backslash)
    std::string volumePathForCreateFile = volumePath;
    if (!volumePathForCreateFile.empty() && volumePathForCreateFile.back() == '\\') {
        volumePathForCreateFile.pop_back();
    }

    HANDLE hVolume = CreateFileA(volumePathForCreateFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hVolume == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        if (eventManager) {
            eventManager->notifyLogUpdate("Volume " + volumePath + " does not exist or is not accessible (error: " +
                                          std::to_string(errorCode) + ")\r\n");
        }
        return false;
    }
    CloseHandle(hVolume);

    // Check if volume is formatted by trying to get volume information
    std::string volumePathForInfo = volumePath;
    if (!volumePathForInfo.empty() && volumePathForInfo.back() != '\\') {
        volumePathForInfo += '\\';
    }

    char  volumeLabel[MAX_PATH] = {0};
    DWORD serialNumber = 0, maxComponentLen = 0, fileSystemFlags = 0;
    char  fileSystem[MAX_PATH] = {0};

    if (!GetVolumeInformationA(volumePathForInfo.c_str(), volumeLabel, sizeof(volumeLabel), &serialNumber,
                               &maxComponentLen, &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
        DWORD errorCode = GetLastError();
        if (eventManager) {
            eventManager->notifyLogUpdate("Volume " + volumePath + " is not formatted or accessible (error: " +
                                          std::to_string(errorCode) + ")\r\n");
        }
        return false;
    }

    if (eventManager) {
        eventManager->notifyLogUpdate("Volume " + volumePath + " is formatted as " + std::string(fileSystem) +
                                      " with label '" + std::string(volumeLabel) + "'\r\n");
    }

    // Try preferred letter first, then any available letter
    std::vector<char> lettersToTry;

    if (!preferredLetter.empty()) {
        lettersToTry.push_back(preferredLetter[0]);
    }

    // Add letters from Z to D
    for (char letter = 'Z'; letter >= 'D'; --letter) {
        if (preferredLetter.empty() || letter != preferredLetter[0]) {
            lettersToTry.push_back(letter);
        }
    }

    for (char letter : lettersToTry) {
        std::string driveCandidate = std::string(1, letter) + ":\\";
        if (GetDriveTypeA(driveCandidate.c_str()) == DRIVE_NO_ROOT_DIR) {
            // Use mountvol.exe instead of SetVolumeMountPointA for better reliability
            std::string mountvolCmd = "mountvol " + std::string(1, letter) + ": " + volumePath;
            if (eventManager) {
                eventManager->notifyLogUpdate("Executing: " + mountvolCmd + "\r\n");
            }

            // Execute mountvol command
            int result = system(mountvolCmd.c_str());
            if (result == 0) {
                if (eventManager) {
                    eventManager->notifyLogUpdate("Successfully assigned drive letter " + std::string(1, letter) +
                                                  ": to volume " + volumePath + "\r\n");
                }
                return true;
            } else {
                if (eventManager) {
                    eventManager->notifyLogUpdate("Failed to assign drive letter " + std::string(1, letter) +
                                                  ": to volume " + volumePath +
                                                  " (mountvol exit code: " + std::to_string(result) + ")\r\n");
                }
            }
        }
    }

    if (eventManager) {
        eventManager->notifyLogUpdate("Could not assign any drive letter to volume " + volumePath + "\r\n");
    }
    return false;
}

std::string AssignDriveLetterCommand::getDescription() const {
    return "Assign drive letter to volume: " + volumePath;
}

// RemoveDriveLetterCommand implementation
RemoveDriveLetterCommand::RemoveDriveLetterCommand(const std::string &driveLetter) : driveLetter(driveLetter) {}

bool RemoveDriveLetterCommand::execute(EventManager *eventManager) {
    std::string mountPoint = driveLetter;
    if (mountPoint.back() != ':') {
        mountPoint += ":";
    }

    if (DeleteVolumeMountPointA(mountPoint.c_str())) {
        if (eventManager) {
            eventManager->notifyLogUpdate("Successfully removed drive letter " + mountPoint + "\r\n");
        }
        return true;
    } else {
        if (eventManager) {
            eventManager->notifyLogUpdate("Failed to remove drive letter " + mountPoint + "\r\n");
        }
        return false;
    }
}

std::string RemoveDriveLetterCommand::getDescription() const {
    return "Remove drive letter: " + driveLetter;
}

// VolumeManager implementation
VolumeManager::VolumeManager()
    : assignedDetector(std::make_unique<DriveLetterVolumeDetector>()),
      unassignedDetector(std::make_unique<UnassignedVolumeDetector>()) {}

std::vector<std::pair<std::string, std::string>> VolumeManager::detectAllVolumes(EventManager *eventManager) {
    auto assignedVolumes   = assignedDetector->detectVolumes(eventManager);
    auto unassignedVolumes = unassignedDetector->detectVolumes(eventManager);

    assignedVolumes.insert(assignedVolumes.end(), unassignedVolumes.begin(), unassignedVolumes.end());
    return assignedVolumes;
}

bool VolumeManager::partitionExists(const std::string &label, EventManager *eventManager) {
    // Try assigned volumes first, then unassigned
    if (assignedDetector->partitionExists(label, eventManager)) {
        return true;
    }
    return unassignedDetector->partitionExists(label, eventManager);
}

bool VolumeManager::executeCommand(std::unique_ptr<IVolumeCommand> command, EventManager *eventManager) {
    if (eventManager) {
        eventManager->notifyLogUpdate("Executing volume command: " + command->getDescription() + "\r\n");
    }
    return command->execute(eventManager);
}

std::string VolumeManager::getPartitionDriveLetter(const std::string &label, EventManager *eventManager) {
    // First check assigned volumes
    auto assignedVolumes = assignedDetector->detectVolumes(eventManager);
    for (const auto &volume : assignedVolumes) {
        if (_stricmp(volume.first.c_str(), label.c_str()) == 0) {
            return volume.second + "\\";
        }
    }

    // If not found, check unassigned volumes and try to assign a drive letter
    auto unassignedVolumes = unassignedDetector->detectVolumes(eventManager);
    for (const auto &volume : unassignedVolumes) {
        if (_stricmp(volume.first.c_str(), label.c_str()) == 0) {
            // Try to assign a drive letter to this volume
            if (executeCommand(std::make_unique<AssignDriveLetterCommand>(volume.second, ""), eventManager)) {
                // Find the assigned letter
                char drives[256];
                GetLogicalDriveStringsA(sizeof(drives), drives);
                char *drive = drives;
                while (*drive) {
                    char volumeName[MAX_PATH];
                    if (GetVolumeNameForVolumeMountPointA(drive, volumeName, sizeof(volumeName))) {
                        std::string volNameCompare = volume.second;
                        std::string guidStr        = volumeName;
                        size_t      minLen =
                            (guidStr.length() < volNameCompare.length() ? guidStr.length() : volNameCompare.length());
                        if (guidStr.substr(0, minLen) == volNameCompare.substr(0, minLen)) {
                            return std::string(drive);
                        }
                    }
                    drive += strlen(drive) + 1;
                }
            }
            break;
        }
    }

    return "";
}

std::string VolumeManager::getPartitionFileSystem(const std::string &label, EventManager *eventManager) {
    // Check assigned volumes first
    auto assignedVolumes = assignedDetector->detectVolumes(eventManager);
    for (const auto &volume : assignedVolumes) {
        if (_stricmp(volume.first.c_str(), label.c_str()) == 0) {
            char  fileSystem[MAX_PATH];
            DWORD serialNumber, maxComponentLen, fileSystemFlags;
            if (GetVolumeInformationA((volume.second + ":\\").c_str(), nullptr, 0, &serialNumber, &maxComponentLen,
                                      &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                return std::string(fileSystem);
            }
        }
    }

    // Check unassigned volumes
    auto unassignedVolumes = unassignedDetector->detectVolumes(eventManager);
    for (const auto &volume : unassignedVolumes) {
        if (_stricmp(volume.first.c_str(), label.c_str()) == 0) {
            char        fileSystem[MAX_PATH];
            DWORD       serialNumber, maxComponentLen, fileSystemFlags;
            std::string volPath = volume.second + "\\";
            if (GetVolumeInformationA(volPath.c_str(), nullptr, 0, &serialNumber, &maxComponentLen, &fileSystemFlags,
                                      fileSystem, sizeof(fileSystem))) {
                return std::string(fileSystem);
            }
        }
    }

    return "";
}

int VolumeManager::countEfiPartitions(EventManager *eventManager) {
    int             count = 0;
    std::set<DWORD> processedSerials; // Track serial numbers to avoid counting same partition twice

    // Check assigned volumes
    auto assignedVolumes = assignedDetector->detectVolumes(eventManager);
    for (const auto &volume : assignedVolumes) {
        if (_stricmp(volume.first.c_str(), EFI_VOLUME_LABEL) == 0 || _stricmp(volume.first.c_str(), "SYSTEM") == 0) {
            char        fileSystem[MAX_PATH];
            DWORD       serialNumber, maxComponentLen, fileSystemFlags;
            std::string drivePath = volume.second + ":\\";
            if (GetVolumeInformationA(drivePath.c_str(), nullptr, 0, &serialNumber, &maxComponentLen, &fileSystemFlags,
                                      fileSystem, sizeof(fileSystem))) {
                // Only count if we haven't seen this serial number before
                if (processedSerials.find(serialNumber) == processedSerials.end()) {
                    processedSerials.insert(serialNumber);
                    count++;
                }
            }
        }
    }

    // Check unassigned volumes
    auto unassignedVolumes = unassignedDetector->detectVolumes(eventManager);
    for (const auto &volume : unassignedVolumes) {
        if (_stricmp(volume.first.c_str(), EFI_VOLUME_LABEL) == 0 || _stricmp(volume.first.c_str(), "SYSTEM") == 0) {
            char        fileSystem[MAX_PATH];
            DWORD       serialNumber, maxComponentLen, fileSystemFlags;
            std::string volPath = volume.second + "\\";
            if (GetVolumeInformationA(volPath.c_str(), nullptr, 0, &serialNumber, &maxComponentLen, &fileSystemFlags,
                                      fileSystem, sizeof(fileSystem))) {
                // Only count if we haven't seen this serial number before
                if (processedSerials.find(serialNumber) == processedSerials.end()) {
                    processedSerials.insert(serialNumber);
                    count++;
                }
            }
        }
    }

    return count;
}

bool VolumeManager::isWindowsUsingEfiPartition([[maybe_unused]] EventManager *eventManager) {
    // Simplified check: Windows is not using the ISOEFI partition for booting
    // The BCD shows bootmgr device is partition=\Device\HarddiskVolume1 (system EFI)
    // not any mounted drive letter like Z: (ISOEFI)
    return false;
}