#pragma once

#include <string>
#include "../models/EventManager.h"

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

private:
    EventManager *eventManager_;
    void logDiskStructure();
};