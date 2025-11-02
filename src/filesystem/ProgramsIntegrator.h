#pragma once
#include <string>
#include <fstream>
#include <set>
#include <functional>

// Forward declarations
class FileCopyManager;
class ISOReader;

/**
 * @brief Handles integration of Programs directory into mounted WIM images.
 *
 * Responsible for:
 * - Copying Programs from various sources (disk, ISO)
 * - Managing Programs directory in WIM for RAM boot
 * - Providing progress feedback during integration
 *
 * Follows Single Responsibility Principle for Programs integration.
 */
class ProgramsIntegrator {
public:
    /**
     * @brief Callback for progress updates
     * @param message Progress message
     */
    using ProgressCallback = std::function<void(const std::string &message)>;

    ProgramsIntegrator(FileCopyManager &fileCopyManager);
    ~ProgramsIntegrator();

    /**
     * @brief Integrates Programs directory into mounted WIM
     * @param mountDir Path to mounted WIM directory
     * @param programsSource Primary source directory for Programs
     * @param fallbackProgramsSource Fallback source directory
     * @param isoPath Path to ISO file (for extracting Programs if not found elsewhere)
     * @param isoReader Pointer to ISOReader instance
     * @param copiedSoFar Reference to cumulative bytes copied counter
     * @param logFile Log file stream
     * @param progressCallback Optional progress callback
     * @return true if integration successful
     */
    bool integratePrograms(const std::string &mountDir, const std::string &programsSource,
                           const std::string &fallbackProgramsSource, const std::string &isoPath, ISOReader *isoReader,
                           long long &copiedSoFar, std::ofstream &logFile, ProgressCallback progressCallback = nullptr);

    /**
     * @brief Gets the last error message
     * @return Error message string
     */
    std::string getLastError() const {
        return lastError_;
    }

private:
    FileCopyManager &fileCopyManager_;
    std::string      lastError_;

    /**
     * @brief Tries to copy Programs from a source directory
     * @param sourceDir Source directory
     * @param destDir Destination directory in WIM
     * @param copiedSoFar Reference to cumulative bytes copied counter
     * @param logFile Log file stream
     * @return true if copy successful
     */
    bool tryCopyFromDirectory(const std::string &sourceDir, const std::string &destDir, long long &copiedSoFar,
                              std::ofstream &logFile);

    /**
     * @brief Tries to extract Programs from ISO
     * @param isoPath Path to ISO file
     * @param destDir Destination directory in WIM
     * @param isoReader Pointer to ISOReader instance
     * @param copiedSoFar Reference to cumulative bytes copied counter
     * @param logFile Log file stream
     * @return true if extraction successful
     */
    bool tryExtractFromIso(const std::string &isoPath, const std::string &destDir, ISOReader *isoReader,
                           long long &copiedSoFar, std::ofstream &logFile);
};
