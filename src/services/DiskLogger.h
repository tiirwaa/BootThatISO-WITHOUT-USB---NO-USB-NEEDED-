#pragma once
#include <string>
#include <memory>

// Forward declarations
class EventManager;

/**
 * @brief Service for logging disk structure information
 *
 * Service Layer Pattern: Provides disk logging functionality
 */
class DiskLogger {
public:
    explicit DiskLogger(EventManager *eventManager = nullptr);
    ~DiskLogger() = default;

    /**
     * @brief Log complete disk structure to file
     * @param logFilePath Path to log file (optional, uses default if empty)
     */
    void logDiskStructure(const std::string &logFilePath = "");

    /**
     * @brief Log information about a specific disk
     * @param diskIndex Disk index (0-based)
     * @param logFilePath Path to log file
     */
    void logDiskInfo(int diskIndex, const std::string &logFilePath);

    /**
     * @brief Log volume information
     * @param logFilePath Path to log file
     */
    void logVolumeInfo(const std::string &logFilePath);

    /**
     * @brief Set event manager for notifications
     * @param eventManager Event manager instance
     */
    void setEventManager(EventManager *eventManager);

private:
    EventManager *eventManager_;

    /**
     * @brief Get default log directory
     * @return Log directory path
     */
    std::string getLogDirectory() const;

    /**
     * @brief Get default log file path
     * @return Log file path
     */
    std::string getDefaultLogFilePath() const;
};