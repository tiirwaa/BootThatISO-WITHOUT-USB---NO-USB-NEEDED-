#include "PartitionDetector.h"
#include "../utils/Utils.h"
#include <comutil.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <fstream>

PartitionDetector::PartitionDetector() : pSvc(nullptr), pLoc(nullptr), comInitialized(false) {
}

PartitionDetector::~PartitionDetector() {
    CleanupWMI();
}

bool PartitionDetector::InitializeWMI() {
    if (pSvc) return true; // Already initialized

    // Initialize COM in MTA mode for this thread
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) && hr != S_FALSE) {
        comInitialized = true;
    } else if (hr == S_FALSE) {
        comInitialized = false;
    } else {
        return false;
    }

    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        return false;
    }

    hr = CoCreateInstance(
        CLSID_WbemLocator, 0,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) {
        return false;
    }

    hr = pLoc->ConnectServer(
        _bstr_t(L"ROOT\\Microsoft\\Windows\\Storage"),
        NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hr)) {
        return false;
    }

    hr = CoSetProxyBlanket(
        pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
        NULL, RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    return SUCCEEDED(hr);
}

void PartitionDetector::CleanupWMI() {
    if (pSvc) {
        pSvc->Release();
        pSvc = nullptr;
    }
    if (pLoc) {
        pLoc->Release();
        pLoc = nullptr;
    }
    if (comInitialized) {
        CoUninitialize();
        comInitialized = false;
    }
}

std::vector<PartitionInfo> PartitionDetector::findPartitionsByLabels(const std::vector<std::string>& labels) {
    std::vector<PartitionInfo> foundPartitions;

    if (!InitializeWMI()) {
        return foundPartitions;
    }

    // Get all partitions first
    auto allPartitions = getAllPartitions();

    // Filter by labels
    for (const auto& partition : allPartitions) {
        for (const auto& label : labels) {
            if (_stricmp(partition.fileSystemLabel.c_str(), label.c_str()) == 0) {
                foundPartitions.push_back(partition);
                break; // Found match, no need to check other labels
            }
        }
    }

    return foundPartitions;
}

std::vector<PartitionInfo> PartitionDetector::findPartitionsBySize(UINT64 minSizeBytes, UINT64 maxSizeBytes) {
    std::vector<PartitionInfo> foundPartitions;

    if (!InitializeWMI()) {
        return foundPartitions;
    }

    // Get all partitions first
    auto allPartitions = getAllPartitions();

    // Filter by size range
    for (const auto& partition : allPartitions) {
        if (partition.sizeBytes >= minSizeBytes && partition.sizeBytes <= maxSizeBytes) {
            foundPartitions.push_back(partition);
        }
    }

    return foundPartitions;
}

std::vector<PartitionInfo> PartitionDetector::getAllPartitions() {
    std::vector<PartitionInfo> partitions;

    try {
        if (!InitializeWMI()) {
            return partitions;
        }

        // Query partition data
        BSTR bstrQuery = SysAllocString(L"SELECT * FROM MSFT_Partition");
        BSTR bstrLanguage = SysAllocString(L"WQL");
        IEnumWbemClassObject* pEnumerator = nullptr;

        HRESULT hr = pSvc->ExecQuery(bstrLanguage, bstrQuery,
                                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                    NULL, &pEnumerator);

        SysFreeString(bstrQuery);
        SysFreeString(bstrLanguage);

        if (FAILED(hr) || !pEnumerator) {
            return partitions;
        }

        IWbemClassObject* pPartition = nullptr;
        ULONG uReturn = 0;

        while (pEnumerator->Next(WBEM_INFINITE, 1, &pPartition, &uReturn) == S_OK) {
            PartitionInfo info = {};

            // Get DiskNumber
            VARIANT varDiskNum;
            VariantInit(&varDiskNum);
            if (SUCCEEDED(pPartition->Get(L"DiskNumber", 0, &varDiskNum, NULL, NULL))) {
                if (varDiskNum.vt == VT_UI4) {
                    info.diskNumber = varDiskNum.uintVal;
                } else if (varDiskNum.vt == VT_I4) {
                    info.diskNumber = static_cast<UINT32>(varDiskNum.intVal);
                }
                VariantClear(&varDiskNum);
            }

            // Get PartitionNumber
            VARIANT varPartNum;
            VariantInit(&varPartNum);
            if (SUCCEEDED(pPartition->Get(L"PartitionNumber", 0, &varPartNum, NULL, NULL))) {
                if (varPartNum.vt == VT_UI4) {
                    info.partitionNumber = varPartNum.uintVal;
                } else if (varPartNum.vt == VT_I4) {
                    info.partitionNumber = static_cast<UINT32>(varPartNum.intVal);
                }
                VariantClear(&varPartNum);
            }

            // Get Size
            VARIANT varSize;
            VariantInit(&varSize);
            if (SUCCEEDED(pPartition->Get(L"Size", 0, &varSize, NULL, NULL))) {
                if (varSize.vt == VT_UI8) {
                    info.sizeBytes = varSize.ullVal;
                } else if (varSize.vt == VT_I8) {
                    info.sizeBytes = static_cast<UINT64>(varSize.llVal);
                } else if (varSize.vt == VT_BSTR && varSize.bstrVal && SysStringLen(varSize.bstrVal) > 0) {
                    // Try to parse as string number
                    try {
                        std::string sizeStr = Utils::wstring_to_utf8(varSize.bstrVal);
                        info.sizeBytes = std::stoull(sizeStr);
                    } catch (...) {
                        info.sizeBytes = 0;
                    }
                }
                VariantClear(&varSize);
            }

            // Get Offset
            VARIANT varOffset;
            VariantInit(&varOffset);
            if (SUCCEEDED(pPartition->Get(L"Offset", 0, &varOffset, NULL, NULL))) {
                if (varOffset.vt == VT_UI8) {
                    info.offsetBytes = varOffset.ullVal;
                } else if (varOffset.vt == VT_BSTR && varOffset.bstrVal && SysStringLen(varOffset.bstrVal) > 0) {
                    // Try to parse as string number
                    try {
                        std::string offsetStr = Utils::wstring_to_utf8(varOffset.bstrVal);
                        info.offsetBytes = std::stoull(offsetStr);
                    } catch (...) {
                        info.offsetBytes = 0;
                    }
                }
                VariantClear(&varOffset);
            }

            // Get DriveLetter
            VARIANT varDriveLetter;
            VariantInit(&varDriveLetter);
            if (SUCCEEDED(pPartition->Get(L"DriveLetter", 0, &varDriveLetter, NULL, NULL))) {
                if (varDriveLetter.vt == VT_BSTR && varDriveLetter.bstrVal && SysStringLen(varDriveLetter.bstrVal) > 0) {
                    try {
                        info.driveLetter = Utils::wstring_to_utf8(varDriveLetter.bstrVal);
                    } catch (...) {
                        // Ignore conversion errors
                        info.driveLetter = "";
                    }
                }
                VariantClear(&varDriveLetter);
            }

            // Get GptType
            VARIANT varGptType;
            VariantInit(&varGptType);
            if (SUCCEEDED(pPartition->Get(L"GptType", 0, &varGptType, NULL, NULL))) {
                if (varGptType.vt == VT_BSTR && varGptType.bstrVal && SysStringLen(varGptType.bstrVal) > 0) {
                    try {
                        info.gptType = Utils::wstring_to_utf8(varGptType.bstrVal);
                    } catch (...) {
                        // Ignore conversion errors
                        info.gptType = "";
                    }
                }
                VariantClear(&varGptType);
            }

            // Get IsActive
            VARIANT varIsActive;
            VariantInit(&varIsActive);
            if (SUCCEEDED(pPartition->Get(L"IsActive", 0, &varIsActive, NULL, NULL))) {
                if (varIsActive.vt == VT_BOOL) {
                    info.isActive = varIsActive.boolVal == VARIANT_TRUE;
                }
                VariantClear(&varIsActive);
            }

            // Get ObjectId for matching with volumes
            VARIANT varObjectId;
            VariantInit(&varObjectId);
            std::string partitionObjectId;
            if (SUCCEEDED(pPartition->Get(L"ObjectId", 0, &varObjectId, NULL, NULL))) {
                if (varObjectId.vt == VT_BSTR && varObjectId.bstrVal && SysStringLen(varObjectId.bstrVal) > 0) {
                    try {
                        partitionObjectId = Utils::wstring_to_utf8(varObjectId.bstrVal);
                    } catch (...) {
                        partitionObjectId = "";
                    }
                }
                VariantClear(&varObjectId);
            }

            // Try to get volume label by checking associated volumes
            if (!info.driveLetter.empty()) {
                char volumeName[MAX_PATH] = {0};
                char fileSystem[MAX_PATH] = {0};
                DWORD serialNumber, maxComponentLen, fileSystemFlags;

                std::string drivePath = info.driveLetter + ":\\";
                if (GetVolumeInformationA(drivePath.c_str(), volumeName, sizeof(volumeName),
                                        &serialNumber, &maxComponentLen, &fileSystemFlags,
                                        fileSystem, sizeof(fileSystem))) {
                    info.fileSystemLabel = volumeName;
                }
            } else if (!partitionObjectId.empty()) {
                // For partitions without drive letters, query MSFT_Volume to find matching volume
                info.fileSystemLabel = getVolumeLabelByPartitionObjectId(partitionObjectId);
            }

            partitions.push_back(info);
            pPartition->Release();
        }

        pEnumerator->Release();
    } catch (const std::exception& e) {
        // Log exception but don't crash
        std::ofstream logFile((Utils::getExeDirectory() + "logs\\partition_detector_error.log").c_str(), std::ios::app);
        if (logFile) {
            logFile << "Exception in getAllPartitions: " << e.what() << std::endl;
            logFile.close();
        }
        return partitions;
    } catch (...) {
        // Log unknown exception but don't crash
        std::ofstream logFile((Utils::getExeDirectory() + "logs\\partition_detector_error.log").c_str(), std::ios::app);
        if (logFile) {
            logFile << "Unknown exception in getAllPartitions" << std::endl;
            logFile.close();
        }
        return partitions;
    }

    return partitions;
}

std::string PartitionDetector::getVolumeLabelByPartitionObjectId(const std::string& partitionObjectId) {
    try {
        if (!InitializeWMI()) {
            return "";
        }

        // Query MSFT_Volume to find volumes
        BSTR bstrQuery = SysAllocString(L"SELECT * FROM MSFT_Volume");
        BSTR bstrLanguage = SysAllocString(L"WQL");
        IEnumWbemClassObject* pEnumerator = nullptr;

        HRESULT hr = pSvc->ExecQuery(bstrLanguage, bstrQuery,
                                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                    NULL, &pEnumerator);

        SysFreeString(bstrQuery);
        SysFreeString(bstrLanguage);

        if (FAILED(hr) || !pEnumerator) {
            return "";
        }

        IWbemClassObject* pVolume = nullptr;
        ULONG uReturn = 0;

        while (pEnumerator->Next(WBEM_INFINITE, 1, &pVolume, &uReturn) == S_OK) {
            // Get FileSystemLabel
            VARIANT varLabel;
            VariantInit(&varLabel);
            if (SUCCEEDED(pVolume->Get(L"FileSystemLabel", 0, &varLabel, NULL, NULL))) {
                std::string volumeLabel;
                if (varLabel.vt == VT_BSTR && varLabel.bstrVal && SysStringLen(varLabel.bstrVal) > 0) {
                    try {
                        volumeLabel = Utils::wstring_to_utf8(varLabel.bstrVal);
                    } catch (...) {
                        volumeLabel = "";
                    }
                }
                VariantClear(&varLabel);

                if (!volumeLabel.empty()) {
                    // Check if this volume is associated with our partition
                    // by checking the ObjectId path
                    VARIANT varPath;
                    VariantInit(&varPath);
                    if (SUCCEEDED(pVolume->Get(L"Path", 0, &varPath, NULL, NULL))) {
                        if (varPath.vt == VT_BSTR && varPath.bstrVal && SysStringLen(varPath.bstrVal) > 0) {
                            try {
                                std::string volumePath = Utils::wstring_to_utf8(varPath.bstrVal);
                                // The path should contain the partition ObjectId
                                if (volumePath.find(partitionObjectId) != std::string::npos) {
                                    VariantClear(&varPath);
                                    pVolume->Release();
                                    pEnumerator->Release();
                                    return volumeLabel;
                                }
                            } catch (...) {
                                // Ignore conversion errors
                            }
                        }
                        VariantClear(&varPath);
                    }
                }
            }

            pVolume->Release();
        }

        pEnumerator->Release();
    } catch (...) {
        // Silently ignore errors in volume label lookup
    }

    return "";
}

PartitionInfo PartitionDetector::getPartitionInfo(UINT32 diskNumber, UINT32 partitionNumber) {
    auto allPartitions = getAllPartitions();
    for (const auto& partition : allPartitions) {
        if (partition.diskNumber == diskNumber && partition.partitionNumber == partitionNumber) {
            return partition;
        }
    }
    return PartitionInfo(); // Return empty info if not found
}