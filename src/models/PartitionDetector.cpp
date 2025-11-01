#include "PartitionDetector.h"
#include <algorithm>

std::vector<PartitionInfo> PartitionDetector::findPartitionsByLabels(const std::vector<std::string> &labels) {
    std::vector<PartitionInfo> result;

    for (const auto &partition : findAllPartitions()) {
        for (const auto &label : labels) {
            if (partition.fileSystemLabel == label) {
                result.push_back(partition);
                break;
            }
        }
    }

    return result;
}

std::vector<PartitionInfo> PartitionDetector::findPartitionsBySize(UINT64 minSizeBytes, UINT64 maxSizeBytes) {
    std::vector<PartitionInfo> result;

    for (const auto &partition : findAllPartitions()) {
        if (partition.sizeBytes >= minSizeBytes) {
            if (maxSizeBytes == 0 || partition.sizeBytes <= maxSizeBytes) {
                result.push_back(partition);
            }
        }
    }

    return result;
}

std::vector<PartitionInfo> PartitionDetector::findUnformattedPartitions() {
    std::vector<PartitionInfo> result;

    for (const auto &partition : findAllPartitions()) {
        if (!partition.hasVolume || partition.fileSystemLabel.empty()) {
            result.push_back(partition);
        }
    }

    return result;
}

std::vector<PartitionInfo> PartitionDetector::findAllPartitions() {
    return queryPartitions();
}

PartitionInfo PartitionDetector::findSystemPartition() {
    auto partitions = queryPartitions("DriveLetter = 'C'");

    if (!partitions.empty()) {
        return partitions[0];
    }

    // Return empty partition info if not found
    return PartitionInfo{0, 0, "", "", false, ""};
}

bool PartitionDetector::shouldDeletePartition(const PartitionInfo &partition) {
    // Delete partitions with specific labels
    if (partition.fileSystemLabel == "ISOBOOT" || partition.fileSystemLabel == "ISOEFI") {
        return true;
    }

    // Delete large unformatted partitions (likely leftovers from previous operations)
    // Assuming partitions larger than 100GB that are unformatted are candidates for deletion
    const UINT64 LARGE_PARTITION_THRESHOLD = 100ULL * 1024 * 1024 * 1024; // 100GB
    if (!partition.hasVolume && partition.sizeBytes > LARGE_PARTITION_THRESHOLD) {
        return true;
    }

    return false;
}

std::vector<PartitionInfo> PartitionDetector::queryPartitions(const std::string &whereClause) {
    std::vector<PartitionInfo> result;

    WmiStorageManager wmi;
    if (!wmi.Initialize()) {
        return result;
    }

    std::string query = "SELECT * FROM MSFT_Partition WHERE DiskNumber = 0";
    if (!whereClause.empty()) {
        query += " AND " + whereClause;
    }

    IEnumWbemClassObject *pEnum = wmi.ExecQuery(std::wstring(query.begin(), query.end()).c_str());
    if (!pEnum) {
        return result;
    }

    IWbemClassObject *pPartition = nullptr;
    ULONG             uReturn    = 0;

    while (pEnum->Next(WBEM_INFINITE, 1, &pPartition, &uReturn) == S_OK && uReturn > 0) {
        result.push_back(wmiObjectToPartitionInfo(pPartition));
        pPartition->Release();
    }

    pEnum->Release();
    return result;
}

PartitionInfo PartitionDetector::wmiObjectToPartitionInfo(IWbemClassObject *pPartition) {
    PartitionInfo info = {0, 0, "", "", false, ""};

    // Get partition number
    VARIANT varPartitionNumber;
    VariantInit(&varPartitionNumber);
    if (SUCCEEDED(pPartition->Get(L"PartitionNumber", 0, &varPartitionNumber, NULL, NULL))) {
        info.partitionNumber = varPartitionNumber.uintVal;
        VariantClear(&varPartitionNumber);
    }

    // Get size
    VARIANT varSize;
    VariantInit(&varSize);
    if (SUCCEEDED(pPartition->Get(L"Size", 0, &varSize, NULL, NULL))) {
        info.sizeBytes = varSize.ullVal;
        VariantClear(&varSize);
    }

    // Get drive letter
    VARIANT varDriveLetter;
    VariantInit(&varDriveLetter);
    if (SUCCEEDED(pPartition->Get(L"DriveLetter", 0, &varDriveLetter, NULL, NULL))) {
        if (varDriveLetter.bstrVal) {
            std::wstring ws(varDriveLetter.bstrVal);
            info.driveLetter = std::string(ws.begin(), ws.end());
        }
        VariantClear(&varDriveLetter);
    }

    // Check if partition has volumes and get label
    WmiStorageManager wmi;
    if (wmi.Initialize()) {
        std::string volQuery =
            "ASSOCIATORS OF {MSFT_Partition.DiskNumber=0,PartitionNumber=" + std::to_string(info.partitionNumber) +
            "} WHERE AssocClass=MSFT_PartitionToVolume";

        IEnumWbemClassObject *pVolEnum = wmi.ExecQuery(std::wstring(volQuery.begin(), volQuery.end()).c_str());
        if (pVolEnum) {
            IWbemClassObject *pVolume   = nullptr;
            ULONG             volReturn = 0;

            if (pVolEnum->Next(WBEM_INFINITE, 1, &pVolume, &volReturn) == S_OK && volReturn > 0) {
                info.hasVolume = true;

                VARIANT varLabel;
                VariantInit(&varLabel);
                if (SUCCEEDED(pVolume->Get(L"FileSystemLabel", 0, &varLabel, NULL, NULL))) {
                    if (varLabel.bstrVal) {
                        std::wstring ws(varLabel.bstrVal);
                        info.fileSystemLabel = std::string(ws.begin(), ws.end());
                    }
                    VariantClear(&varLabel);
                }

                pVolume->Release();
            }

            pVolEnum->Release();
        }
    }

    return info;
}