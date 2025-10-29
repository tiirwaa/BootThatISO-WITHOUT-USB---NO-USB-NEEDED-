#ifndef PARTITIONMANAGER_H
#define PARTITIONMANAGER_H

#include <string>

struct SpaceValidationResult {
    bool isValid;
    long long availableGB;
    std::string errorMessage;
};

class PartitionManager
{
public:
    PartitionManager();
    ~PartitionManager();

    SpaceValidationResult validateAvailableSpace();
    long long getAvailableSpaceGB();
    bool createPartition(const std::string& format = "FAT32");
    bool partitionExists();
    std::string getPartitionDriveLetter();
    std::string getPartitionFileSystem();
    bool reformatPartition(const std::string& format);
};

#endif // PARTITIONMANAGER_H