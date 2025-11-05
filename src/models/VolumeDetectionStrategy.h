#pragma once
#include <string>
#include <vector>
#include <memory>

// Forward declarations
class EventManager;

/**
 * @brief Interface for volume detection strategies
 *
 * Strategy Pattern: Defines different algorithms for detecting volumes
 */
class IVolumeDetectionStrategy {
public:
    virtual ~IVolumeDetectionStrategy() = default;

    /**
     * @brief Detect volumes using specific strategy
     * @param eventManager Event manager for logging
     * @return Vector of detected volume information (label, drive letter/path)
     */
    virtual std::vector<std::pair<std::string, std::string>> detectVolumes(EventManager *eventManager) = 0;

    /**
     * @brief Check if partition with specific label exists
     * @param label Volume label to search for
     * @param eventManager Event manager for logging
     * @return true if partition exists
     */
    virtual bool partitionExists(const std::string &label, EventManager *eventManager) = 0;
};

/**
 * @brief Strategy for detecting volumes with assigned drive letters
 */
class DriveLetterVolumeDetector : public IVolumeDetectionStrategy {
public:
    std::vector<std::pair<std::string, std::string>> detectVolumes(EventManager *eventManager) override;
    bool partitionExists(const std::string &label, EventManager *eventManager) override;
};

/**
 * @brief Strategy for detecting unassigned volumes (without drive letters)
 */
class UnassignedVolumeDetector : public IVolumeDetectionStrategy {
public:
    std::vector<std::pair<std::string, std::string>> detectVolumes(EventManager *eventManager) override;
    bool partitionExists(const std::string &label, EventManager *eventManager) override;
};

/**
 * @brief Command interface for volume operations
 *
 * Command Pattern: Encapsulates volume operations
 */
class IVolumeCommand {
public:
    virtual ~IVolumeCommand() = default;

    /**
     * @brief Execute the volume command
     * @param eventManager Event manager for logging
     * @return true if successful
     */
    virtual bool execute(EventManager *eventManager) = 0;

    /**
     * @brief Get command description
     * @return Description string
     */
    virtual std::string getDescription() const = 0;
};

/**
 * @brief Command to assign drive letter to volume
 */
class AssignDriveLetterCommand : public IVolumeCommand {
private:
    std::string volumePath;
    std::string preferredLetter;

public:
    AssignDriveLetterCommand(const std::string &volumePath, const std::string &preferredLetter = "");

    bool        execute(EventManager *eventManager) override;
    std::string getDescription() const override;
};

/**
 * @brief Command to remove drive letter from volume
 */
class RemoveDriveLetterCommand : public IVolumeCommand {
private:
    std::string driveLetter;

public:
    explicit RemoveDriveLetterCommand(const std::string &driveLetter);

    bool        execute(EventManager *eventManager) override;
    std::string getDescription() const override;
};

/**
 * @brief Service for managing volume operations
 *
 * Facade Pattern: Provides unified interface for volume operations
 */
class VolumeManager {
private:
    std::unique_ptr<IVolumeDetectionStrategy> assignedDetector;
    std::unique_ptr<IVolumeDetectionStrategy> unassignedDetector;

public:
    VolumeManager();
    ~VolumeManager() = default;

    /**
     * @brief Detect all volumes using both strategies
     * @param eventManager Event manager for logging
     * @return Combined list of all detected volumes
     */
    std::vector<std::pair<std::string, std::string>> detectAllVolumes(EventManager *eventManager);

    /**
     * @brief Check if partition exists using appropriate strategy
     * @param label Volume label to search for
     * @param eventManager Event manager for logging
     * @return true if partition exists
     */
    bool partitionExists(const std::string &label, EventManager *eventManager);

    /**
     * @brief Execute volume command
     * @param command Command to execute
     * @param eventManager Event manager for logging
     * @return true if successful
     */
    // Get drive letter for a partition with specific label, assigning one if necessary
    std::string getPartitionDriveLetter(const std::string &label, EventManager *eventManager);

    // Get filesystem type for a partition with specific label
    std::string getPartitionFileSystem(const std::string &label, EventManager *eventManager);

    // Count EFI partitions (ISOEFI or SYSTEM labeled)
    int countEfiPartitions(EventManager *eventManager);

    // Check if Windows is using EFI partition
    bool isWindowsUsingEfiPartition(EventManager *eventManager);

    /**
     * @brief Execute volume command
     * @param command Command to execute
     * @param eventManager Event manager for logging
     * @return true if successful
     */
    bool executeCommand(std::unique_ptr<IVolumeCommand> command, EventManager *eventManager);
};