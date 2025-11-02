#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <functional>

// Forward declaration
class ISOReader;
class IniConfigurator;

/**
 * @brief Handles INI file configuration and integration in mounted WIM images.
 *
 * Responsible for:
 * - Extracting INI files from ISO
 * - Reconfiguring existing INI files in WIM
 * - Processing drive letter replacements
 * - Managing INI file caching and cleanup
 *
 * Follows Single Responsibility Principle for INI file management.
 */
class IniFileProcessor {
public:
    /**
     * @brief Callback for progress updates
     * @param message Progress message
     */
    using ProgressCallback = std::function<void(const std::string &message)>;

    IniFileProcessor(IniConfigurator &iniConfigurator);
    ~IniFileProcessor();

    /**
     * @brief Processes INI files in mounted WIM
     * @param mountDir Path to mounted WIM directory
     * @param isoPath Path to ISO file
     * @param isoReader Pointer to ISOReader instance
     * @param driveLetter Target drive letter for INI configuration (e.g., "X:")
     * @param logFile Log file stream
     * @param progressCallback Optional progress callback
     * @return true if processing successful
     */
    bool processIniFiles(const std::string &mountDir, const std::string &isoPath, ISOReader *isoReader,
                         const std::string &driveLetter, std::ofstream &logFile,
                         ProgressCallback progressCallback = nullptr);

    /**
     * @brief Gets the last error message
     * @return Error message string
     */
    std::string getLastError() const {
        return lastError_;
    }

private:
    IniConfigurator &iniConfigurator_;
    std::string      lastError_;

    /**
     * @brief Reconfigures existing INI files in mounted WIM
     * @param mountDir Path to mounted WIM directory
     * @param driveLetter Target drive letter
     * @param logFile Log file stream
     * @return Number of INI files reconfigured
     */
    int reconfigureExistingIniFiles(const std::string &mountDir, const std::string &driveLetter,
                                    std::ofstream &logFile);

    /**
     * @brief Extracts and processes INI files from ISO
     * @param mountDir Path to mounted WIM directory
     * @param isoPath Path to ISO file
     * @param isoReader Pointer to ISOReader instance
     * @param driveLetter Target drive letter
     * @param logFile Log file stream
     * @return Number of INI files extracted and processed
     */
    int extractAndProcessIniFilesFromIso(const std::string &mountDir, const std::string &isoPath, ISOReader *isoReader,
                                         const std::string &driveLetter, std::ofstream &logFile);

    /**
     * @brief Gets list of INI files from ISO root
     * @param isoPath Path to ISO file
     * @param isoReader Pointer to ISOReader instance
     * @return Vector of INI file names
     */
    std::vector<std::string> getIniFilesFromIso(const std::string &isoPath, ISOReader *isoReader);
};
