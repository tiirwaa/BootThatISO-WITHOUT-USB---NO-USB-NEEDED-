#ifndef PARTITIONMANAGER_H
#define PARTITIONMANAGER_H

#include <string>
#include "../models/SpaceValidationResult.h"

class PartitionManager
{
private:
    PartitionManager();
    ~PartitionManager();
    PartitionManager(const PartitionManager&) = delete;
    PartitionManager& operator=(const PartitionManager&) = delete;

public:
    static PartitionManager& getInstance();

    SpaceValidationResult validateAvailableSpace();
    long long getAvailableSpaceGB();
    bool createPartition(const std::string& format = "FAT32");
    bool partitionExists();
    std::string getPartitionDriveLetter();
    std::string getEfiPartitionDriveLetter();
    std::string getPartitionFileSystem();
    bool reformatPartition(const std::string& format);
};

#endif // PARTITIONMANAGER_H