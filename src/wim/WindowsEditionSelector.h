#pragma once
#include <string>
#include <vector>
#include <memory>
#include <fstream>

// Forward declarations
class WimMounter;
class ISOReader;
class EventManager;

/**
 * @brief Handles Windows edition selection and injection into boot.wim
 *
 * This class is responsible for:
 * 1. Extracting install.wim/install.esd from Windows ISOs
 * 2. Detecting available Windows editions (indices)
 * 3. Presenting options to the user via EventManager
 * 4. Injecting the selected edition into boot.wim for RAM boot
 *
 * This solves the problem where install.wim/install.esd becomes inaccessible
 * after booting into RAM (X: drive), by embedding the installation files
 * directly into the boot environment.
 */
class WindowsEditionSelector {
public:
    /**
     * @brief Structure containing Windows edition information
     */
    struct WindowsEdition {
        int         index;
        std::string name;
        std::string description;
        long long   size; // Size in bytes
    };

    WindowsEditionSelector(EventManager &eventManager, WimMounter &wimMounter, ISOReader &isoReader);
    ~WindowsEditionSelector();

    /**
     * @brief Detects if the ISO contains install.wim or install.esd
     * @param isoPath Path to the ISO file
     * @return true if install.wim/esd is found
     */
    bool hasInstallImage(const std::string &isoPath);

    /**
     * @brief Gets all available Windows editions from install.wim/esd
     * @param isoPath Path to the ISO file
     * @param tempDir Temporary directory for extraction
     * @param logFile Log file stream
     * @return Vector of available editions
     */
    std::vector<WindowsEdition> getAvailableEditions(const std::string &isoPath, const std::string &tempDir,
                                                     std::ofstream &logFile);

    /**
     * @brief Prompts user to select a Windows edition
     * @param editions Vector of available editions
     * @param logFile Log file stream
     * @return Selected edition index (1-based), or 0 if cancelled
     */
    int promptUserSelection(const std::vector<WindowsEdition> &editions, std::ofstream &logFile);

    /**
     * @brief Prompts user to select multiple Windows editions
     * @param editions Vector of available editions
     * @param selectedIndices Output vector of selected indices (1-based)
     * @param logFile Log file stream
     * @return true if user selected at least one edition, false if cancelled
     */
    bool promptUserMultiSelection(const std::vector<WindowsEdition> &editions, std::vector<int> &selectedIndices,
                                  std::ofstream &logFile);

    /**
     * @brief Injects selected Windows edition into boot.wim by copying install.esd inside Index 2
     * @param isoPath Path to the ISO file
     * @param bootWimPath Path to the boot.wim file
     * @param selectedIndex Index of the edition to inject (1-based)
     * @param tempDir Temporary directory for extraction
     * @param logFile Log file stream
     * @return true if injection successful
     */
    bool injectEditionIntoBootWim(const std::string &isoPath, const std::string &bootWimPath, int selectedIndex,
                                  const std::string &tempDir, std::ofstream &logFile);
    
    /**
     * @brief Exports selected editions to a new install.esd with only chosen indices
     * @param sourceInstallPath Path to the original install.wim/esd
     * @param selectedIndices Vector of indices to export (1-based)
     * @param destInstallPath Path for the new install.esd with selected editions
     * @param logFile Log file stream
     * @return true if export successful
     */
    bool exportSelectedEditions(const std::string &sourceInstallPath, const std::vector<int> &selectedIndices,
                                const std::string &destInstallPath, std::ofstream &logFile);

    /**
     * @brief Main workflow: detect, prompt, and inject Windows edition
     * @param isoPath Path to the ISO file
     * @param bootWimPath Path to the boot.wim file
     * @param tempDir Temporary directory for extraction
     * @param logFile Log file stream
     * @return true if process completed successfully
     */
    bool processWindowsEditions(const std::string &isoPath, const std::string &bootWimPath, const std::string &tempDir,
                                std::ofstream &logFile);

private:
    EventManager &eventManager_;
    WimMounter   &wimMounter_;
    ISOReader    &isoReader_;

    std::string installImagePath_; // Cached path to extracted install.wim/esd
    bool        isEsd_;            // true if install.esd, false if install.wim

    /**
     * @brief Extracts install.wim or install.esd from ISO
     * @param isoPath Path to the ISO file
     * @param destDir Destination directory
     * @param logFile Log file stream
     * @return Path to extracted file, or empty string on failure
     */
    std::string extractInstallImage(const std::string &isoPath, const std::string &destDir, std::ofstream &logFile);

    /**
     * @brief Formats file size for display
     * @param bytes Size in bytes
     * @return Formatted string (e.g., "4.5 GB")
     */
    std::string formatSize(long long bytes);
};
