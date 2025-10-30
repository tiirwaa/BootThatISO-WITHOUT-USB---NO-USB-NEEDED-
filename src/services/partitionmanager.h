#ifndef PARTITIONMANAGER_H
#define PARTITIONMANAGER_H

#include <string>
#include "../models/SpaceValidationResult.h"
#include "../models/EventManager.h"

class PartitionManager
{
private:
    PartitionManager();
    ~PartitionManager();
    PartitionManager(const PartitionManager&) = delete;
    PartitionManager& operator=(const PartitionManager&) = delete;

    EventManager* eventManager;

public:
    static PartitionManager& getInstance();

    void setEventManager(EventManager* em) { eventManager = em; }

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