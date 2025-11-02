#pragma once
#include <string>
#include <fstream>

/**
 * @brief Handles startnet.cmd configuration for WinPE boot environments.
 *
 * Responsible for:
 * - Detecting existing startnet.cmd
 * - Creating minimal startnet.cmd for standard WinPE
 * - Preserving custom startnet.cmd scripts
 * - Ensuring proper WinPE initialization
 *
 * Follows Single Responsibility Principle for startnet.cmd management.
 */
class StartnetConfigurator {
public:
    StartnetConfigurator();
    ~StartnetConfigurator();

    /**
     * @brief Configures startnet.cmd in the mounted WIM
     * @param mountDir Path to mounted WIM directory
     * @param logFile Log file stream
     * @return true if configuration successful
     */
    bool configureStartnet(const std::string &mountDir, std::ofstream &logFile);

    /**
     * @brief Checks if startnet.cmd exists in the mounted WIM
     * @param mountDir Path to mounted WIM directory
     * @return true if startnet.cmd exists
     */
    bool startnetExists(const std::string &mountDir);

    /**
     * @brief Creates a minimal startnet.cmd for standard WinPE
     * @param mountDir Path to mounted WIM directory
     * @param logFile Log file stream
     * @return true if creation successful
     */
    bool createMinimalStartnet(const std::string &mountDir, std::ofstream &logFile);

    /**
     * @brief Gets the last error message
     * @return Error message string
     */
    std::string getLastError() const {
        return lastError_;
    }

private:
    std::string lastError_;

    /**
     * @brief Gets the path to startnet.cmd
     * @param mountDir Path to mounted WIM directory
     * @return Full path to startnet.cmd
     */
    std::string getStartnetPath(const std::string &mountDir);

    /**
     * @brief Ensures Windows\System32 directory exists
     * @param mountDir Path to mounted WIM directory
     * @return true if directories exist or were created
     */
    bool ensureSystem32Exists(const std::string &mountDir);
};
