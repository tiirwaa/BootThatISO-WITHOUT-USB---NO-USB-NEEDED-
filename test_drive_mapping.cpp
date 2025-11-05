#include <windows.h>
#include <iostream>
#include <string>

int main() {
    std::cout << "Testing drive letter mapping...\n";

    // Get all logical drives
    char drives[256];
    if (GetLogicalDriveStringsA(sizeof(drives), drives)) {
        std::cout << "Available drives:\n";
        char *drive = drives;
        while (*drive) {
            UINT type = GetDriveTypeA(drive);
            std::cout << "  " << drive << " (type: " << type << ")\n";

            // Get volume name for this drive
            char volName[MAX_PATH];
            if (QueryDosDeviceA(drive, volName, sizeof(volName))) {
                std::cout << "    Volume name: " << volName << "\n";
            } else {
                std::cout << "    QueryDosDevice failed: " << GetLastError() << "\n";
            }

            drive += strlen(drive) + 1;
        }
    } else {
        std::cout << "GetLogicalDriveStrings failed: " << GetLastError() << "\n";
    }

    // Now enumerate all volumes
    std::cout << "\nEnumerating all volumes:\n";
    char volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }

            std::string volPath = std::string(volumeName) + "\\";
            std::cout << "Volume: " << volumeName << "\n";

            // Get volume information
            char volumeLabel[MAX_PATH] = {0};
            if (GetVolumeInformationA(volPath.c_str(), volumeLabel, sizeof(volumeLabel), nullptr, nullptr, nullptr, nullptr, 0)) {
                std::cout << "  Label: '" << volumeLabel << "'\n";
            } else {
                std::cout << "  GetVolumeInformation failed: " << GetLastError() << "\n";
            }

            // Try to find drive letter
            char driveLetter = 0;
            char driveStrings[256];
            if (GetLogicalDriveStringsA(sizeof(driveStrings), driveStrings)) {
                char *drive = driveStrings;
                while (*drive) {
                    if (GetDriveTypeA(drive) != DRIVE_NO_ROOT_DIR) {
                        char volNameCheck[MAX_PATH];
                        if (QueryDosDeviceA(drive, volNameCheck, sizeof(volNameCheck))) {
                            std::string volNameStr = std::string("\\\\?\\") + volNameCheck;
                            if (volNameStr == volumeName) {
                                driveLetter = drive[0];
                                std::cout << "  Drive letter: " << driveLetter << ":\n";
                                break;
                            }
                        }
                    }
                    drive += strlen(drive) + 1;
                }
            }

            if (driveLetter == 0) {
                std::cout << "  No drive letter found\n";
            }

        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));

        FindVolumeClose(hVolume);
    } else {
        std::cout << "FindFirstVolume failed: " << GetLastError() << "\n";
    }

    return 0;
}