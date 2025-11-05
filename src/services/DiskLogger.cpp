#include "DiskLogger.h"
#include "../utils/Utils.h"
#include "../models/EventManager.h"
#include <windows.h>
#include <vds.h>
#include <Objbase.h>
#include <fstream>
#include <vector>
#include <algorithm>

const CLSID CLSID_VdsLoader = {0x9C38ED61, 0xD565, 0x4728, {0xAE, 0xEE, 0xC8, 0x09, 0x52, 0xF0, 0xEC, 0xDE}};

DiskLogger::DiskLogger(EventManager *eventManager) : eventManager_(eventManager) {}

void DiskLogger::setEventManager(EventManager *eventManager) {
    eventManager_ = eventManager;
}

std::string DiskLogger::getLogDirectory() const {
    return Utils::getExeDirectory() + "logs";
}

std::string DiskLogger::getDefaultLogFilePath() const {
    return getLogDirectory() + "\\disk_structure.log";
}

void DiskLogger::logDiskStructure(const std::string &logFilePath) {
    static_cast<void>(logFilePath); // Suppress unused parameter warning
    std::string actualLogPath = logFilePath.empty() ? getDefaultLogFilePath() : logFilePath;

    // Create log directory if it doesn't exist
    std::string logDir = getLogDirectory();
    CreateDirectoryA(logDir.c_str(), NULL);

    std::ofstream logFile(actualLogPath.c_str(), std::ios::app);
    if (!logFile.is_open()) {
        if (eventManager_) {
            eventManager_->notifyLogUpdate("Error: Cannot open disk structure log file: " + actualLogPath + "\r\n");
        }
        return;
    }

    logFile << "=== Disk Structure Log ===\n";

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        logFile << "Failed to initialize COM: " << hr << "\n";
        logFile.close();
        if (eventManager_) {
            eventManager_->notifyLogUpdate("Failed to initialize COM for disk logging\r\n");
        }
        return;
    }

    IVdsServiceLoader *pLoader = NULL;
    hr = CoCreateInstance(CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER, IID_IVdsServiceLoader, (void **)&pLoader);
    if (FAILED(hr)) {
        logFile << "Failed to create VDS Loader: " << hr << "\n";
        CoUninitialize();
        logFile.close();
        return;
    }

    IVdsService *pService = NULL;
    hr                    = pLoader->LoadService(NULL, &pService);
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
    hr                            = pService->QueryProviders(VDS_QUERY_SOFTWARE_PROVIDERS, &pEnumProvider);
    if (FAILED(hr)) {
        logFile << "Failed to query providers: " << hr << "\n";
        pService->Release();
        CoUninitialize();
        logFile.close();
        return;
    }

    bool      hasProviders     = false;
    IUnknown *pUnknownProvider = NULL;
    while (pEnumProvider->Next(1, &pUnknownProvider, NULL) == S_OK) {
        hasProviders                = true;
        IVdsSwProvider *pSwProvider = NULL;
        hr                          = pUnknownProvider->QueryInterface(IID_IVdsSwProvider, (void **)&pSwProvider);
        pUnknownProvider->Release();
        if (FAILED(hr))
            continue;

        IEnumVdsObject *pEnumPack = NULL;
        hr                        = pSwProvider->QueryPacks(&pEnumPack);
        pSwProvider->Release();
        if (FAILED(hr)) {
            logFile << "Failed to query packs: " << hr << "\n";
            continue;
        }

        bool      hasPacks     = false;
        IUnknown *pUnknownPack = NULL;
        while (pEnumPack->Next(1, &pUnknownPack, NULL) == S_OK) {
            hasPacks        = true;
            IVdsPack *pPack = NULL;
            hr              = pUnknownPack->QueryInterface(IID_IVdsPack, (void **)&pPack);
            pUnknownPack->Release();
            if (FAILED(hr))
                continue;

            logFile << "Pack:\n";

            IEnumVdsObject *pEnumDisk = NULL;
            hr                        = pPack->QueryDisks(&pEnumDisk);
            if (FAILED(hr)) {
                logFile << "Failed to query disks: " << hr << "\n";
                pPack->Release();
                continue;
            }

            bool      hasDisks     = false;
            IUnknown *pUnknownDisk = NULL;
            while (pEnumDisk->Next(1, &pUnknownDisk, NULL) == S_OK) {
                hasDisks        = true;
                IVdsDisk *pDisk = NULL;
                hr              = pUnknownDisk->QueryInterface(IID_IVdsDisk, (void **)&pDisk);
                pUnknownDisk->Release();
                if (FAILED(hr))
                    continue;

                VDS_DISK_PROP diskProp;
                hr = pDisk->GetProperties(&diskProp);
                if (SUCCEEDED(hr)) {
                    logFile << "  Disk: " << Utils::wstring_to_utf8(diskProp.pwszName) << ", Size: " << diskProp.ullSize
                            << " bytes, Status: " << diskProp.status << "\n";

                    // Get partitions using DeviceIoControl
                    std::string diskPath = Utils::wstring_to_utf8(diskProp.pwszName);
                    HANDLE hDisk = CreateFileA(diskPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                               OPEN_EXISTING, 0, NULL);
                    if (hDisk != INVALID_HANDLE_VALUE) {
                        DRIVE_LAYOUT_INFORMATION_EX *layout = NULL;
                        DWORD layoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 100 * sizeof(PARTITION_INFORMATION_EX);
                        layout           = (DRIVE_LAYOUT_INFORMATION_EX *)malloc(layoutSize);
                        if (layout) {
                            DWORD bytesReturned;
                            if (DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout, layoutSize,
                                                &bytesReturned, NULL)) {
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
                                                  part.Gpt.PartitionType.Data1, part.Gpt.PartitionType.Data2,
                                                  part.Gpt.PartitionType.Data3, part.Gpt.PartitionType.Data4[0],
                                                  part.Gpt.PartitionType.Data4[1], part.Gpt.PartitionType.Data4[2],
                                                  part.Gpt.PartitionType.Data4[3], part.Gpt.PartitionType.Data4[4],
                                                  part.Gpt.PartitionType.Data4[5], part.Gpt.PartitionType.Data4[6],
                                                  part.Gpt.PartitionType.Data4[7]);
                                        typeStr = "GPT Type: " + std::string(guidStr);
                                    } else {
                                        typeStr = "Unknown";
                                    }
                                    logFile << "    Partition: Offset " << part.StartingOffset.QuadPart << ", Size "
                                            << part.PartitionLength.QuadPart << " bytes, " << typeStr << "\n";
                                }

                                // Calculate free spaces
                                std::sort(partitions.begin(), partitions.end());
                                ULONGLONG current = 0;
                                for (auto &p : partitions) {
                                    if (p.first > current) {
                                        logFile << "    Free Space: Offset " << current << ", Size "
                                                << (p.first - current) << " bytes\n";
                                    }
                                    current = p.first + p.second;
                                }
                                if (current < diskProp.ullSize) {
                                    logFile << "    Free Space: Offset " << current << ", Size "
                                            << (diskProp.ullSize - current) << " bytes\n";
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

    if (eventManager_) {
        eventManager_->notifyLogUpdate("Disk structure logged to: " + actualLogPath + "\r\n");
    }
}

void DiskLogger::logDiskInfo(int diskIndex, const std::string &logFilePath) {
    // Implementation for logging specific disk info
    // This could be extended to log information about a specific disk
    if (eventManager_) {
        eventManager_->notifyLogUpdate("Logging disk " + std::to_string(diskIndex) + " info\r\n");
    }
}

void DiskLogger::logVolumeInfo(const std::string &logFilePath) {
    std::string actualLogPath = logFilePath.empty() ? getDefaultLogFilePath() : logFilePath;

    std::string logDir = getLogDirectory();
    CreateDirectoryA(logDir.c_str(), NULL);

    std::ofstream logFile(actualLogPath.c_str(), std::ios::app);
    if (!logFile.is_open()) {
        if (eventManager_) {
            eventManager_->notifyLogUpdate("Error: Cannot open volume info log file\r\n");
        }
        return;
    }

    logFile << "=== Volume Information ===\n";

    // Log assigned drive volumes
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    logFile << "Assigned Drive Volumes:\n";
    char *drive = drives;
    while (*drive) {
        if (GetDriveTypeA(drive) == DRIVE_FIXED) {
            char  volumeName[MAX_PATH];
            char  fileSystem[MAX_PATH];
            DWORD serialNumber, maxComponentLen, fileSystemFlags;

            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen,
                                      &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                logFile << "  " << drive << " - Label: '" << volumeName << "', FS: '" << fileSystem
                        << "', Serial: " << std::hex << serialNumber << std::dec << "\n";
            }
        }
        drive += strlen(drive) + 1;
    }

    // Log unassigned volumes
    logFile << "Unassigned Volumes:\n";
    char   volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));

    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Check if assigned
            bool isAssigned = false;
            char drivesCheck[256];
            GetLogicalDriveStringsA(sizeof(drivesCheck), drivesCheck);
            char *drv = drivesCheck;
            while (*drv) {
                char volumeGuid[MAX_PATH];
                if (GetVolumeNameForVolumeMountPointA(drv, volumeGuid, sizeof(volumeGuid))) {
                    std::string volNameCompare = volumeName;
                    std::string guidStr        = volumeGuid;
                    size_t      minLen =
                        (guidStr.length() < volNameCompare.length() ? guidStr.length() : volNameCompare.length());
                    if (guidStr.substr(0, minLen) == volNameCompare.substr(0, minLen)) {
                        isAssigned = true;
                        break;
                    }
                }
                drv += strlen(drv) + 1;
            }

            if (!isAssigned) {
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
                    logFile << "  " << volumeName << " - Label: '" << volName << "', FS: '" << fsName
                            << "', Serial: " << std::hex << serial << std::dec << "\n";
                }
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));

        FindVolumeClose(hVolume);
    }

    logFile << "=== End Volume Information ===\n\n";
    logFile.close();

    if (eventManager_) {
        eventManager_->notifyLogUpdate("Volume information logged to: " + actualLogPath + "\r\n");
    }
}