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
    std::string monitoredDrive;

    bool RestartComputer();

    bool isDiskGpt();

public:
    static PartitionManager& getInstance();

    void setEventManager(EventManager* em) { eventManager = em; }
    void setMonitoredDrive(const std::string& driveRoot);
    std::string getMonitoredDrive() const { return monitoredDrive; }

    SpaceValidationResult validateAvailableSpace();
    long long getAvailableSpaceGB(const std::string& driveRoot = std::string());
    bool createPartition(const std::string& format = "FAT32", bool skipIntegrityCheck = false);
    bool partitionExists();
    bool efiPartitionExists();
    std::string getPartitionDriveLetter();
    std::string getEfiPartitionDriveLetter();
    std::string getPartitionFileSystem();
    bool reformatPartition(const std::string& format);
    bool reformatEfiPartition();
    bool recoverSpace();
};

#endif // PARTITIONMANAGER_H
