#pragma once
#include <string>
#include <fstream>

/**
 * @brief Handles detection and configuration of PECMD-based PE environments (like Hiren's BootCD PE).
 * 
 * Responsible for:
 * - Detecting PECMD presence (pecmd.exe and pecmd.ini)
 * - Configuring PECMD for RAM boot mode (subst Y: X:\)
 * - Extracting and placing HBCD_PE.ini for LetterSwap compatibility
 * - Preserving PECMD scripts integrity
 * 
 * Follows Single Responsibility Principle for PECMD-specific configuration.
 */
class PecmdConfigurator {
public:
    PecmdConfigurator();
    ~PecmdConfigurator();

    /**
     * @brief Detects if the mounted WIM is a PECMD-based PE
     * @param mountDir Path to mounted WIM directory
     * @return true if PECMD PE detected
     */
    bool isPecmdPE(const std::string &mountDir);

    /**
     * @brief Configures PECMD for RAM boot mode by adding Y: -> X: mapping
     * @param mountDir Path to mounted WIM directory
     * @param logFile Log file stream
     * @return true if configuration successful
     */
    bool configurePecmdForRamBoot(const std::string &mountDir, std::ofstream &logFile);

    /**
     * @brief Extracts HBCD_PE.ini from ISO to boot.wim root for LetterSwap.exe
     * @param isoPath Path to ISO file
     * @param mountDir Path to mounted WIM directory (destination)
     * @param isoReader Pointer to ISOReader instance
     * @param logFile Log file stream
     * @return true if extraction successful (or file not needed)
     */
    bool extractHbcdIni(const std::string &isoPath, const std::string &mountDir, class ISOReader *isoReader,
                        std::ofstream &logFile);

    /**
     * @brief Gets the last error message
     * @return Error message string
     */
    std::string getLastError() const { return lastError_; }

    /**
     * @brief Checks if Programs directory exists in mount
     * @param mountDir Path to mounted WIM directory
     * @return true if Programs directory exists
     */
    bool hasProgramsDirectory(const std::string &mountDir);

private:
    std::string lastError_;

    /**
     * @brief Gets paths to PECMD executables and config files
     * @param mountDir Path to mounted WIM directory
     * @param pecmdExe Output: path to pecmd.exe
     * @param pecmdIni Output: path to pecmd.ini
     */
    void getPecmdPaths(const std::string &mountDir, std::string &pecmdExe, std::string &pecmdIni);

    /**
     * @brief Modifies pecmd.ini to add subst Y: X:\ command
     * @param pecmdIniPath Path to pecmd.ini file
     * @param logFile Log file stream
     * @return true if modification successful
     */
    bool addSubstCommandToPecmdIni(const std::string &pecmdIniPath, std::ofstream &logFile);
};
