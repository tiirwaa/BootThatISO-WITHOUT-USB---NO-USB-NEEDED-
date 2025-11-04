#include "VolumeDetector.h"
#include <windows.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <set>
#include "../utils/Utils.h"
#include "../utils/constants.h"

VolumeDetector::VolumeDetector(EventManager *eventManager) : eventManager_(eventManager) {}

VolumeDetector::~VolumeDetector() {}

bool VolumeDetector::partitionExists() {
    // First check drives with assigned letters
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
                if (_stricmp(volumeName, VOLUME_LABEL) == 0) {
                    return true;
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    // Also check unassigned volumes
    char   volumeNameCheck[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeNameCheck, sizeof(volumeNameCheck));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeNameCheck);
            if (len > 0 && volumeNameCheck[len - 1] == '\\') {
                volumeNameCheck[len - 1] = '\0';
            }

            // Get volume information
            char        volName[MAX_PATH] = {0};
            char        fsName[MAX_PATH]  = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volumeNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                if (_stricmp(volName, VOLUME_LABEL) == 0) {
                    FindVolumeClose(hVolume);
                    return true;
                }
            }
        } while (FindNextVolumeA(hVolume, volumeNameCheck, sizeof(volumeNameCheck)));
        FindVolumeClose(hVolume);
    }

    return false;
}

bool VolumeDetector::efiPartitionExists() {
    // First check drives with assigned letters
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
                if (_stricmp(volumeName, EFI_VOLUME_LABEL) == 0) {
                    return true;
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    // Also check unassigned volumes
    char   volumeNameCheck[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeNameCheck, sizeof(volumeNameCheck));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeNameCheck);
            if (len > 0 && volumeNameCheck[len - 1] == '\\') {
                volumeNameCheck[len - 1] = '\0';
            }

            // Get volume information
            char        volName[MAX_PATH] = {0};
            char        fsName[MAX_PATH]  = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volumeNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                if (_stricmp(volName, EFI_VOLUME_LABEL) == 0) {
                    FindVolumeClose(hVolume);
                    return true;
                }
            }
        } while (FindNextVolumeA(hVolume, volumeNameCheck, sizeof(volumeNameCheck)));
        FindVolumeClose(hVolume);
    }

    return false;
}

std::string VolumeDetector::getPartitionDriveLetter() {
    // First try to find existing drive letter
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\" + DEBUG_DRIVES_LOG_FILE).c_str());
    debugLog << "Searching for BOOTTHATISO partition...\n";

    char *drive = drives;
    while (*drive) {
        UINT driveType = GetDriveTypeA(drive);
        debugLog << "Checking drive: " << drive << " (type: " << driveType << ")\n";
        if (driveType == DRIVE_FIXED) {
            char  volumeName[MAX_PATH] = {0};
            char  fileSystem[MAX_PATH] = {0};
            DWORD serialNumber, maxComponentLen, fileSystemFlags;
            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen,
                                      &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                debugLog << "  Volume name: '" << volumeName << "', File system: '" << fileSystem << "'\n";
                if (_stricmp(volumeName, VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << VOLUME_LABEL << " partition at: " << drive << "\n";
                    debugLog.close();
                    return std::string(drive);
                }
            } else {
                debugLog << "  GetVolumeInformation failed for drive: " << drive << ", error: " << GetLastError()
                         << "\n";
            }
        } else {
            debugLog << "  Drive type " << driveType << " (not DRIVE_FIXED)\n";
        }
        drive += strlen(drive) + 1;
    }

    // If not found, try to find unassigned volumes and assign a drive letter
    debugLog << "Partition not found with drive letter, searching for unassigned volumes...\n";

    char   volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            debugLog << "Checking volume: " << volumeName << "\n";

            // Store volume name with trailing backslash for SetVolumeMountPointA
            std::string volumeNameWithSlash = volumeName;

            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }

            // Get volume information
            char        volName[MAX_PATH] = {0};
            char        fsName[MAX_PATH]  = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volumeName) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                debugLog << "  Volume label: '" << volName << "', FS: '" << fsName << "'\n";
                if (_stricmp(volName, VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << VOLUME_LABEL << " volume: " << volumeName << "\n";

                    // Try to assign a drive letter to this volume
                    // Start from Z: and go backwards to avoid conflicts with common drive letters
                    for (char letter = 'Z'; letter >= 'D'; letter--) {
                        std::string driveLetter = std::string(1, letter) + ":";

                        // Check if drive letter is available
                        UINT driveType = GetDriveTypeA(driveLetter.c_str());
                        debugLog << "  Checking drive letter " << driveLetter << ", type: " << driveType << "\n";

                        if (driveType == DRIVE_NO_ROOT_DIR) {
                            debugLog << "  Trying to assign drive letter " << driveLetter << "\n";

                            std::string mountPoint = driveLetter + "\\";
                            // Use volume name WITH trailing backslash for SetVolumeMountPointA
                            if (SetVolumeMountPointA(mountPoint.c_str(), volumeNameWithSlash.c_str())) {
                                debugLog << "  Successfully assigned drive letter " << driveLetter << "\n";
                                FindVolumeClose(hVolume);
                                debugLog.close();
                                return driveLetter + "\\";
                            } else {
                                DWORD error = GetLastError();
                                debugLog << "  Failed to assign drive letter " << driveLetter << ", error: " << error
                                         << "\n";

                                // If the error is ERROR_DIR_NOT_EMPTY, the letter might be in use
                                // Try the next letter
                                if (error != ERROR_DIR_NOT_EMPTY) {
                                    debugLog << "  Non-recoverable error, stopping drive letter assignment\n";
                                    break;
                                }
                            }
                        } else {
                            debugLog << "  Drive letter " << driveLetter << " not available (type: " << driveType
                                     << ")\n";
                        }
                    }

                    debugLog << "  Could not assign any drive letter\n";
                    FindVolumeClose(hVolume);
                    debugLog.close();
                    return "";
                }
            } else {
                debugLog << "  GetVolumeInformation failed for volume " << volumeName << ", error: " << GetLastError()
                         << "\n";
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));
        FindVolumeClose(hVolume);
    } else {
        debugLog << "FindFirstVolumeA failed, error: " << GetLastError() << "\n";
    }

    debugLog << VOLUME_LABEL << " partition not found.\n";
    debugLog << "Available drives found:\n";

    // List all drives for debugging
    char allDrives[256];
    GetLogicalDriveStringsA(sizeof(allDrives), allDrives);
    char *d = allDrives;
    while (*d) {
        UINT dt = GetDriveTypeA(d);
        debugLog << "  " << d << " (type: " << dt << ")\n";
        d += strlen(d) + 1;
    }

    debugLog.close();
    return "";
}

std::string VolumeDetector::getEfiPartitionDriveLetter() {
    // Similar to getPartitionDriveLetter but for EFI_VOLUME_LABEL
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\" + DEBUG_DRIVES_EFI_LOG_FILE).c_str());
    debugLog << "Searching for " << EFI_VOLUME_LABEL << " partition...\n";

    char *drive = drives;
    while (*drive) {
        UINT driveType = GetDriveTypeA(drive);
        debugLog << "Checking drive: " << drive << " (type: " << driveType << ")\n";
        if (driveType == DRIVE_FIXED) {
            char  volumeName[MAX_PATH] = {0};
            char  fileSystem[MAX_PATH] = {0};
            DWORD serialNumber, maxComponentLen, fileSystemFlags;
            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen,
                                      &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                debugLog << "  Volume name: '" << volumeName << "', File system: '" << fileSystem << "'\n";
                if (_stricmp(volumeName, EFI_VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << EFI_VOLUME_LABEL << " partition at: " << drive << "\n";
                    debugLog.close();
                    return std::string(drive);
                }
            } else {
                debugLog << "  GetVolumeInformation failed for drive: " << drive << ", error: " << GetLastError()
                         << "\n";
            }
        } else {
            debugLog << "  Drive type " << driveType << " (not DRIVE_FIXED)\n";
        }
        drive += strlen(drive) + 1;
    }

    // If not found, try to find unassigned volumes and assign a drive letter
    debugLog << "Partition not found with drive letter, searching for unassigned volumes...\n";

    char   volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            debugLog << "Checking volume: " << volumeName << "\n";

            // Store volume name with trailing backslash for SetVolumeMountPointA
            std::string volumeNameWithSlash = volumeName;

            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }

            // Get volume information
            char        volName[MAX_PATH] = {0};
            char        fsName[MAX_PATH]  = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volumeName) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                debugLog << "  Volume label: '" << volName << "', FS: '" << fsName << "'\n";
                if (_stricmp(volName, EFI_VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << EFI_VOLUME_LABEL << " volume: " << volumeName << "\n";

                    // Try to assign a drive letter to this volume
                    for (char letter = 'Z'; letter >= 'D'; letter--) {
                        std::string driveLetter = std::string(1, letter) + ":";

                        // Check if drive letter is available
                        UINT driveType = GetDriveTypeA(driveLetter.c_str());
                        debugLog << "  Checking drive letter " << driveLetter << ", type: " << driveType << "\n";

                        if (driveType == DRIVE_NO_ROOT_DIR) {
                            debugLog << "  Trying to assign drive letter " << driveLetter << "\n";

                            std::string mountPoint = driveLetter + "\\";
                            if (SetVolumeMountPointA(mountPoint.c_str(), volumeNameWithSlash.c_str())) {
                                debugLog << "  Successfully assigned drive letter " << driveLetter << "\n";
                                // Grant full permissions to ensure writability
                                std::string permCmd = "icacls \"" + driveLetter + "\" /grant Everyone:F /T /C";
                                Utils::exec(permCmd.c_str());
                                FindVolumeClose(hVolume);
                                debugLog.close();
                                return driveLetter + "\\";
                            } else {
                                DWORD error = GetLastError();
                                debugLog << "  Failed to assign drive letter " << driveLetter << ", error: " << error
                                         << "\n";

                                if (error != ERROR_DIR_NOT_EMPTY) {
                                    debugLog << "  Non-recoverable error, stopping drive letter assignment\n";
                                    break;
                                }
                            }
                        } else {
                            debugLog << "  Drive letter " << driveLetter << " not available (type: " << driveType
                                     << ")\n";
                        }
                    }

                    debugLog << "  Could not assign any drive letter\n";
                    FindVolumeClose(hVolume);
                    debugLog.close();
                    return "";
                }
            } else {
                debugLog << "  GetVolumeInformation failed for volume " << volumeName << ", error: " << GetLastError()
                         << "\n";
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));
        FindVolumeClose(hVolume);
    } else {
        debugLog << "FindFirstVolumeA failed, error: " << GetLastError() << "\n";
    }

    debugLog << EFI_VOLUME_LABEL << " partition not found.\n";
    debugLog.close();
    return "";
}

std::string VolumeDetector::getPartitionFileSystem() {
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
                if (_stricmp(volumeName, VOLUME_LABEL) == 0) {
                    return std::string(fileSystem);
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    // Also check unassigned volumes
    char   volumeNameCheck[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeNameCheck, sizeof(volumeNameCheck));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeNameCheck);
            if (len > 0 && volumeNameCheck[len - 1] == '\\') {
                volumeNameCheck[len - 1] = '\0';
            }

            // Get volume information
            char        volName[MAX_PATH] = {0};
            char        fsName[MAX_PATH]  = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volumeNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                if (_stricmp(volName, VOLUME_LABEL) == 0) {
                    FindVolumeClose(hVolume);
                    return std::string(fsName);
                }
            }
        } while (FindNextVolumeA(hVolume, volumeNameCheck, sizeof(volumeNameCheck)));
        FindVolumeClose(hVolume);
    }

    return "";
}

int VolumeDetector::getEfiPartitionSizeMB() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\efi_partition_size.log").c_str());

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
    // Count how many ISOEFI partitions exist (detects duplicates)
    int             count = 0;
    std::set<DWORD> processedSerials; // Track serial numbers to avoid counting same partition twice

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\efi_partition_count.log").c_str());

    debugLog << "Counting ISOEFI partitions...\n";

    // Check drives with assigned letters
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
                if (_stricmp(volumeName, EFI_VOLUME_LABEL) == 0) {
                    // Only count if we haven't seen this serial number before
                    if (processedSerials.find(serialNumber) == processedSerials.end()) {
                        processedSerials.insert(serialNumber);
                        count++;
                        debugLog << "Found ISOEFI partition #" << count << " at " << drive << " (Serial: " << std::hex
                                 << serialNumber << std::dec << ")\n";
                    } else {
                        debugLog << "Skipped duplicate ISOEFI at " << drive
                                 << " (already counted with Serial: " << std::hex << serialNumber << std::dec << ")\n";
                    }
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    // Check unassigned volumes (those without drive letters)
    char   volumeNameCheck[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeNameCheck, sizeof(volumeNameCheck));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            size_t len = strlen(volumeNameCheck);
            if (len > 0 && volumeNameCheck[len - 1] == '\\') {
                volumeNameCheck[len - 1] = '\0';
            }

            char        volName[MAX_PATH] = {0};
            char        fsName[MAX_PATH]  = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volumeNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                if (_stricmp(volName, EFI_VOLUME_LABEL) == 0) {
                    // Only count if we haven't seen this serial number before
                    if (processedSerials.find(serial) == processedSerials.end()) {
                        processedSerials.insert(serial);
                        count++;
                        debugLog << "Found unassigned ISOEFI partition #" << count << " (Serial: " << std::hex << serial
                                 << std::dec << ")\n";
                    } else {
                        debugLog << "Skipped duplicate unassigned ISOEFI (already counted with Serial: " << std::hex
                                 << serial << std::dec << ")\n";
                    }
                }
            }
        } while (FindNextVolumeA(hVolume, volumeNameCheck, sizeof(volumeNameCheck)));
        FindVolumeClose(hVolume);
    }

    debugLog << "Total ISOEFI partitions found: " << count << "\n";
    debugLog.close();
    return count;
}

bool VolumeDetector::isWindowsUsingEfiPartition() {
    // Check if the SYSTEM'S ACTUAL EFI partition has the ISOEFI label
    // This prevents false positives when ISOEFI contains copied Windows files but isn't the active EFI
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\windows_efi_detection.log").c_str());

    debugLog << "Checking if Windows SYSTEM EFI partition is labeled as ISOEFI...\n";

    // Use PowerShell to get the actual system EFI partition
    // IMPORTANT: Don't rely on IsActive flag, as it may not be set in UEFI systems
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "efi_check", 0, tempFile);
    std::string psFile  = std::string(tempFile) + ".ps1";
    std::string outFile = std::string(tempFile) + "_out.txt";

    std::ofstream scriptFile(psFile);
    if (!scriptFile) {
        debugLog << "Failed to create PowerShell script\n";
        debugLog.close();
        return false;
    }

    // Get ALL EFI System Partitions (GptType EFI) and check their labels
    // Then check if any ISOEFI partition is in the system's boot path
    scriptFile << "# Get all EFI partitions with ISOEFI label\n";
    scriptFile << "$efiPartitions = Get-Partition | Where-Object { $_.GptType -eq "
                  "'{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' }\n";
    scriptFile << "$isoefiFound = $false\n";
    scriptFile << "foreach ($part in $efiPartitions) {\n";
    scriptFile << "    $vol = Get-Volume | Where-Object { $_.Path -in (Get-Partition -DiskNumber $part.DiskNumber "
                  "-PartitionNumber $part.PartitionNumber).AccessPaths }\n";
    scriptFile << "    if ($vol -and $vol.FileSystemLabel -eq 'ISOEFI') {\n";
    scriptFile << "        $isoefiFound = $true\n";
    scriptFile << "        break\n";
    scriptFile << "    }\n";
    scriptFile << "}\n";
    scriptFile << "\n";
    scriptFile << "# If we found an ISOEFI partition with EFI type, check if it's critical\n";
    scriptFile << "if ($isoefiFound) {\n";
    scriptFile << "    # Try to check if partition is system/critical by attempting to get its boot files\n";
    scriptFile << "    # If ISOEFI contains \\EFI\\Microsoft\\Boot\\bootmgfw.efi, it's likely the system EFI\n";
    scriptFile << "    $vol = Get-Volume | Where-Object { $_.FileSystemLabel -eq 'ISOEFI' }\n";
    scriptFile << "    if ($vol -and $vol.DriveLetter) {\n";
    scriptFile << "        $efiPath = $vol.DriveLetter + ':\\EFI\\Microsoft\\Boot\\bootmgfw.efi'\n";
    scriptFile << "        if (Test-Path $efiPath) {\n";
    scriptFile << "            'SYSTEM_EFI' | Out-File -FilePath '" << outFile << "' -Encoding ASCII\n";
    scriptFile << "        } else {\n";
    scriptFile << "            'NOT_SYSTEM' | Out-File -FilePath '" << outFile << "' -Encoding ASCII\n";
    scriptFile << "        }\n";
    scriptFile << "    } else {\n";
    scriptFile << "        'NO_DRIVE_LETTER' | Out-File -FilePath '" << outFile << "' -Encoding ASCII\n";
    scriptFile << "    }\n";
    scriptFile << "} else {\n";
    scriptFile << "    'NOT_FOUND' | Out-File -FilePath '" << outFile << "' -Encoding ASCII\n";
    scriptFile << "}\n";
    scriptFile.close();

    // Execute PowerShell script
    std::string         cmd = "powershell -ExecutionPolicy Bypass -NoProfile -File \"" + psFile + "\"";
    STARTUPINFOA        si  = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    bool scriptRan = false;
    if (CreateProcessA(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
                       &pi)) {
        WaitForSingleObject(pi.hProcess, 10000); // 10 seconds timeout
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        scriptRan = true;
    }

    DeleteFileA(psFile.c_str());

    if (!scriptRan) {
        debugLog << "Failed to run PowerShell script\n";
        debugLog.close();
        // IMPORTANT: If we can't run the script, assume it's NOT safe to delete
        // Better to be cautious
        return true; // Return true to prevent deletion
    }

    // Read the result
    std::ifstream resultFile(outFile);
    std::string   result;
    if (resultFile) {
        std::getline(resultFile, result);
        // Trim whitespace
        result.erase(0, result.find_first_not_of(" \t\r\n"));
        result.erase(result.find_last_not_of(" \t\r\n") + 1);
        resultFile.close();
    }
    DeleteFileA(outFile.c_str());

    debugLog << "Detection result: '" << result << "'\n";

    bool isUsingIsoefi = (result == "SYSTEM_EFI");

    if (result == "NO_DRIVE_LETTER") {
        debugLog << "WARNING: ISOEFI partition has no drive letter, cannot verify if it's system EFI\n";
        debugLog << "Assuming it IS system EFI for safety\n";
        isUsingIsoefi = true; // Be cautious
    }

    debugLog << "Conclusion: Windows " << (isUsingIsoefi ? "IS" : "IS NOT")
             << " using ISOEFI as system EFI partition\n";
    debugLog.close();

    return isUsingIsoefi;
}
