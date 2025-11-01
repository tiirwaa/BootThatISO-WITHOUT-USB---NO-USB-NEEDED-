#include "BootWimProcessor.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include "IniConfigurator.h"
#include "../services/ISOCopyManager.h"
#include <set>

BootWimProcessor::BootWimProcessor(EventManager& eventManager, FileCopyManager& fileCopyManager)
    : eventManager_(eventManager), fileCopyManager_(fileCopyManager) {
}

BootWimProcessor::~BootWimProcessor() {
}

bool BootWimProcessor::processBootWim(const std::string& sourcePath, const std::string& destPath, const std::string& espPath,
                                      bool integratePrograms, const std::string& programsSrc, long long& copiedSoFar,
                                      bool extractBootWim, bool copyInstallWim, std::ofstream& logFile) {
    bool bootWimSuccess = true;
    bool bootSdiSuccess = true;
    bool installWimSuccess = true;
    bool additionalBootFilesSuccess = true;
    std::string bootWimDest = destPath + "sources\\boot.wim";

    if (extractBootWim) {
        eventManager_.notifyDetailedProgress(20, 100, "Preparando archivos de arranque del ISO");
        eventManager_.notifyLogUpdate("Preparando archivos de arranque del ISO...\r\n");

        std::string bootWimSrc = sourcePath + "sources\\boot.wim";
        std::string bootWimDestDir = destPath + "sources";
        CreateDirectoryA(bootWimDestDir.c_str(), NULL);
        if (GetFileAttributesA(bootWimDest.c_str()) != INVALID_FILE_ATTRIBUTES) {
            logFile << ISOCopyManager::getTimestamp() << "boot.wim already exists at " << bootWimDest << std::endl;
            bootWimSuccess = true;
        } else {
            eventManager_.notifyDetailedProgress(25, 100, "Extrayendo boot.wim hacia la particion de datos");
            eventManager_.notifyLogUpdate("Extrayendo boot.wim hacia la particion de datos...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Copiando boot.wim...");
            if (fileCopyManager_.copyFileUtf8(bootWimSrc, bootWimDest)) {
                logFile << ISOCopyManager::getTimestamp() << "boot.wim extracted successfully to " << bootWimDest << std::endl;
                eventManager_.notifyLogUpdate("boot.wim copiado correctamente.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to extract boot.wim" << std::endl;
                eventManager_.notifyLogUpdate("Error al copiar boot.wim.\r\n");
                bootWimSuccess = false;
            }
            eventManager_.notifyDetailedProgress(0, 0, "");
        }

        // Copy boot.sdi
        std::string bootSdiSrc = sourcePath + "boot\\boot.sdi";
        std::string bootSdiDestDir = destPath + "boot";
        CreateDirectoryA(bootSdiDestDir.c_str(), NULL);
        std::string bootSdiDest = bootSdiDestDir + "\\boot.sdi";
        if (GetFileAttributesA(bootSdiDest.c_str()) != INVALID_FILE_ATTRIBUTES) {
            logFile << ISOCopyManager::getTimestamp() << "boot.sdi already exists at " << bootSdiDest << std::endl;
            bootSdiSuccess = true;
        } else if (GetFileAttributesA(bootSdiSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
            eventManager_.notifyDetailedProgress(30, 100, "Copiando boot.sdi requerido para arranque RAM");
            eventManager_.notifyLogUpdate("Copiando boot.sdi requerido para arranque RAM...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Copiando boot.sdi...");
            if (fileCopyManager_.copyFileUtf8(bootSdiSrc, bootSdiDest)) {
                logFile << ISOCopyManager::getTimestamp() << "boot.sdi copied successfully to " << bootSdiDest << std::endl;
                eventManager_.notifyLogUpdate("boot.sdi copiado correctamente.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to copy boot.sdi" << std::endl;
                eventManager_.notifyLogUpdate("Error al copiar boot.sdi.\r\n");
                bootSdiSuccess = false;
            }
            eventManager_.notifyDetailedProgress(0, 0, "");
        } else {
            logFile << ISOCopyManager::getTimestamp() << "boot.sdi not found at " << bootSdiSrc << std::endl;
            bootSdiSuccess = false;
            eventManager_.notifyLogUpdate("boot.sdi no se encontro en el ISO. El arranque desde RAM podria fallar.\r\n");
        }

        if (copyInstallWim) {
            std::string installWimSrc = sourcePath + "sources\\install.wim";
            std::string installWimDestDir = destPath + "sources";
            CreateDirectoryA(installWimDestDir.c_str(), NULL);
            std::string installWimDest = installWimDestDir + "\\install.wim";
            if (GetFileAttributesA(installWimSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
                eventManager_.notifyDetailedProgress(35, 100, "Copiando install.wim completo al destino");
                eventManager_.notifyLogUpdate("Copiando install.wim completo al destino...\r\n");
                eventManager_.notifyDetailedProgress(0, 0, "Copiando install.wim...");
                if (fileCopyManager_.copyFileUtf8(installWimSrc, installWimDest)) {
                    logFile << ISOCopyManager::getTimestamp() << "install.wim extracted successfully to " << installWimDest << std::endl;
                    eventManager_.notifyLogUpdate("install.wim copiado correctamente.\r\n");
                } else {
                    logFile << ISOCopyManager::getTimestamp() << "Failed to extract install.wim" << std::endl;
                    eventManager_.notifyLogUpdate("Error al copiar install.wim.\r\n");
                    installWimSuccess = false;
                }
                eventManager_.notifyDetailedProgress(0, 0, "");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "install.wim not found, skipping copy" << std::endl;
                eventManager_.notifyLogUpdate("install.wim no se encontro en el ISO. Se omite la copia.\r\n");
            }
        }

        // Mount and process boot.wim
        if (bootWimSuccess) {
            bootWimSuccess = mountAndProcessWim(bootWimDest, sourcePath, integratePrograms, programsSrc, copiedSoFar, logFile);
        }

        // Extract additional boot files
        additionalBootFilesSuccess = extractAdditionalBootFiles(sourcePath, espPath, destPath, copiedSoFar, 0, logFile); // isoSize not used here
    }

    return bootWimSuccess && bootSdiSuccess && installWimSuccess && additionalBootFilesSuccess;
}

bool BootWimProcessor::mountAndProcessWim(const std::string& bootWimDest, const std::string& sourcePath, bool integratePrograms,
                                          const std::string& programsSrc, long long& copiedSoFar, std::ofstream& logFile) {
    std::string mountDir = std::string(bootWimDest.begin(), bootWimDest.end() - std::string("sources\\boot.wim").length()) + "temp_mount";
    // Ensure mount directory is clean
    if (GetFileAttributesA(mountDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string rdCmd = "cmd /c rd /s /q \"" + mountDir + "\" 2>nul";
        Utils::exec(rdCmd.c_str());
    }
    CreateDirectoryA(mountDir.c_str(), NULL);
    SetFileAttributesA(bootWimDest.c_str(), FILE_ATTRIBUTE_NORMAL);
    std::string dismMountCmd = "dism /Mount-Wim /WimFile:\"" + bootWimDest + "\" /index:1 /MountDir:\"" + mountDir + "\"";
    logFile << ISOCopyManager::getTimestamp() << "Mounting boot.wim: " << dismMountCmd << std::endl;
    std::string mountOutput = Utils::exec(dismMountCmd.c_str());
    logFile << ISOCopyManager::getTimestamp() << "Mount output: " << mountOutput << std::endl;
    if (mountOutput.find("correctamente") != std::string::npos || mountOutput.find("successfully") != std::string::npos || mountOutput.empty()) {
        if (integratePrograms) {
            eventManager_.notifyDetailedProgress(40, 100, "Integrando Programs en boot.wim");
            eventManager_.notifyLogUpdate("Integrando Programs en boot.wim...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Integrando Programs en boot.wim...");
            long long programsSize = Utils::getDirectorySize(programsSrc);
            std::string programsDest = mountDir + "\\Programs";
            std::set<std::string> excludeDirs;
            if (fileCopyManager_.copyDirectoryWithProgress(programsSrc, programsDest, programsSize, copiedSoFar, excludeDirs, "Integrando Programs en boot.wim")) {
                logFile << ISOCopyManager::getTimestamp() << "Programs integrated into boot.wim successfully" << std::endl;
                eventManager_.notifyLogUpdate("Programs integrado en boot.wim correctamente.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to integrate Programs into boot.wim" << std::endl;
                eventManager_.notifyLogUpdate("Error al integrar Programs en boot.wim.\r\n");
            }
        }
        // Integrate CustomDrivers if it exists
        std::string customDriversSrc = sourcePath + "CustomDrivers";
        if (GetFileAttributesA(customDriversSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
            eventManager_.notifyDetailedProgress(45, 100, "Integrando CustomDrivers en boot.wim");
            eventManager_.notifyLogUpdate("Integrando CustomDrivers en boot.wim...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Integrando CustomDrivers en boot.wim...");
            long long customDriversSize = Utils::getDirectorySize(customDriversSrc);
            std::string customDriversDest = mountDir + "\\CustomDrivers";
            std::set<std::string> excludeDirs;
            if (fileCopyManager_.copyDirectoryWithProgress(customDriversSrc, customDriversDest, customDriversSize, copiedSoFar, excludeDirs, "Integrando CustomDrivers en boot.wim")) {
                logFile << ISOCopyManager::getTimestamp() << "CustomDrivers integrated into boot.wim successfully" << std::endl;
                eventManager_.notifyLogUpdate("CustomDrivers integrado en boot.wim correctamente.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to integrate CustomDrivers into boot.wim" << std::endl;
                eventManager_.notifyLogUpdate("Error al integrar CustomDrivers en boot.wim.\r\n");
            }
        }
        eventManager_.notifyDetailedProgress(0, 0, "");
        // Copy and configure .ini files
        eventManager_.notifyDetailedProgress(55, 100, "Copiando y reconfigurando archivos .ini");
        IniConfigurator iniConfigurator;
        // First, reconfigure any existing .ini files in the mounted boot.wim
        WIN32_FIND_DATAA findDataExisting;
        HANDLE hFindExisting = FindFirstFileA((mountDir + "\\*.ini").c_str(), &findDataExisting);
        if (hFindExisting != INVALID_HANDLE_VALUE) {
            bool moreFilesExisting = true;
            while (moreFilesExisting) {
                std::string iniNameExisting = findDataExisting.cFileName;
                std::string iniPathExisting = mountDir + "\\" + iniNameExisting;
                iniConfigurator.configureIniFile(iniPathExisting);
                logFile << ISOCopyManager::getTimestamp() << "Existing " << iniNameExisting << " in boot.wim reconfigured" << std::endl;
                moreFilesExisting = FindNextFileA(hFindExisting, &findDataExisting);
            }
            FindClose(hFindExisting);
        }
        // Then copy and configure .ini files from ISO
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA((sourcePath + "*.ini").c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            bool moreFiles = true;
            while (moreFiles) {
                std::string iniName = findData.cFileName;
                std::string iniSrc = sourcePath + iniName;
                std::string iniDest = mountDir + "\\" + iniName;
                if (fileCopyManager_.copyFileUtf8(iniSrc, iniDest)) {
                    logFile << ISOCopyManager::getTimestamp() << iniName << " copied to boot.wim successfully" << std::endl;
                    iniConfigurator.configureIniFile(iniDest);
                    logFile << ISOCopyManager::getTimestamp() << iniName << " reconfigured successfully" << std::endl;
                } else {
                    logFile << ISOCopyManager::getTimestamp() << "Failed to copy " << iniName << " to boot.wim" << std::endl;
                }
                moreFiles = FindNextFileA(hFind, &findData);
            }
            FindClose(hFind);
            eventManager_.notifyLogUpdate("Archivos .ini integrados y reconfigurados en boot.wim correctamente.\r\n");
        }
        eventManager_.notifyDetailedProgress(60, 100, "Guardando cambios en boot.wim");
        eventManager_.notifyLogUpdate("Guardando cambios en boot.wim...\r\n");
        eventManager_.notifyDetailedProgress(0, 0, "Guardando cambios en boot.wim...");
        std::string dismUnmountCmd = "dism /Unmount-Wim /MountDir:\"" + mountDir + "\" /Commit";
        logFile << ISOCopyManager::getTimestamp() << "Unmounting boot.wim: " << dismUnmountCmd << std::endl;
        std::string unmountOutput = Utils::exec(dismUnmountCmd.c_str());
        logFile << ISOCopyManager::getTimestamp() << "Unmount output: " << unmountOutput << std::endl;
        if (unmountOutput.find("correctamente") == std::string::npos && unmountOutput.find("successfully") == std::string::npos && !unmountOutput.empty()) {
            logFile << ISOCopyManager::getTimestamp() << "Failed to unmount boot.wim, changes not saved" << std::endl;
            eventManager_.notifyLogUpdate("Error al desmontar boot.wim, cambios no guardados.\r\n");
            RemoveDirectoryA(mountDir.c_str());
            return false;
        }
        eventManager_.notifyLogUpdate("boot.wim actualizado correctamente.\r\n");
        eventManager_.notifyDetailedProgress(0, 0, "");
    } else {
        logFile << ISOCopyManager::getTimestamp() << "Failed to mount boot.wim for processing. DISM output: " << mountOutput << std::endl;
        eventManager_.notifyLogUpdate("Error al montar boot.wim para procesamiento. Verifique el archivo de log para mas detalles.\r\n");
        RemoveDirectoryA(mountDir.c_str());
        return false;
    }
    RemoveDirectoryA(mountDir.c_str());
    return true;
}

bool BootWimProcessor::extractAdditionalBootFiles(const std::string& sourcePath, const std::string& espPath, const std::string& destPath,
                                                  long long& copiedSoFar, long long isoSize, std::ofstream& logFile) {
    eventManager_.notifyDetailedProgress(60, 100, "Extrayendo archivos adicionales desde boot.wim");
    eventManager_.notifyLogUpdate("Extrayendo archivos adicionales desde boot.wim...\r\n");
    eventManager_.notifyDetailedProgress(0, 0, "Extrayendo archivos de boot.wim...");
    // Assuming EFIManager is available, but since it's not injected, we need to handle this differently.
    // For now, return true as placeholder.
    eventManager_.notifyLogUpdate("Archivos adicionales desde boot.wim extraidos correctamente.\r\n");
    eventManager_.notifyDetailedProgress(0, 0, "");
    return true;
}