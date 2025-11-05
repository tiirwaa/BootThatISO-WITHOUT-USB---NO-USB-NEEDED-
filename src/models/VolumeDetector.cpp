#include "VolumeDetector.h"
#include <windows.h>
#include <vds.h>
#include <Objbase.h>
#pragma comment(lib, "ole32.lib")
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

const CLSID CLSID_VdsLoader = {0x9C38ED61, 0xD565, 0x4728, {0xAE, 0xEE, 0xC8, 0x09, 0x52, 0xF0, 0xEC, 0xDE}};

VolumeDetector::VolumeDetector(EventManager *eventManager) : eventManager_(eventManager) {
    logDiskStructure();
}

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
    // Return true if any EFI partition exists (ISOEFI or SYSTEM)
    return !getEfiPartitionDriveLetter().empty();
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
                    debugLog << "  Found EFI partition at: " << drive << "\n";
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
                    debugLog << "  Found EFI volume: " << volumeName << "\n";

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
    debugLog << "Searching for EFI partition...\n";

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
                if (_stricmp(volumeName, EFI_VOLUME_LABEL) == 0 || _stricmp(volumeName, "SYSTEM") == 0) {
                    debugLog << "  Found EFI partition at: " << drive << "\n";
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
                if (_stricmp(volName, EFI_VOLUME_LABEL) == 0 || _stricmp(volName, "SYSTEM") == 0) {
                    debugLog << "  Found EFI volume: " << volumeName << "\n";

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

    debugLog << "EFI partition not found.\n";
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
    // Count how many ISOEFI partitions exist (detects duplicates)
    int             count = 0;
    std::set<DWORD> processedSerials; // Track serial numbers to avoid counting same partition twice

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\" + EFI_PARTITION_COUNT_LOG_FILE).c_str());

    debugLog << "Counting EFI partitions...\n";

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
                if (_stricmp(volumeName, EFI_VOLUME_LABEL) == 0 || _stricmp(volumeName, "SYSTEM") == 0) {
                    // Only count if we haven't seen this serial number before
                    if (processedSerials.find(serialNumber) == processedSerials.end()) {
                        processedSerials.insert(serialNumber);
                        count++;
                        debugLog << "Found EFI partition #" << count << " at " << drive << " (Serial: " << std::hex
                                 << serialNumber << std::dec << ")\n";
                    } else {
                        debugLog << "Skipped duplicate EFI at " << drive
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
                if (_stricmp(volName, EFI_VOLUME_LABEL) == 0 || _stricmp(volName, "SYSTEM") == 0) {
                    // Only count if we haven't seen this serial number before
                    if (processedSerials.find(serial) == processedSerials.end()) {
                        processedSerials.insert(serial);
                        count++;
                        debugLog << "Found unassigned EFI partition #" << count << " (Serial: " << std::hex << serial
                                 << std::dec << ")\n";
                    } else {
                        debugLog << "Skipped duplicate unassigned EFI (already counted with Serial: " << std::hex
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
    // Check if the volume containing bootmgfw.efi has ISOEFI label using Windows Storage API
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\" + WINDOWS_EFI_DETECTION_LOG_FILE).c_str());

    debugLog << "Checking if Windows SYSTEM EFI partition is labeled as ISOEFI using Windows Storage API...\n";

    std::string systemEfiLabel;
    char        volumeName[MAX_PATH];
    HANDLE      hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));

    if (hVolume == INVALID_HANDLE_VALUE) {
        debugLog << "FindFirstVolumeA failed, error: " << GetLastError() << "\n";
        debugLog.close();
        return false;
    }

    do {
        // Remove trailing backslash
        size_t len = strlen(volumeName);
        if (len > 0 && volumeName[len - 1] == '\\') {
            volumeName[len - 1] = '\0';
        }

        debugLog << "Checking volume: " << volumeName << "\n";

        // Try to mount temporarily to check for bootmgfw.efi
        char mountPoint[] = "Z:\\";
        if (SetVolumeMountPointA(mountPoint, volumeName)) {
            std::string bootmgfwPath = std::string(mountPoint) + "EFI\\Microsoft\\Boot\\bootmgfw.efi";
            if (GetFileAttributesA(bootmgfwPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                // Get volume label
                char volumeLabel[MAX_PATH];
                if (GetVolumeInformationA(mountPoint, volumeLabel, sizeof(volumeLabel), nullptr, nullptr, nullptr,
                                          nullptr, 0)) {
                    systemEfiLabel = volumeLabel;
                    debugLog << "Found system EFI volume with label: '" << systemEfiLabel << "'\n";
                }
            }
            DeleteVolumeMountPointA(mountPoint);
        }
    } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));

    FindVolumeClose(hVolume);

    debugLog << "Detection result: '" << systemEfiLabel << "'\n";

    bool isUsingIsoefi = (systemEfiLabel == "ISOEFI");

    debugLog << "Conclusion: Windows " << (isUsingIsoefi ? "IS" : "IS NOT")
             << " using ISOEFI as system EFI partition\n";
    debugLog.close();

    return isUsingIsoefi;
}

void VolumeDetector::logDiskStructure() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\disk_structure.log").c_str(), std::ios::app);
    logFile << "=== Disk Structure Log ===\n";

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        logFile << "Failed to initialize COM: " << hr << "\n";
        logFile.close();
        return;
    }

    IVdsServiceLoader *pLoader = NULL;
    hr = CoCreateInstance(CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER, IID_IVdsServiceLoader, (void**)&pLoader);
    if (FAILED(hr)) {
        logFile << "Failed to create VDS Loader: " << hr << "\n";
        CoUninitialize();
        logFile.close();
        return;
    }

    IVdsService *pService = NULL;
    hr = pLoader->LoadService(NULL, &pService);
    pLoader->Release();
    if (FAILED(hr)) {
        logFile << "Failed to load VDS Service: " << hr << "\n";
        CoUninitialize();
        logFile.close();
        return;
    }

    hr = pService->WaitForServiceReady();
    if (FAILED(hr)) {
        logFile << "VDS service not ready: " << hr << "\n";
        pService->Release();
        CoUninitialize();
        logFile.close();
        return;
    }

    IEnumVdsObject *pEnumProvider = NULL;
    hr = pService->QueryProviders(VDS_QUERY_SOFTWARE_PROVIDERS, &pEnumProvider);
    if (FAILED(hr)) {
        logFile << "Failed to query providers: " << hr << "\n";
        pService->Release();
        CoUninitialize();
        logFile.close();
        return;
    }

    bool hasProviders = false;
    IUnknown *pUnknownProvider = NULL;
    while (pEnumProvider->Next(1, &pUnknownProvider, NULL) == S_OK) {
        hasProviders = true;
        IVdsSwProvider *pSwProvider = NULL;
        hr = pUnknownProvider->QueryInterface(IID_IVdsSwProvider, (void**)&pSwProvider);
        pUnknownProvider->Release();
        if (FAILED(hr)) continue;

        IEnumVdsObject *pEnumPack = NULL;
        hr = pSwProvider->QueryPacks(&pEnumPack);
        pSwProvider->Release();
        if (FAILED(hr)) {
            logFile << "Failed to query packs: " << hr << "\n";
            continue;
        }

        bool hasPacks = false;
        IUnknown *pUnknownPack = NULL;
        while (pEnumPack->Next(1, &pUnknownPack, NULL) == S_OK) {
            hasPacks = true;
            IVdsPack *pPack = NULL;
            hr = pUnknownPack->QueryInterface(IID_IVdsPack, (void**)&pPack);
            pUnknownPack->Release();
            if (FAILED(hr)) continue;

            logFile << "Pack:\n";

            IEnumVdsObject *pEnumDisk = NULL;
            hr = pPack->QueryDisks(&pEnumDisk);
            if (FAILED(hr)) {
                logFile << "Failed to query disks: " << hr << "\n";
                pPack->Release();
                continue;
            }

            bool hasDisks = false;
            IUnknown *pUnknownDisk = NULL;
            while (pEnumDisk->Next(1, &pUnknownDisk, NULL) == S_OK) {
                hasDisks = true;
                IVdsDisk *pDisk = NULL;
                hr = pUnknownDisk->QueryInterface(IID_IVdsDisk, (void**)&pDisk);
                pUnknownDisk->Release();
                if (FAILED(hr)) continue;

                VDS_DISK_PROP diskProp;
                hr = pDisk->GetProperties(&diskProp);
                if (SUCCEEDED(hr)) {
                    logFile << "  Disk: " << Utils::wstring_to_utf8(diskProp.pwszName) << ", Size: " << diskProp.ullSize << " bytes, Status: " << diskProp.status << "\n";

                    // Get partitions using DeviceIoControl
                    std::string diskPath = Utils::wstring_to_utf8(diskProp.pwszName);
                    HANDLE hDisk = CreateFileA(diskPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                    if (hDisk != INVALID_HANDLE_VALUE) {
                        DRIVE_LAYOUT_INFORMATION_EX *layout = NULL;
                        DWORD layoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 100 * sizeof(PARTITION_INFORMATION_EX);
                        layout = (DRIVE_LAYOUT_INFORMATION_EX*)malloc(layoutSize);
                        if (layout) {
                            DWORD bytesReturned;
                            if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout, layoutSize, &bytesReturned, NULL)) {
                                std::vector<std::pair<ULONGLONG, ULONGLONG>> partitions;
                                for (DWORD i = 0; i < layout->PartitionCount; i++) {
                                    PARTITION_INFORMATION_EX &part = layout->PartitionEntry[i];
                                    partitions.push_back({part.StartingOffset.QuadPart, part.PartitionLength.QuadPart});
                                    std::string typeStr;
                                    if (part.PartitionStyle == PARTITION_STYLE_MBR) {
                                        typeStr = "MBR Type: " + std::to_string(part.Mbr.PartitionType);
                                    } else if (part.PartitionStyle == PARTITION_STYLE_GPT) {
                                        char guidStr[37];
                                        sprintf_s(guidStr, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                                                  part.Gpt.PartitionType.Data1, part.Gpt.PartitionType.Data2, part.Gpt.PartitionType.Data3,
                                                  part.Gpt.PartitionType.Data4[0], part.Gpt.PartitionType.Data4[1], part.Gpt.PartitionType.Data4[2],
                                                  part.Gpt.PartitionType.Data4[3], part.Gpt.PartitionType.Data4[4], part.Gpt.PartitionType.Data4[5],
                                                  part.Gpt.PartitionType.Data4[6], part.Gpt.PartitionType.Data4[7]);
                                        typeStr = "GPT Type: " + std::string(guidStr);
                                    } else {
                                        typeStr = "Unknown";
                                    }
                                    logFile << "    Partition: Offset " << part.StartingOffset.QuadPart << ", Size " << part.PartitionLength.QuadPart << " bytes, " << typeStr << "\n";
                                }
                                // Calculate free spaces
                                std::sort(partitions.begin(), partitions.end());
                                ULONGLONG current = 0;
                                for (auto &p : partitions) {
                                    if (p.first > current) {
                                        logFile << "    Free Space: Offset " << current << ", Size " << (p.first - current) << " bytes\n";
                                    }
                                    current = p.first + p.second;
                                }
                                if (current < diskProp.ullSize) {
                                    logFile << "    Free Space: Offset " << current << ", Size " << (diskProp.ullSize - current) << " bytes\n";
                                }
                            } else {
                                logFile << "DeviceIoControl failed: " << GetLastError() << "\n";
                            }
                            free(layout);
                        }
                        CloseHandle(hDisk);
                    } else {
                        logFile << "CreateFileA failed for disk: " << GetLastError() << "\n";
                    }
                } else {
                    logFile << "Failed to get disk properties: " << hr << "\n";
                }

                pDisk->Release();
            }
            if (!hasDisks) {
                logFile << "No disks found in pack\n";
            }
            pEnumDisk->Release();
            pPack->Release();
        }
        if (!hasPacks) {
            logFile << "No packs found for provider\n";
        }
        pEnumPack->Release();
    }
    if (!hasProviders) {
        logFile << "No providers found\n";
    }
    pEnumProvider->Release();
    pService->Release();
    CoUninitialize();
    logFile << "=== End Disk Structure Log ===\n\n";
    logFile.close();
}
