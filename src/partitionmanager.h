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
    bool createPartition();
    bool partitionExists();
};

#endif // PARTITIONMANAGER_H