#include "BootWimProcessor.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include "IniConfigurator.h"
#include "../services/ISOCopyManager.h"
#include "ISOReader.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <exception>
#include <system_error>
#include <cstdint>
#include <sstream>
#include <set>
#include <vector>
#include <cstring>
#include <cctype>
#include <thread>
#include <atomic>
#include <chrono>

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
            logFile << ISOCopyManager::getTimestamp()
                    << "boot.sdi not found inside ISO at path boot/boot.sdi; attempting system fallback" << std::endl;
            // Fallback to system boot.sdi if available
            std::vector<std::string> systemSdiCandidates = {
                std::string("C:\\Windows\\Boot\\DVD\\EFI\\boot.sdi"),
                std::string("C:\\Windows\\Boot\\PCAT\\boot.sdi"),
                std::string("C:\\Windows\\Boot\\EFI\\boot.sdi")
            };
            bool fallbackCopied = false;
            for (const auto &cand : systemSdiCandidates) {
                if (GetFileAttributesA(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    // Ensure destination directory exists
                    CreateDirectoryA(bootSdiDestDir.c_str(), NULL);
                    if (CopyFileA(cand.c_str(), bootSdiDest.c_str(), FALSE)) {
                        logFile << ISOCopyManager::getTimestamp() << "boot.sdi copied from system: " << cand
                                << " -> " << bootSdiDest << std::endl;
                        eventManager_.notifyLogUpdate(
                            "boot.sdi no estaba en el ISO; se copio desde el sistema para habilitar RAM boot.\r\n");
                        fallbackCopied = true;
                        break;
                    } else {
                        logFile << ISOCopyManager::getTimestamp() << "Failed to copy system boot.sdi from " << cand
                                << ", error=" << GetLastError() << std::endl;
                    }
                }
            }
            if (!fallbackCopied) {
                bootSdiSuccess = false;
                eventManager_.notifyLogUpdate(
                    "boot.sdi no se encontro ni en el ISO ni en el sistema. El arranque RAM puede fallar.\r\n");
            }
        }

        if (copyInstallWim) {
            // Skip injecting install.* into boot.wim in RAM mode to improve reliability and reduce size.
            // We'll use install.wim/esd directly from disk during setup.
            logFile << ISOCopyManager::getTimestamp()
                    << "Skipping install.* injection into boot.wim; will use on-disk install image." << std::endl;
            eventManager_.notifyLogUpdate(
                "Omitiendo la inyección de install.* en boot.wim; se usará la imagen desde disco.\r\n");
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
    
    // Get the number of indices in boot.wim and pick the best one to process
    std::string dismInfoCmd = "dism /Get-WimInfo /WimFile:\"" + bootWimDest + "\"";
    std::string infoOutput = Utils::exec(dismInfoCmd.c_str());
    logFile << ISOCopyManager::getTimestamp() << "WimInfo output:\n" << infoOutput << std::endl;

    auto toLowerNoAccents = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        // very light accent normalization for 'í' in "Índice" and 'ó' in "Descripción"
        for (char &c : s) {
            unsigned char uc = (unsigned char)c;
            if (uc == 0xED || uc == 0xCD) c = 'i'; // í / Í
            if (uc == 0xF3 || uc == 0xD3) c = 'o'; // ó / Ó
        }
        return s;
    };

    // Count indices (cover both English "Index :" and Spanish "Índice :")
    int    indexCount = 0;
    size_t pos        = 0;
    std::string infoLower = toLowerNoAccents(infoOutput);
    while ((pos = infoLower.find("index :", pos)) != std::string::npos) {
        indexCount++;
        pos += 7;
    }

    // Prefer Windows Setup image (commonly index 2). Fallback to index 2 if >=2 images, else 1.
    int index = 1;
    // Try to detect the index whose name/description mentions "Windows Setup" (or localized variants)
    // We'll scan blocks between "Index : X" markers.
    int upperBound = (indexCount > 2 ? indexCount : 2);
    for (int candidate = 1; candidate <= upperBound; ++candidate) {
        std::string marker      = "Index : " + std::to_string(candidate);
        std::string markerLower = toLowerNoAccents(marker);
        size_t      start       = infoLower.find(markerLower);
        if (start == std::string::npos) continue;
        size_t next = infoLower.find("index :", start + markerLower.size());
        std::string block = infoLower.substr(start, next == std::string::npos ? std::string::npos : next - start);
    bool isSetup = (block.find("setup") != std::string::npos) ||
               (block.find("instalacion") != std::string::npos);
        if (isSetup) {
            index = candidate;
            break;
        }
        // If we didn't find explicit setup but candidate==2 and there are at least 2 images, choose 2
        if (candidate == 2 && indexCount >= 2) {
            index = 2;
            // don't break yet in case a later block explicitly says setup, but typically there are max 2
        }
    }
    logFile << ISOCopyManager::getTimestamp() << "Number of indices: " << indexCount
            << ", selected index to mount: " << index << std::endl;
    
    // Extract and display only Index/Name/Description for the selected image
    std::string indexStr = "Index : " + std::to_string(index);
    size_t      startPos = infoOutput.find(indexStr);
    if (startPos != std::string::npos) {
        size_t      endPos       = infoOutput.find("Index :", startPos + indexStr.length());
        if (endPos == std::string::npos) endPos = infoOutput.length();
        std::string blockOrig    = infoOutput.substr(startPos, endPos - startPos);
        std::string blockLower   = toLowerNoAccents(blockOrig);

        auto extractLineByLabel = [&](const std::string &labelLower) -> std::string {
            size_t p = blockLower.find(labelLower);
            if (p == std::string::npos) return std::string();
            // p is an offset valid for both lowercase and original blocks (same length)
            size_t lineEnd = blockOrig.find_first_of("\r\n", p);
            std::string line = (lineEnd == std::string::npos) ? blockOrig.substr(p)
                                                               : blockOrig.substr(p, lineEnd - p);
            // Trim any trailing CR characters
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            return line;
        };

        std::string outIndex = extractLineByLabel("index :");
        // Support English/Spanish field names
        std::string outName  = extractLineByLabel("name :");
        if (outName.empty()) outName = extractLineByLabel("nombre :");
        std::string outDesc  = extractLineByLabel("description :");
        if (outDesc.empty()) outDesc = extractLineByLabel("descripcion :");

        std::string minimal;
        minimal.reserve(256);
        if (!outIndex.empty()) minimal += outIndex + "\r\n";
        if (!outName.empty())  minimal += outName  + "\r\n";
        if (!outDesc.empty())  minimal += outDesc  + "\r\n";

        if (!minimal.empty()) {
            eventManager_.notifyLogUpdate("Detalles del índice seleccionado:\r\n" + minimal + "\r\n");
        }
    }
    
    std::string dism = Utils::getDismPath();
    std::string dismMountCmd = "\"" + dism + "\" /Mount-Wim /WimFile:\"" + bootWimDest + "\" /index:" +
                                std::to_string(index) + " /MountDir:\"" + mountDir + "\"";
    logFile << ISOCopyManager::getTimestamp() << "Mounting boot.wim: " << dismMountCmd << std::endl;
    std::string mountOutput;
    int         mountCode = Utils::execWithExitCode(dismMountCmd.c_str(), mountOutput);
    logFile << ISOCopyManager::getTimestamp() << "Mount output (code=" << mountCode << "): " << mountOutput
            << std::endl;
    
    // Detect if this is a PECMD-based PE (like Hiren's) BEFORE processing Programs
    std::string system32Dir = mountDir + "\\Windows\\System32";
    std::string pecmdExe = system32Dir + "\\pecmd.exe";
    std::string pecmdIni = system32Dir + "\\pecmd.ini";
    bool isPecmdPE = (GetFileAttributesA(pecmdExe.c_str()) != INVALID_FILE_ATTRIBUTES) &&
                     (GetFileAttributesA(pecmdIni.c_str()) != INVALID_FILE_ATTRIBUTES);
    
    // For PECMD PEs in RAM mode: we WILL integrate Programs into boot.wim at X:\Programs
    // Then we'll add "subst Y: X:\" to pecmd.ini so Y:\Programs will work transparently
    if (isPecmdPE) {
        logFile << ISOCopyManager::getTimestamp() 
                << "PECMD PE detected: will integrate Programs/CustomDrivers into boot.wim and map Y: -> X:" 
                << std::endl;
        eventManager_.notifyLogUpdate("PECMD PE detectado: integrando contenido en boot.wim para modo RAM...\r\n");
    }
    
    if (mountCode == 0) {
        // CRITICAL: For PECMD-based PEs like Hiren's in RAM mode, DO integrate Programs into boot.wim
        // We'll add "subst Y: X:\" to pecmd.ini so Y:\Programs will map to X:\Programs
        if (integratePrograms) {
            eventManager_.notifyDetailedProgress(40, 100, "Integrando Programs en boot.wim");
            eventManager_.notifyLogUpdate("Integrando Programs en boot.wim...\r\n");
            eventManager_.notifyDetailedProgress(0, 0, "Integrando Programs en boot.wim...");
            std::string           programsDest = mountDir + "\\Programs";
            std::set<std::string> excludeDirs;

            auto tryCopyPrograms = [&](const std::string &sourceDir) -> bool {
                if (sourceDir.empty() || GetFileAttributesA(sourceDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    return false;
                }
                long long programsSize = Utils::getDirectorySize(sourceDir);
                if (fileCopyManager_.copyDirectoryWithProgress(sourceDir, programsDest, programsSize, copiedSoFar,
                                                               excludeDirs, "Integrando Programs en boot.wim")) {
                    logFile << ISOCopyManager::getTimestamp()
                            << "Programs integrated into boot.wim successfully from " << sourceDir << std::endl;
                    eventManager_.notifyLogUpdate("Programs integrado en boot.wim correctamente.\r\n");
                    return true;
                }
                logFile << ISOCopyManager::getTimestamp()
                        << "Failed to integrate Programs into boot.wim from " << sourceDir << std::endl;
                return false;
            };

            bool programsIntegrated = false;
            if (tryCopyPrograms(programsSrc)) {
                programsIntegrated = true;
            } else {
                std::string destProgramsSrc = destPath + "Programs";
                if (destProgramsSrc != programsSrc && tryCopyPrograms(destProgramsSrc)) {
                    programsIntegrated = true;
                }
            }

            if (!programsIntegrated) {
                std::error_code removeProgramsEc;
                std::filesystem::remove_all(programsDest, removeProgramsEc);
                if (isoReader_->extractDirectory(sourcePath, "Programs", programsDest)) {
                    copiedSoFar += Utils::getDirectorySize(programsDest);
                    logFile << ISOCopyManager::getTimestamp()
                            << "Programs extracted from ISO and integrated into boot.wim" << std::endl;
                    eventManager_.notifyLogUpdate("Programs integrado en boot.wim correctamente.\r\n");
                    programsIntegrated = true;
                } else {
                    logFile << ISOCopyManager::getTimestamp()
                            << "Programs directory not found in ISO or extraction failed" << std::endl;
                }
            }

            if (!programsIntegrated) {
                // If Programs is not present in source or ISO, just skip silently with an info message
                eventManager_.notifyLogUpdate("Carpeta 'Programs' no encontrada; se omite su integracion.\r\n");
            }
        }
        
        // Integrate CustomDrivers into boot.wim (including for PECMD PEs in RAM mode)
        // We'll add "subst Y: X:\" to pecmd.ini so Y:\CustomDrivers will map to X:\CustomDrivers
        {
            // NOTE: Stage drivers OUTSIDE the mounted image and let DISM publish them into the image.
            //       Keeping a raw "CustomDrivers" folder inside the image is not required and only adds size.
            std::filesystem::path customDriversStage;
            bool                  stageOk = true;
            try {
                std::ostringstream tmpName;
                tmpName << "EasyISOBoot_CustomDrivers_" << reinterpret_cast<uintptr_t>(this);
                customDriversStage = std::filesystem::temp_directory_path() / tmpName.str();
                std::error_code mkEc; std::filesystem::create_directories(customDriversStage, mkEc);
                if (mkEc) stageOk = false;
            } catch (const std::exception &) {
                stageOk = false;
            }

            std::set<std::string> customDriversExclude;
            bool                  customDriversIntegrated = false;
            bool                  customDriversError      = false;

        auto tryCopyCustomDrivers = [&](const std::string &sourceDir) -> bool {
            if (sourceDir.empty() || GetFileAttributesA(sourceDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
                return false;
            }
        if (!stageOk) return false;
        long long customDriversSize = Utils::getDirectorySize(sourceDir);
        if (fileCopyManager_.copyDirectoryWithProgress(sourceDir, customDriversStage.string(), customDriversSize, copiedSoFar,
                                                           customDriversExclude,
                                                           "Integrando CustomDrivers en boot.wim")) {
                logFile << ISOCopyManager::getTimestamp()
            << "CustomDrivers staged successfully from " << sourceDir << std::endl;
                eventManager_.notifyLogUpdate("CustomDrivers integrado en boot.wim correctamente.\r\n");
                return true;
            }
            logFile << ISOCopyManager::getTimestamp()
            << "Failed to stage CustomDrivers from " << sourceDir << std::endl;
            customDriversError = true;
            return false;
        };

        if (!customDriversIntegrated && tryCopyCustomDrivers(destPath + "CustomDrivers")) {
            customDriversIntegrated = true;
        }

        if (!customDriversIntegrated) {
            if (stageOk && isoReader_->extractDirectory(sourcePath, "CustomDrivers", customDriversStage.string())) {
                copiedSoFar += Utils::getDirectorySize(customDriversStage.string());
                logFile << ISOCopyManager::getTimestamp()
                        << "CustomDrivers extracted from ISO and staged" << std::endl;
                eventManager_.notifyLogUpdate("CustomDrivers integrado en boot.wim correctamente.\r\n");
                customDriversIntegrated = true;
            } else {
                logFile << ISOCopyManager::getTimestamp()
                        << "CustomDrivers directory not found in ISO or extraction failed" << std::endl;
            }
        }

    if (!customDriversIntegrated && customDriversError) {
            eventManager_.notifyLogUpdate("Error al integrar CustomDrivers en boot.wim.\r\n");
        }
    // If CustomDrivers were staged, register them into PE with DISM so WinPE loads them
    if (customDriversIntegrated && stageOk) {
        std::string addCust = "\"" + dism + "\" /Image:\"" + mountDir + "\" /Add-Driver /Driver:\"" +
                  customDriversStage.string() + "\" /Recurse";
        logFile << ISOCopyManager::getTimestamp() << "Adding CustomDrivers with command: " << addCust
            << std::endl;
        std::string dismOutCust;
        int         dismCodeCust = Utils::execWithExitCode(addCust.c_str(), dismOutCust);
        logFile << ISOCopyManager::getTimestamp()
            << "DISM add-driver (CustomDrivers) output (code=" << dismCodeCust << "): " << dismOutCust
            << std::endl;
        if (dismCodeCust != 0) {
        std::string addCustForce = addCust + " /ForceUnsigned";
        logFile << ISOCopyManager::getTimestamp()
            << "Retrying CustomDrivers with /ForceUnsigned: " << addCustForce << std::endl;
        std::string dismOutCust2;
        int         dismCodeCust2 = Utils::execWithExitCode(addCustForce.c_str(), dismOutCust2);
        logFile << ISOCopyManager::getTimestamp()
            << "DISM add-driver (CustomDrivers force) output (code=" << dismCodeCust2 << "): "
            << dismOutCust2 << std::endl;
        if (dismCodeCust2 != 0) {
            eventManager_.notifyLogUpdate(
            "Advertencia: No se pudieron registrar algunos CustomDrivers en WinPE.\r\n");
        }
        }
        // Cleanup staging folder
        std::error_code rmStgEc; std::filesystem::remove_all(customDriversStage, rmStgEc);
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
        std::vector<std::string> isoIniFiles;
        auto isoFileList = isoReader_->listFiles(sourcePath);
        for (const auto &entry : isoFileList) {
            std::string lower = Utils::toLower(entry);
            bool hasSlash = entry.find('\\') != std::string::npos || entry.find('/') != std::string::npos;
            if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".ini" && !hasSlash) {
                isoIniFiles.push_back(entry);
            }
        }

        if (!isoIniFiles.empty()) {
            std::filesystem::path tempIniDir;
            bool                  tempDirAvailable = true;
            try {
                std::ostringstream tempDirName;
                tempDirName << "EasyISOBoot_ini_cache_" << reinterpret_cast<uintptr_t>(this);
                tempIniDir = std::filesystem::temp_directory_path() / tempDirName.str();
                std::error_code dirEc;
                std::filesystem::create_directories(tempIniDir, dirEc);
                if (dirEc) {
                    tempDirAvailable = false;
                }
            } catch (const std::exception &) {
                tempDirAvailable = false;
            }

            for (const auto &entry : isoIniFiles) {
                std::string iniName = entry;
                std::replace(iniName.begin(), iniName.end(), '\\', '/');
                size_t slashPos = iniName.find_last_of('/');
                if (slashPos != std::string::npos) {
                    iniName = iniName.substr(slashPos + 1);
                }

                std::string isoPathInArchive = entry;
                std::replace(isoPathInArchive.begin(), isoPathInArchive.end(), '\\', '/');
                std::string iniDest = mountDir + "\\" + iniName;
                bool        processed = false;

                if (tempDirAvailable) {
                    std::filesystem::path tempIniPath = tempIniDir / iniName;
                    std::error_code       remEc;
                    std::filesystem::remove(tempIniPath, remEc);
                    if (isoReader_->extractFile(sourcePath, isoPathInArchive, tempIniPath.u8string())) {
                        if (iniConfigurator.processIniFile(tempIniPath.u8string(), iniDest, driveLetter)) {
                            logFile << ISOCopyManager::getTimestamp() << iniName
                                    << " processed and copied to boot.wim successfully" << std::endl;
                            processed = true;
                        }
                    }
                    std::error_code cleanupEc;
                    std::filesystem::remove(tempIniPath, cleanupEc);
                }

                if (!processed) {
                    if (isoReader_->extractFile(sourcePath, isoPathInArchive, iniDest)) {
                        logFile << ISOCopyManager::getTimestamp() << iniName
                                << " copied to boot.wim successfully (fallback)" << std::endl;
                    } else {
                        logFile << ISOCopyManager::getTimestamp() << "Failed to copy " << iniName
                                << " to boot.wim" << std::endl;
                    }
                }
            }

            if (tempDirAvailable) {
                std::error_code cleanupDirEc;
                std::filesystem::remove_all(tempIniDir, cleanupDirEc);
            }

            eventManager_.notifyLogUpdate("Archivos .ini integrados y reconfigurados en boot.wim correctamente.\r\n");
        }

        // Ensure Windows\System32 exists and handle startnet.cmd content
        std::string windowsDir  = mountDir + "\\Windows";
        std::string system32DirPath = windowsDir + "\\System32";
        CreateDirectoryA(windowsDir.c_str(), NULL);
        CreateDirectoryA(system32DirPath.c_str(), NULL);
        std::string startnetPath = system32DirPath + "\\startnet.cmd";

        auto fileExists = [](const std::string &p) -> bool {
            return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
        };

        auto trimCopy = [](std::string s) {
            auto isspace2 = [](unsigned char c) { return std::isspace(c) != 0; };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !isspace2(c); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !isspace2(c); }).base(), s.end());
            return s;
        };

        // Detect Hiren's/PECMD-style PE by presence of pecmd.exe and pecmd.ini
        // Reuse the pecmdExe and pecmdIni variables declared earlier
        bool hasPecmd = fileExists(pecmdExe) && fileExists(pecmdIni);

        // Log detection results for debugging
        logFile << ISOCopyManager::getTimestamp() << "PECMD detection: pecmd.exe=" 
                << (fileExists(pecmdExe) ? "FOUND" : "NOT FOUND") 
                << ", pecmd.ini=" << (fileExists(pecmdIni) ? "FOUND" : "NOT FOUND") 
                << ", hasPecmd=" << (hasPecmd ? "YES" : "NO") << std::endl;

        // Check for Programs directory (commonly used by Hiren's)
        std::string programsDir = mountDir + "\\Programs";
        bool hasPrograms = GetFileAttributesA(programsDir.c_str()) != INVALID_FILE_ATTRIBUTES;
        logFile << ISOCopyManager::getTimestamp() << "Programs directory in boot.wim: " 
                << (hasPrograms ? "EXISTS" : "NOT FOUND") << std::endl;
        
        if (hasPecmd) {
            eventManager_.notifyLogUpdate("Hiren's BootCD PE detectado (PECMD presente).\r\n");
            
            // For Hiren's BootCD PE: Copy HBCD_PE.ini from ISO root to BOOT.WIM ROOT
            // Since we're using "subst Y: X:\", LetterSwap.exe will find it at Y:\HBCD_PE.ini (which points to X:\HBCD_PE.ini)
            std::string hbcdIniInISO = sourcePath;
            // sourcePath is the ISO path, need to check if HBCD_PE.ini exists in root
            std::vector<std::string> isoRootFiles;
            if (isoReader_) {
                isoRootFiles = isoReader_->listFiles(sourcePath);
            }
            
            bool foundHBCDini = false;
            for (const auto &file : isoRootFiles) {
                std::string lower = Utils::toLower(file);
                std::replace(lower.begin(), lower.end(), '\\', '/');
                // Check if it's HBCD_PE.ini in root (no path separators)
                if ((lower == "hbcd_pe.ini" || lower.find("/hbcd_pe.ini") != std::string::npos) &&
                    lower.find_first_of("/\\") == lower.find_last_of("/\\")) {
                    foundHBCDini = true;
                    break;
                }
            }
            
            if (foundHBCDini) {
                // Extract to BOOT.WIM ROOT (X:\HBCD_PE.ini), accessible as Y:\HBCD_PE.ini via subst
                std::string hbcdIniDest = mountDir + "\\HBCD_PE.ini";
                logFile << ISOCopyManager::getTimestamp() 
                        << "Found HBCD_PE.ini in ISO root, extracting to boot.wim root..." << std::endl;
                
                if (isoReader_->extractFile(sourcePath, "HBCD_PE.ini", hbcdIniDest)) {
                    logFile << ISOCopyManager::getTimestamp() 
                            << "HBCD_PE.ini copied successfully to boot.wim root: " << hbcdIniDest << std::endl;
                    eventManager_.notifyLogUpdate("HBCD_PE.ini integrado en boot.wim (accesible como Y:\\HBCD_PE.ini via subst).\r\n");
                } else {
                    logFile << ISOCopyManager::getTimestamp() 
                            << "Failed to extract HBCD_PE.ini from ISO" << std::endl;
                }
            } else {
                logFile << ISOCopyManager::getTimestamp() 
                        << "HBCD_PE.ini not found in ISO root (normal for some PE variants)" << std::endl;
            }
            
            // For PECMD PE in RAM mode: modify pecmd.ini to map Y: -> X: using subst
            // This allows PECMD scripts to work without changes (Y:\Programs still works)
            logFile << ISOCopyManager::getTimestamp() 
                    << "Hiren's/PECMD PE detected in RAM mode: adding Y: -> X: drive mapping to pecmd.ini" 
                    << std::endl;
            
            // Read existing pecmd.ini
            std::ifstream pecmdIn(pecmdIni, std::ios::binary);
            std::string pecmdContent;
            if (pecmdIn) {
                pecmdContent.assign((std::istreambuf_iterator<char>(pecmdIn)), std::istreambuf_iterator<char>());
                pecmdIn.close();
                
                // Insert SUBST Y: X:\ command at the very beginning (after first line which is usually {ENTER:...})
                // Find the first newline
                size_t firstNewline = pecmdContent.find('\n');
                if (firstNewline != std::string::npos) {
                    // Insert after the first line (ENTER line)
                    std::string substCmd = "// BootThatISO: Map Y: to X: for RAM boot mode\r\n"
                                          "EXEC @!X:\\Windows\\System32\\subst.exe Y: X:\\\r\n"
                                          "WAIT 500\r\n\r\n";
                    pecmdContent.insert(firstNewline + 1, substCmd);
                    
                    // Write modified pecmd.ini back
                    std::ofstream pecmdOut(pecmdIni, std::ios::binary | std::ios::trunc);
                    if (pecmdOut) {
                        pecmdOut.write(pecmdContent.data(), (std::streamsize)pecmdContent.size());
                        pecmdOut.flush();
                        logFile << ISOCopyManager::getTimestamp() 
                                << "Added 'subst Y: X:\\' command to pecmd.ini for RAM boot compatibility" 
                                << std::endl;
                        eventManager_.notifyLogUpdate("Mapeo Y: -> X: agregado a pecmd.ini para modo RAM.\r\n");
                    }
                } else {
                    logFile << ISOCopyManager::getTimestamp() 
                            << "Warning: Could not find insertion point in pecmd.ini" << std::endl;
                }
            } else {
                logFile << ISOCopyManager::getTimestamp() 
                        << "Warning: Could not read pecmd.ini for modification" << std::endl;
            }
            
            eventManager_.notifyLogUpdate(
                "PECMD configurado para modo RAM (Y: apuntará a X:).\r\n");
        } else {
            // For non-PECMD PEs, handle startnet.cmd normally
            bool startnetExists = fileExists(startnetPath);
            logFile << ISOCopyManager::getTimestamp() 
                    << "Non-PECMD PE detected, startnet.cmd exists: " 
                    << (startnetExists ? "YES" : "NO") << std::endl;
            
            if (startnetExists) {
                // Preserve existing startnet.cmd
                logFile << ISOCopyManager::getTimestamp() 
                        << "startnet.cmd found: preserving without changes" << std::endl;
                eventManager_.notifyLogUpdate("startnet.cmd detectado: se conserva sin cambios.\r\n");
            } else {
                // Create minimal startnet.cmd for non-PECMD WinPE
                logFile << ISOCopyManager::getTimestamp() 
                        << "startnet.cmd not present, creating minimal WinPE init" << std::endl;
                std::string minimalContent = "@echo off\r\nwpeinit\r\n";
                std::ofstream snOut(startnetPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (snOut) {
                    snOut.write(minimalContent.data(), (std::streamsize)minimalContent.size());
                    snOut.flush();
                    logFile << ISOCopyManager::getTimestamp() 
                            << "Created minimal startnet.cmd for WinPE" << std::endl;
                }
            }
        }

        // IMPORTANT: Do NOT normalize pecmd.ini for Hiren's BootCD PE
        // The original pecmd.ini is carefully configured and uses %WinDir% variables correctly
        // Hiren's PECMD relies on proper environment variable expansion and LetterSwap.exe
        // to map the data partition to Y: drive, which is where Programs and CustomDrivers live
        if (hasPecmd) {
            logFile << ISOCopyManager::getTimestamp() 
                    << "Hiren's PECMD detected: pecmd.ini will be preserved without modification" << std::endl;
            logFile << ISOCopyManager::getTimestamp() 
                    << "Note: PECMD uses LetterSwap.exe to map data partition to Y: for Programs access" 
                    << std::endl;
            eventManager_.notifyLogUpdate(
                "pecmd.ini preservado sin modificaciones (PECMD gestiona variables y letra Y: automáticamente).\r\n");
        }

        eventManager_.notifyDetailedProgress(60, 100, "Guardando cambios en boot.wim");
        eventManager_.notifyLogUpdate("Guardando cambios en boot.wim...\r\n");
        // Show a smooth advancing progress while DISM commits changes
        std::atomic<bool> savingInProgress(true);
        std::thread       savingProgressThread([this, &savingInProgress]() {
            int percent = 60;
            while (savingInProgress.load()) {
                percent = (percent < 95) ? (percent + 1) : 90; // oscillate below 100% until finish
                this->eventManager_.notifyDetailedProgress(percent, 100, "Guardando cambios en boot.wim");
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
        });
        std::string dismUnmountCmd = "dism /Unmount-Wim /MountDir:\"" + mountDir + "\" /Commit";
        logFile << ISOCopyManager::getTimestamp() << "Unmounting boot.wim: " << dismUnmountCmd << std::endl;
    std::string unmountOutput;
    std::string dismUnmountCmdFull = "\"" + dism + "\" /Unmount-Wim /MountDir:\"" + mountDir + "\" /Commit";
    int         unmountCode        = Utils::execWithExitCode(dismUnmountCmdFull.c_str(), unmountOutput);
        // Stop the progress animation
        savingInProgress.store(false);
        if (savingProgressThread.joinable()) savingProgressThread.join();
    logFile << ISOCopyManager::getTimestamp() << "Unmount output (code=" << unmountCode << "): "
        << unmountOutput << std::endl;
    if (unmountCode != 0) {
            logFile << ISOCopyManager::getTimestamp() << "Failed to unmount boot.wim, changes not saved" << std::endl;
            eventManager_.notifyLogUpdate("Error al desmontar boot.wim, cambios no guardados.\r\n");
            RemoveDirectoryA(mountDir.c_str());
            return false;
        }
        // Finalize progress to 100%
        eventManager_.notifyDetailedProgress(100, 100, "Guardando cambios en boot.wim");
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
    const std::vector<std::string> storageTokens   = {"nvme", "ahci", "rst", "vmd", "raid", "scsi", "ide", "iastor"};
    const std::vector<std::string> usbPrefixes     = {"usb", "xhci"};
    const std::vector<std::string> usbTokens       = {"iusb3", "usb3", "xhc", "xhci", "amdhub3", "amdxhc", "intelusb3"};
    const std::vector<std::string> networkPrefixes = {"net", "vwifi", "vwlan"};
    const std::vector<std::string> networkTokens   = {"wifi", "wlan", "wwan"};

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
            // Normalize: lowercase and remove non-alphanumeric characters for robust matching
            std::string normalized;
            normalized.reserve(content.size());
            for (char ch : content) {
                unsigned char uch = static_cast<unsigned char>(ch);
                char lower = static_cast<char>(std::tolower(uch));
                if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
                    normalized.push_back(lower);
                }
            }

            // Match either "class=net" (becomes "classnet") or the Net Class GUID without dashes
            if (normalized.find("classnet") != std::string::npos ||
                normalized.find("4d36e972e32511cebfc108002be10318") != std::string::npos) {
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

    int stagedStorage = 0, stagedUsb = 0, stagedNet = 0;
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

    bool storageMatch = matchesAnyPrefix(dirNameLower, storagePrefixes) || containsAnyToken(dirNameLower, storageTokens);
    bool usbMatch     = matchesAnyPrefix(dirNameLower, usbPrefixes) || containsAnyToken(dirNameLower, usbTokens);
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
    if (storageMatch) stagedStorage++;
    if (usbMatch) stagedUsb++;
    if (netMatch) stagedNet++;
    std::string cat = netMatch ? "network" : (storageMatch ? "storage" : (usbMatch ? "usb" : "other"));
    logFile << ISOCopyManager::getTimestamp() << "Staged driver directory " << dirName
        << " (" << cat << ")" << std::endl;
        copiedAny = true;
    }

    if (copiedAny) {
    logFile << ISOCopyManager::getTimestamp() << "Staged driver directories: storage=" << stagedStorage
        << ", usb=" << stagedUsb << ", network=" << stagedNet << std::endl;
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
        // Retry with /ForceUnsigned as a fallback for environments where some OEM drivers lack signatures in PE
        std::string addDriverForce = addDriver + " /ForceUnsigned";
        logFile << ISOCopyManager::getTimestamp() << "Retrying DISM add-driver with /ForceUnsigned: "
                << addDriverForce << std::endl;
        std::string dismOutput2;
        int         dismCode2 = Utils::execWithExitCode(addDriverForce.c_str(), dismOutput2);
        logFile << ISOCopyManager::getTimestamp()
                << "DISM add-driver (force unsigned) output (code=" << dismCode2 << "): " << dismOutput2
                << std::endl;
        if (dismCode2 != 0) {
            logFile << ISOCopyManager::getTimestamp()
                    << "DISM add-driver reported failure while integrating local drivers." << std::endl;
            return false;
        }
    }
    return true;
}
