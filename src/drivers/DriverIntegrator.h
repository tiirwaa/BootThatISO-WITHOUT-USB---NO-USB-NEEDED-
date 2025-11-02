#pragma once
#include <string>
#include <vector>
#include <functional>
#include <fstream>

/**
 * @brief Handles driver integration into mounted WIM images.
 *
 * Responsible for:
 * - Staging and integrating local system drivers (storage, USB, network)
 * - Integrating custom drivers from CustomDrivers folder
 * - Filtering and categorizing drivers by type
 *
 * Follows Single Responsibility Principle and Strategy Pattern for driver selection.
 */
class DriverIntegrator {
public:
    /**
     * @brief Categories of drivers to integrate
     */
    enum class DriverCategory {
        Storage, ///< Storage controllers (NVMe, AHCI, RAID)
        Usb,     ///< USB controllers and hubs
        Network, ///< Network adapters (WiFi, Ethernet, WWAN)
        All      ///< All categories
    };

    /**
     * @brief Callback for progress updates
     * @param message Progress message
     */
    using ProgressCallback = std::function<void(const std::string &message)>;

    /**
     * @brief Callback for logging
     * @param message Log message
     */
    using LogCallback = std::function<void(const std::string &message)>;

    DriverIntegrator();
    ~DriverIntegrator();

    /**
     * @brief Integrates local system drivers into a mounted WIM image
     * @param mountDir Path to mounted WIM directory
     * @param categories Categories of drivers to integrate
     * @param logFile Log file stream
     * @param progressCallback Optional progress callback
     * @return true if integration successful, false otherwise
     */
    bool integrateSystemDrivers(const std::string &mountDir, DriverCategory categories, std::ofstream &logFile,
                                ProgressCallback progressCallback = nullptr);

    /**
     * @brief Integrates custom drivers from a source directory into mounted WIM
     * @param mountDir Path to mounted WIM directory
     * @param customDriversSource Source directory with custom drivers
     * @param logFile Log file stream
     * @param progressCallback Optional progress callback
     * @return true if integration successful, false otherwise
     */
    bool integrateCustomDrivers(const std::string &mountDir, const std::string &customDriversSource,
                                std::ofstream &logFile, ProgressCallback progressCallback = nullptr);

    /**
     * @brief Gets the last error message
     * @return Error message string
     */
    std::string getLastError() const {
        return lastError_;
    }

    /**
     * @brief Gets statistics about integrated drivers
     * @return String with integration statistics
     */
    std::string getIntegrationStats() const;

private:
    std::string lastError_;
    int         stagedStorage_;
    int         stagedUsb_;
    int         stagedNetwork_;
    int         stagedCustom_;

    /**
     * @brief Stages system drivers to a temporary directory
     * @param stagingDir Temporary directory for staging
     * @param categories Categories of drivers to stage
     * @param logFile Log file stream
     * @return true if staging successful
     */
    bool stageSystemDrivers(const std::string &stagingDir, DriverCategory categories, std::ofstream &logFile);

    /**
     * @brief Adds staged drivers to WIM image using DISM
     * @param mountDir Path to mounted WIM directory
     * @param stagingDir Directory containing staged drivers
     * @param logFile Log file stream
     * @param isCustomDrivers If true, these are custom drivers (for logging)
     * @return true if DISM add-driver successful
     */
    bool addDriversToImage(const std::string &mountDir, const std::string &stagingDir, std::ofstream &logFile,
                           bool isCustomDrivers = false);

    /**
     * @brief Checks if a driver directory matches storage criteria
     * @param dirNameLower Lowercase directory name
     * @return true if it's a storage driver
     */
    bool isStorageDriver(const std::string &dirNameLower);

    /**
     * @brief Checks if a driver directory matches USB criteria
     * @param dirNameLower Lowercase directory name
     * @return true if it's a USB driver
     */
    bool isUsbDriver(const std::string &dirNameLower);

    /**
     * @brief Checks if a driver directory matches network criteria
     * @param dirNameLower Lowercase directory name
     * @param dirPath Full path to check INF files if needed
     * @return true if it's a network driver
     */
    bool isNetworkDriver(const std::string &dirNameLower, const std::string &dirPath);

    /**
     * @brief Checks if directory contains network INF files
     * @param dirPath Directory to check
     * @return true if network INF found
     */
    bool directoryContainsNetworkInf(const std::string &dirPath);

    /**
     * @brief Executes DISM command and captures output
     * @param command DISM command to execute
     * @param output Reference to store command output
     * @return Exit code from DISM
     */
    int executeDism(const std::string &command, std::string &output);
};
