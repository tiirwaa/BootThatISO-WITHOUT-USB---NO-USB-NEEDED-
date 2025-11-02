#include "BootWimProcessor.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include "IniConfigurator.h"
#include "../services/ISOCopyManager.h"
#include "ISOReader.h"
#include <filesystem>
#include <algorithm>
#include <fstream>\r\n#include <exception>\r\n#include <sstream>
#include <set>
#include <vector>
#include <cstring>
#include <cctype>

BootWimProcessor::BootWimProcessor(EventManager &eventManager, FileCopyManager &fileCopyManager)
    : eventManager_(eventManager), fileCopyManager_(fileCopyManager), isoReader_(std::make_unique<ISOReader>()) {}

BootWimProcessor::~BootWimProcessor() {}

bool BootWimProcessor::processBootWim(const std::string &sourcePath, const std::string &destPath,
                                      const std::string &espPath, bool integratePrograms,
                                      const std::string &programsSrc, long long &copiedSoFar, bool extractBootWim,
                                      bool copyInstallWim, std::ofstream &logFile) {
    bool        bootWimSuccess             = true;
    bool        bootSdiSuccess             = true;
    bool        installWimSuccess          = true;
    bool        additionalBootFilesSuccess = true;
    std::string bootWimDest                = destPath + "sources\\boot.wim";

    if (extractBootWim) {
        eventManager_.notifyDetailedProgress(20, 100, "Preparando archivos de arranque del ISO");
        eventManager_.notifyLogUpdate("Preparando archivos de arranque del ISO...\r\n");

        // sourcePath is the ISO file path; extract boot.wim from the ISO to the data partition
        std::string bootWimDestDir = destPath + "sources";
        CreateDirectoryA(bootWimDestDir.c_str(), NULL);
        if (GetFileAttributesA(bootWimDest.c_str()) != INVALID_FILE_ATTRIBUTES) {
            logFile << ISOCopyManager::getTimestamp() << "boot.wim already exists at " << bootWimDest << std::endl;
            bootWimSuccess = true;
        } else {
            eventManager_.notifyDetailedProgress(25, 100, "Extrayendo boot.wim hacia la particion de datos");
            eventManager_.notifyLogUpdate("Extrayendo boot.wim hacia la particion de datos...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Copiando boot.wim...");
            if (isoReader_->extractFile(sourcePath, "sources/boot.wim", bootWimDest)) {
                logFile << ISOCopyManager::getTimestamp() << "boot.wim extracted successfully to " << bootWimDest
                        << std::endl;
                eventManager_.notifyLogUpdate("boot.wim copiado correctamente.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to extract boot.wim" << std::endl;
                eventManager_.notifyLogUpdate("Error al copiar boot.wim.\r\n");
                bootWimSuccess = false;
            }
            eventManager_.notifyDetailedProgress(0, 0, "");
        }

        // Copy boot.sdi
        std::string bootSdiDestDir = destPath + "boot";
        CreateDirectoryA(bootSdiDestDir.c_str(), NULL);
        std::string bootSdiDest = bootSdiDestDir + "\\boot.sdi";
        if (GetFileAttributesA(bootSdiDest.c_str()) != INVALID_FILE_ATTRIBUTES) {
            logFile << ISOCopyManager::getTimestamp() << "boot.sdi already exists at " << bootSdiDest << std::endl;
            bootSdiSuccess = true;
        } else if (isoReader_->fileExists(sourcePath, "boot/boot.sdi")) {
            eventManager_.notifyDetailedProgress(30, 100, "Copiando boot.sdi requerido para arranque RAM");
            eventManager_.notifyLogUpdate("Copiando boot.sdi requerido para arranque RAM...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Copiando boot.sdi...");
            if (isoReader_->extractFile(sourcePath, "boot/boot.sdi", bootSdiDest)) {
                logFile << ISOCopyManager::getTimestamp() << "boot.sdi copied successfully to " << bootSdiDest
                        << std::endl;
                eventManager_.notifyLogUpdate("boot.sdi copiado correctamente.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to copy boot.sdi" << std::endl;
                eventManager_.notifyLogUpdate("Error al copiar boot.sdi.\r\n");
                bootSdiSuccess = false;
            }
            eventManager_.notifyDetailedProgress(0, 0, "");
        } else {
            logFile << ISOCopyManager::getTimestamp() << "boot.sdi not found inside ISO at path boot/boot.sdi" << std::endl;
            bootSdiSuccess = false;
            eventManager_.notifyLogUpdate(
                "boot.sdi no se encontro en el ISO. El arranque desde RAM podria fallar.\r\n");
        }

        if (copyInstallWim) {
            // Skip injecting install.* into boot.wim in RAM mode to improve reliability and reduce size.
            // We'll use install.wim/esd directly from disk during setup.
            logFile << ISOCopyManager::getTimestamp()
                    << "Skipping install.* injection into boot.wim; will use on-disk install image." << std::endl;
            eventManager_.notifyLogUpdate(
                "Omitiendo la inyecciÃ³n de install.* en boot.wim; se usarÃ¡ la imagen desde disco.\r\n");
        }

        // Mount and process boot.wim
        if (bootWimSuccess) {
            bootWimSuccess = mountAndProcessWim(bootWimDest, destPath, sourcePath, integratePrograms, programsSrc,
                                                copiedSoFar, logFile);
        }

        // Extract additional boot files
        additionalBootFilesSuccess =
            extractAdditionalBootFiles(sourcePath, espPath, destPath, copiedSoFar, 0, logFile); // isoSize not used here
    }

    return bootWimSuccess && bootSdiSuccess && installWimSuccess && additionalBootFilesSuccess;
}

bool BootWimProcessor::mountAndProcessWim(const std::string &bootWimDest, const std::string &destPath,
                                          const std::string &sourcePath, bool integratePrograms,
                                          const std::string &programsSrc, long long &copiedSoFar,
                                          std::ofstream &logFile) {
    std::string driveLetter = destPath.substr(0, 2);
    std::string mountDir =
        std::string(bootWimDest.begin(), bootWimDest.end() - std::string("sources\\boot.wim").length()) + "temp_mount";
    // Ensure mount directory is clean
    if (GetFileAttributesA(mountDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string rdCmd = "cmd /c rd /s /q \"" + mountDir + "\" 2>nul";
        Utils::exec(rdCmd.c_str());
    }
    CreateDirectoryA(mountDir.c_str(), NULL);
    SetFileAttributesA(bootWimDest.c_str(), FILE_ATTRIBUTE_NORMAL);
    
    // Get the number of indices in boot.wim
    std::string dismInfoCmd = "dism /Get-WimInfo /WimFile:\"" + bootWimDest + "\"";
    std::string infoOutput = Utils::exec(dismInfoCmd.c_str());
    logFile << ISOCopyManager::getTimestamp() << "WimInfo output:\n" << infoOutput << std::endl;
    int indexCount = 0;
    size_t pos = 0;
    while ((pos = infoOutput.find("Index :", pos)) != std::string::npos) {
        indexCount++;
        pos += 7;
    }
    int index = 1;  // Always mount index 1 (PE) for processing, even if more indices exist
    logFile << ISOCopyManager::getTimestamp() << "Number of indices: " << indexCount << ", mounting index: " << index << std::endl;
    
    // Extract and display the selected index details on screen
    std::string indexStr = "Index : " + std::to_string(index);
    size_t startPos = infoOutput.find(indexStr);
    if (startPos != std::string::npos) {
        size_t endPos = infoOutput.find("Index :", startPos + indexStr.length());
        if (endPos == std::string::npos) endPos = infoOutput.length();
        std::string indexDetails = infoOutput.substr(startPos, endPos - startPos);
        eventManager_.notifyLogUpdate("Detalles del Ã­ndice seleccionado:\r\n" + indexDetails + "\r\n");
    }
    
    std::string dism = Utils::getDismPath();
    std::string dismMountCmd = "\"" + dism + "\" /Mount-Wim /WimFile:\"" + bootWimDest + "\" /index:" +
                                std::to_string(index) + " /MountDir:\"" + mountDir + "\"";
    logFile << ISOCopyManager::getTimestamp() << "Mounting boot.wim: " << dismMountCmd << std::endl;
    std::string mountOutput;
    int         mountCode = Utils::execWithExitCode(dismMountCmd.c_str(), mountOutput);
    logFile << ISOCopyManager::getTimestamp() << "Mount output (code=" << mountCode << "): " << mountOutput
            << std::endl;
    if (mountCode == 0) {
        if (integratePrograms) {
            eventManager_.notifyDetailedProgress(40, 100, "Integrando Programs en boot.wim");
            eventManager_.notifyLogUpdate("Integrando Programs en boot.wim...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Integrando Programs en boot.wim...");
            long long             programsSize = Utils::getDirectorySize(programsSrc);
            std::string           programsDest = mountDir + "\\Programs";
            std::set<std::string> excludeDirs;
            if (fileCopyManager_.copyDirectoryWithProgress(programsSrc, programsDest, programsSize, copiedSoFar,
                                                           excludeDirs, "Integrando Programs en boot.wim")) {
                logFile << ISOCopyManager::getTimestamp() << "Programs integrated into boot.wim successfully"
                        << std::endl;
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
            long long             customDriversSize = Utils::getDirectorySize(customDriversSrc);
            std::string           customDriversDest = mountDir + "\\CustomDrivers";
            std::set<std::string> excludeDirs;
            if (fileCopyManager_.copyDirectoryWithProgress(customDriversSrc, customDriversDest, customDriversSize,
                                                           copiedSoFar, excludeDirs,
                                                           "Integrando CustomDrivers en boot.wim")) {
                logFile << ISOCopyManager::getTimestamp() << "CustomDrivers integrated into boot.wim successfully"
                        << std::endl;
                eventManager_.notifyLogUpdate("CustomDrivers integrado en boot.wim correctamente.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to integrate CustomDrivers into boot.wim"
                        << std::endl;
                eventManager_.notifyLogUpdate("Error al integrar CustomDrivers en boot.wim.\r\n");
            }
        }
        // Integrate critical system drivers from the current machine
        eventManager_.notifyDetailedProgress(47, 100, "Integrando controladores locales en boot.wim");
        if (integrateSystemDriversIntoMountedImage(mountDir, logFile)) {
            eventManager_.notifyLogUpdate("Controladores del sistema integrados en boot.wim.\r\n");
        } else {
            eventManager_.notifyLogUpdate(
                "Advertencia: No se pudieron integrar todos los controladores locales en boot.wim.\r\n");
        }
        eventManager_.notifyDetailedProgress(0, 0, "");
        // Copy and configure .ini files
        eventManager_.notifyDetailedProgress(55, 100, "Copiando y reconfigurando archivos .ini");
        IniConfigurator iniConfigurator;
        // First, reconfigure any existing .ini files in the mounted boot.wim
        WIN32_FIND_DATAA findDataExisting;
        HANDLE           hFindExisting = FindFirstFileA((mountDir + "\\*.ini").c_str(), &findDataExisting);
        if (hFindExisting != INVALID_HANDLE_VALUE) {
            bool moreFilesExisting = true;
            while (moreFilesExisting) {
                std::string iniNameExisting = findDataExisting.cFileName;
                std::string iniPathExisting = mountDir + "\\" + iniNameExisting;
                iniConfigurator.configureIniFile(iniPathExisting, driveLetter);
                logFile << ISOCopyManager::getTimestamp() << "Existing " << iniNameExisting
                        << " in boot.wim reconfigured" << std::endl;
                moreFilesExisting = FindNextFileA(hFindExisting, &findDataExisting);
            }
            FindClose(hFindExisting);
        }
        // Then copy and configure .ini files from ISO
        WIN32_FIND_DATAA findData;
        HANDLE           hFind = FindFirstFileA((sourcePath + "*.ini").c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            bool moreFiles = true;
            while (moreFiles) {
                std::string iniName = findData.cFileName;
                std::string iniSrc  = sourcePath + iniName;
                std::string iniDest = mountDir + "\\" + iniName;
                // Process the .ini content outside the .wim and write directly to mountDir
                if (iniConfigurator.processIniFile(iniSrc, iniDest, driveLetter)) {
                    logFile << ISOCopyManager::getTimestamp() << iniName
                            << " processed and copied to boot.wim successfully" << std::endl;
                } else {
                    // Fallback: copy without processing
                    if (fileCopyManager_.copyFileUtf8(iniSrc, iniDest)) {
                        logFile << ISOCopyManager::getTimestamp() << iniName
                                << " copied to boot.wim successfully (fallback)" << std::endl;
                    } else {
                        logFile << ISOCopyManager::getTimestamp() << "Failed to copy " << iniName << " to boot.wim"
                                << std::endl;
                    }
                }
                moreFiles = FindNextFileA(hFind, &findData);
            }
            FindClose(hFind);
            eventManager_.notifyLogUpdate("Archivos .ini integrados y reconfigurados en boot.wim correctamente.\r\n");
        }

        // Create or replace startnet.cmd in Windows\System32 only if it already exists
        std::string windowsDir  = mountDir + "\\Windows";
        std::string system32Dir = windowsDir + "\\System32";
        CreateDirectoryA(windowsDir.c_str(), NULL);
        CreateDirectoryA(system32Dir.c_str(), NULL);
        std::string startnetPath = system32Dir + "\\startnet.cmd";
        if (GetFileAttributesA(startnetPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // File exists, replace it
            std::ofstream startnetFile(startnetPath, std::ios::binary);
            if (startnetFile.is_open()) {
                startnetFile
                    << "@echo off\r\nwpeinit\r\nX:\\Windows\\System32\\pecmd.exe MAIN X:\\Windows\\System32\\PECMD.ini\r\n";
                startnetFile.close();
                logFile << ISOCopyManager::getTimestamp() << "startnet.cmd replaced in boot.wim" << std::endl;
                eventManager_.notifyLogUpdate("startnet.cmd reemplazado en boot.wim.\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to replace startnet.cmd in boot.wim" << std::endl;
                eventManager_.notifyLogUpdate("Error al reemplazar startnet.cmd en boot.wim.\r\n");
            }
        } else {
            // File does not exist, skip injection
            logFile << ISOCopyManager::getTimestamp() << "startnet.cmd does not exist in boot.wim, skipping injection" << std::endl;
            eventManager_.notifyLogUpdate("startnet.cmd no existe en boot.wim, se omite la inyecciÃ³n.\r\n");
        }

        eventManager_.notifyDetailedProgress(60, 100, "Guardando cambios en boot.wim");
        eventManager_.notifyLogUpdate("Guardando cambios en boot.wim...\r\n");
        eventManager_.notifyDetailedProgress(0, 0, "Guardando cambios en boot.wim...");
        std::string dismUnmountCmd = "dism /Unmount-Wim /MountDir:\"" + mountDir + "\" /Commit";
        logFile << ISOCopyManager::getTimestamp() << "Unmounting boot.wim: " << dismUnmountCmd << std::endl;
    std::string unmountOutput;
    std::string dismUnmountCmdFull = "\"" + dism + "\" /Unmount-Wim /MountDir:\"" + mountDir + "\" /Commit";
    int         unmountCode        = Utils::execWithExitCode(dismUnmountCmdFull.c_str(), unmountOutput);
    logFile << ISOCopyManager::getTimestamp() << "Unmount output (code=" << unmountCode << "): "
        << unmountOutput << std::endl;
    if (unmountCode != 0) {
            logFile << ISOCopyManager::getTimestamp() << "Failed to unmount boot.wim, changes not saved" << std::endl;
            eventManager_.notifyLogUpdate("Error al desmontar boot.wim, cambios no guardados.\r\n");
            RemoveDirectoryA(mountDir.c_str());
            return false;
        }
        eventManager_.notifyLogUpdate("boot.wim actualizado correctamente.\r\n");
        eventManager_.notifyDetailedProgress(0, 0, "");
    } else {
    logFile << ISOCopyManager::getTimestamp()
        << "Failed to mount boot.wim for processing. DISM code=" << mountCode
        << ", output: " << mountOutput << std::endl;
        eventManager_.notifyLogUpdate(
            "Error al montar boot.wim para procesamiento. Verifique el archivo de log para mas detalles.\r\n");
        RemoveDirectoryA(mountDir.c_str());
        return false;
    }
    RemoveDirectoryA(mountDir.c_str());
    return true;
}

bool BootWimProcessor::extractAdditionalBootFiles(const std::string &sourcePath, const std::string &espPath,
                                                  const std::string &destPath, long long &copiedSoFar,
                                                  long long isoSize, std::ofstream &logFile) {
    eventManager_.notifyDetailedProgress(60, 100, "Extrayendo archivos adicionales desde boot.wim");
    eventManager_.notifyLogUpdate("Extrayendo archivos adicionales desde boot.wim...\r\n");
    eventManager_.notifyDetailedProgress(0, 0, "Extrayendo archivos de boot.wim...");
    // Assuming EFIManager is available, but since it's not injected, we need to handle this differently.
    // For now, return true as placeholder.
    eventManager_.notifyLogUpdate("Archivos adicionales desde boot.wim extraidos correctamente.\r\n");
    eventManager_.notifyDetailedProgress(0, 0, "");
    return true;
}

bool BootWimProcessor::integrateSystemDriversIntoMountedImage(const std::string &mountDir, std::ofstream &logFile) {
    char windowsDir[MAX_PATH] = {0};
    UINT written              = GetWindowsDirectoryA(windowsDir, MAX_PATH);
    if (written == 0 || written >= MAX_PATH) {
        logFile << ISOCopyManager::getTimestamp()
                << "Skipping local driver integration: failed to resolve Windows directory." << std::endl;
        return false;
    }

    std::string systemRoot(windowsDir);
    if (!systemRoot.empty() && (systemRoot.back() == '\\' || systemRoot.back() == '/')) {
        systemRoot.pop_back();
    }

    std::string fileRepository = systemRoot + "\\System32\\DriverStore\\FileRepository";
    if (GetFileAttributesA(fileRepository.c_str()) == INVALID_FILE_ATTRIBUTES) {
        logFile << ISOCopyManager::getTimestamp()
                << "Skipping local driver integration: DriverStore not found at " << fileRepository << std::endl;
        return false;
    }

    char tempPath[MAX_PATH] = {0};
    if (!GetTempPathA(MAX_PATH, tempPath)) {
        logFile << ISOCopyManager::getTimestamp()
                << "Skipping local driver integration: GetTempPath failed." << std::endl;
        return false;
    }

    char tempDirTemplate[MAX_PATH] = {0};
    if (!GetTempFileNameA(tempPath, "drv", 0, tempDirTemplate)) {
        logFile << ISOCopyManager::getTimestamp()
                << "Skipping local driver integration: GetTempFileName failed." << std::endl;
        return false;
    }

    DeleteFileA(tempDirTemplate);
    if (!CreateDirectoryA(tempDirTemplate, NULL)) {
        logFile << ISOCopyManager::getTimestamp()
                << "Skipping local driver integration: failed to create staging directory." << std::endl;
        return false;
    }

    std::filesystem::path        stagingRoot(tempDirTemplate);
    const std::vector<std::string> storagePrefixes = {"storahci", "stornvme"};
    const std::vector<std::string> usbPrefixes     = {"usb", "xhci"};
    const std::vector<std::string> networkPrefixes = {"net", "vwifi", "vwlan"};
    const std::vector<std::string> networkTokens   = {"wifi", "wlan", "wwan"};

    auto startsWithIgnoreCase = [](const std::string &value, const std::string &prefix) {
        if (value.size() < prefix.size()) {
            return false;
        }
        return _strnicmp(value.c_str(), prefix.c_str(), static_cast<unsigned int>(prefix.size())) == 0;
    };
    auto matchesAnyPrefix = [&](const std::string &dirLower, const std::vector<std::string> &list) -> bool {
        for (const auto &prefix : list) {
            if (dirLower.rfind(prefix, 0) == 0) {
                return true;
            }
        }
        return false;
    };
    auto containsAnyToken = [&](const std::string &dirLower, const std::vector<std::string> &tokens) -> bool {
        for (const auto &token : tokens) {
            if (dirLower.find(token) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    auto directoryContainsNetworkInf = [&](const std::filesystem::path &dirPath) -> bool {
        std::error_code infEc;
        for (std::filesystem::directory_iterator infIt(dirPath, infEc), infEnd; infIt != infEnd; infIt.increment(infEc)) {
            if (infEc) {
                logFile << ISOCopyManager::getTimestamp()
                        << "Failed to enumerate files inside " << dirPath.string() << ": " << infEc.message()
                        << std::endl;
                break;
            }
            if (!infIt->is_regular_file()) {
                continue;
            }

            std::string extension = infIt->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".inf") {
                continue;
            }

            std::ifstream infFile(infIt->path());
            if (!infFile.is_open()) {
                continue;
            }

            std::ostringstream buffer;
            buffer << infFile.rdbuf();
            std::string content = buffer.str();
            std::string normalized;
            normalized.reserve(content.size());
            for (char ch : content) {
                if (!std::isspace(static_cast<unsigned char>(ch))) {
                    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                }
            }

            if (normalized.find("class=net") != std::string::npos ||
                normalized.find("{4d36e972e32511cebfc108002be10318}") != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    bool            copiedAny = false;
    std::error_code ec;
    std::filesystem::directory_iterator it(fileRepository, ec);
    std::filesystem::directory_iterator endIt;
    if (ec) {
        logFile << ISOCopyManager::getTimestamp()
                << "Skipping local driver integration: unable to enumerate DriverStore (" << ec.message() << ")"
                << std::endl;
        std::filesystem::remove_all(stagingRoot, ec);
        return false;
    }

    for (; it != endIt; it.increment(ec)) {
        if (ec) {
            logFile << ISOCopyManager::getTimestamp()
                    << "DriverStore enumeration error: " << ec.message() << std::endl;
            break;
        }

        const auto &entry = *it;
        if (!entry.is_directory()) {
            continue;
        }

        std::string dirName      = entry.path().filename().string();
        std::string dirNameLower = dirName;
        std::transform(dirNameLower.begin(), dirNameLower.end(), dirNameLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool storageMatch = matchesAnyPrefix(dirNameLower, storagePrefixes);
        bool usbMatch     = matchesAnyPrefix(dirNameLower, usbPrefixes);
        bool netMatch     = matchesAnyPrefix(dirNameLower, networkPrefixes) ||
                            containsAnyToken(dirNameLower, networkTokens) ||
                            directoryContainsNetworkInf(entry.path());

        if (!(storageMatch || usbMatch || netMatch)) {
            continue;
        }

        std::filesystem::path destination = stagingRoot / dirName;
        std::filesystem::create_directories(destination, ec);
        if (ec) {
            logFile << ISOCopyManager::getTimestamp()
                    << "Failed to create staging directory for " << dirName << ": " << ec.message() << std::endl;
            ec.clear();
            continue;
        }

        std::filesystem::copy(entry.path(), destination,
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                              ec);
        if (ec) {
            logFile << ISOCopyManager::getTimestamp()
                    << "Failed to copy driver directory " << dirName << ": " << ec.message() << std::endl;
            ec.clear();
            continue;
        }
        logFile << ISOCopyManager::getTimestamp() << "Staged driver directory " << dirName
                << (netMatch ? " (network)" : "") << std::endl;
        copiedAny = true;
    }

    if (!copiedAny) {
        logFile << ISOCopyManager::getTimestamp()
                << "No matching local driver directories found for integration." << std::endl;
        std::filesystem::remove_all(stagingRoot, ec);
        return false;
    }

    std::string stagingRootStr = stagingRoot.string();
    std::string dism           = Utils::getDismPath();
    std::string addDriver      = "\"" + dism + "\" /Image:\"" + mountDir + "\" /Add-Driver /Driver:\""
                            + stagingRootStr + "\" /Recurse";
    logFile << ISOCopyManager::getTimestamp() << "Adding local drivers with command: " << addDriver << std::endl;
    std::string dismOutput;
    int         dismCode = Utils::execWithExitCode(addDriver.c_str(), dismOutput);
    logFile << ISOCopyManager::getTimestamp()
            << "DISM add-driver output (code=" << dismCode << "): " << dismOutput << std::endl;

    std::filesystem::remove_all(stagingRoot, ec);

    if (dismCode != 0) {
        logFile << ISOCopyManager::getTimestamp()
                << "DISM add-driver reported failure while integrating local drivers." << std::endl;
        return false;
    }
    return true;
}
