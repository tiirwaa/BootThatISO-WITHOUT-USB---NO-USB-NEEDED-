#pragma once

#include <string>
#include <memory>
#include "../models/EventManager.h"
#include "../models/VolumeDetectionStrategy.h"
#include "../services/DiskLogger.h"

class VolumeDetector {
public:
    explicit VolumeDetector(EventManager *eventManager);
    ~VolumeDetector();

    bool        partitionExists();
    bool        efiPartitionExists();
    bool        isWindowsUsingEfiPartition(); // Check if Windows is using ISOEFI
    int         countEfiPartitions();         // Count how many ISOEFI partitions exist
    std::string getPartitionDriveLetter();
    std::string getEfiPartitionDriveLetter();
    std::string getPartitionFileSystem();
    int         getEfiPartitionSizeMB(); // Get EFI partition size in MB
    std::string getIsoEfiPartitionDriveLetter();

    // Public method to log disk structure using DiskLogger service
    void logDiskStructure();

private:
    EventManager                  *eventManager_;
    std::unique_ptr<VolumeManager> volumeManager_;
    std::unique_ptr<DiskLogger>    diskLogger_;
};