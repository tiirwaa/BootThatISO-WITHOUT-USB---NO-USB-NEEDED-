#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <atlbase.h>
#include "../../include/models/WmiStorageManager.h"

#pragma comment(lib, "wbemuuid.lib")

// Partition information structure
struct PartitionInfo {
    UINT32 partitionNumber;
    UINT64 sizeBytes;
    std::string driveLetter;
    std::string fileSystemLabel;
    bool hasVolume;
    std::string partitionType;
};

// Partition detection class
class PartitionDetector {
private:
    std::vector<PartitionInfo> queryPartitions(const std::string& whereClause = "");
    PartitionInfo wmiObjectToPartitionInfo(IWbemClassObject* pPartition);

public:
    // Find partitions by volume labels
    std::vector<PartitionInfo> findPartitionsByLabels(const std::vector<std::string>& labels);

    // Find partitions by size range
    std::vector<PartitionInfo> findPartitionsBySize(UINT64 minSizeBytes, UINT64 maxSizeBytes = 0);

    // Find unformatted partitions
    std::vector<PartitionInfo> findUnformattedPartitions();

    // Find all partitions on disk 0
    std::vector<PartitionInfo> findAllPartitions();

    // Find system partition (usually C:)
    PartitionInfo findSystemPartition();

    // Determine if a partition should be deleted during space recovery
    bool shouldDeletePartition(const PartitionInfo& partition);
};