#pragma once
#include <string>
#include <memory>
#include <fstream>

// Forward declarations
class EventManager;
class FileCopyManager;
class ISOReader;
class WimMounter;
class DriverIntegrator;
class PecmdConfigurator;
class StartnetConfigurator;
class ProgramsIntegrator;
class IniFileProcessor;
class IniConfigurator;
class WindowsEditionSelector;

/**
 * @brief Orchestrates the processing of boot.wim for Windows PE boot environments.
 *
 * This is a high-level coordinator that delegates specific responsibilities to specialized classes:
 * - WimMounter: Mounting/unmounting WIM images
 * - DriverIntegrator: Integrating system and custom drivers
 * - PecmdConfigurator: Configuring PECMD PE (Hiren's BootCD)
 * - StartnetConfigurator: Managing startnet.cmd
 * - ProgramsIntegrator: Integrating Programs directory
 * - IniFileProcessor: Processing INI configuration files
 *
 * Follows Facade Pattern and Single Responsibility Principle.
 * This class coordinates the workflow but delegates implementation details.
 */
class BootWimProcessor {
public:
    BootWimProcessor(EventManager &eventManager, FileCopyManager &fileCopyManager);
    ~BootWimProcessor();

    /**
     * @brief Main entry point for boot.wim processing
     * @param sourcePath Path to ISO file
     * @param destPath Path to destination data partition
     * @param espPath Path to ESP partition
     * @param integratePrograms Whether to integrate Programs directory
     * @param programsSrc Source directory for Programs
     * @param copiedSoFar Reference to cumulative bytes copied counter
     * @param extractBootWim Whether to extract boot.wim from ISO
     * @param copyInstallWim Whether to copy install.wim (currently unused)
     * @param logFile Log file stream
     * @param injectDrivers Whether to inject drivers into boot.wim
     * @return true if processing successful
     */
    bool processBootWim(const std::string &sourcePath, const std::string &destPath, const std::string &espPath,
                        bool integratePrograms, const std::string &programsSrc, long long &copiedSoFar,
                        bool extractBootWim, bool copyInstallWim, std::ofstream &logFile, bool injectDrivers = false);

private:
    EventManager    &eventManager_;
    FileCopyManager &fileCopyManager_;

    // Specialized collaborators (composition over inheritance)
    std::unique_ptr<ISOReader>              isoReader_;
    std::unique_ptr<WimMounter>             wimMounter_;
    std::unique_ptr<DriverIntegrator>       driverIntegrator_;
    std::unique_ptr<PecmdConfigurator>      pecmdConfigurator_;
    std::unique_ptr<StartnetConfigurator>   startnetConfigurator_;
    std::unique_ptr<ProgramsIntegrator>     programsIntegrator_;
    std::unique_ptr<IniFileProcessor>       iniFileProcessor_;
    std::unique_ptr<IniConfigurator>        iniConfigurator_;
    std::unique_ptr<WindowsEditionSelector> windowsEditionSelector_;

    /**
     * @brief Extracts boot.wim and boot.sdi from ISO to data partition
     * @param sourcePath Path to ISO file
     * @param destPath Path to destination data partition
     * @param espDriveLetter Drive letter of ESP partition (e.g., "Y:")
     * @param logFile Log file stream
     * @return true if extraction successful
     */
    bool extractBootFiles(const std::string &sourcePath, const std::string &destPath, const std::string &espDriveLetter,
                          std::ofstream &logFile);

    /**
     * @brief Mounts and processes boot.wim
     * @param bootWimDest Path to boot.wim file
     * @param destPath Path to destination data partition
     * @param sourcePath Path to ISO file
     * @param integratePrograms Whether to integrate Programs directory
     * @param programsSrc Source directory for Programs
     * @param copiedSoFar Reference to cumulative bytes copied counter
     * @param logFile Log file stream
     * @param injectDrivers Whether to inject drivers
     * @return true if processing successful
     */
    bool mountAndProcessWim(const std::string &bootWimDest, const std::string &destPath, const std::string &sourcePath,
                            bool integratePrograms, const std::string &programsSrc, long long &copiedSoFar,
                            std::ofstream &logFile, bool injectDrivers);

    /**
     * @brief Copies boot.wim to ESP partition if not Hiren's PE (size check)
     * @param destPath Path to destination data partition (where boot.wim is)
     * @param espDriveLetter Drive letter of ESP partition
     * @param logFile Log file stream
     * @return true if copy successful or skipped (Hiren's PE)
     */
    bool copyBootWimToEspIfNeeded(const std::string &destPath, const std::string &espDriveLetter,
                                  std::ofstream &logFile);

    /**
     * @brief Extracts additional boot files from ISO
     * @param sourcePath Path to ISO file
     * @param espPath Path to ESP partition
     * @param destPath Path to destination data partition
     * @param copiedSoFar Reference to cumulative bytes copied counter
     * @param isoSize Size of ISO file
     * @param logFile Log file stream
     * @return true if extraction successful
     */
    bool extractAdditionalBootFiles(const std::string &sourcePath, const std::string &espPath,
                                    const std::string &destPath, long long &copiedSoFar, long long isoSize,
                                    std::ofstream &logFile);

    /**
     * @brief Processes install.wim/esd by injecting storage and USB drivers
     * @param installImagePath Path to install.wim or install.esd
     * @param destPath Path to destination data partition
     * @param logFile Log file stream
     * @return true if processing successful
     */
    bool processInstallWim(const std::string &installImagePath, const std::string &destPath, std::ofstream &logFile);
};
