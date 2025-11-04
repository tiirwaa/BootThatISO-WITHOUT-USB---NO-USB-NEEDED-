#ifndef PARTITIONMANAGER_H
#define PARTITIONMANAGER_H

#include <string>
#include <memory>
#include "../models/SpaceValidationResult.h"
#include "../models/EventManager.h"
#include "../models/DiskIntegrityChecker.h"
#include "../models/VolumeDetector.h"
#include "../models/SpaceManager.h"
#include "../models/DiskpartExecutor.h"
#include "../models/PartitionReformatter.h"
#include "../models/PartitionCreator.h"

class PartitionManager {
private:
    PartitionManager();
    ~PartitionManager();
    PartitionManager(const PartitionManager &)            = delete;
    PartitionManager &operator=(const PartitionManager &) = delete;

    EventManager *eventManager;
    std::string   monitoredDrive;

    // New dependency-injected components
    std::unique_ptr<DiskIntegrityChecker> diskIntegrityChecker;
    std::unique_ptr<VolumeDetector>       volumeDetector;
    std::unique_ptr<SpaceManager>         spaceManager;
    std::unique_ptr<DiskpartExecutor>     diskpartExecutor;
    std::unique_ptr<PartitionReformatter> partitionReformatter;
    std::unique_ptr<PartitionCreator>     partitionCreator;

    bool RestartComputer();

public:
    static PartitionManager &getInstance();

    void        setEventManager(EventManager *em);
    void        setMonitoredDrive(const std::string &driveRoot);
    std::string getMonitoredDrive() const {
        return monitoredDrive;
    }

    SpaceValidationResult validateAvailableSpace();
    long long             getAvailableSpaceGB(const std::string &driveRoot = std::string());
    bool                  createPartition(const std::string &format = "FAT32", bool skipIntegrityCheck = false);
    bool                  partitionExists();
    bool                  efiPartitionExists();
    std::string           getPartitionDriveLetter();
    std::string           getEfiPartitionDriveLetter();
    std::string           getPartitionFileSystem();
    int                   getEfiPartitionSizeMB(); // Get EFI partition size in MB
    bool                  reformatPartition(const std::string &format);
    bool                  reformatEfiPartition();
    bool                  recoverSpace();
};

#endif // PARTITIONMANAGER_H
