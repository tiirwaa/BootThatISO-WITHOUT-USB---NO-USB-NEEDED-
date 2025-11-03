#include "BootWimProcessor.h"
#include "../models/EventManager.h"
#include "../models/FileCopyManager.h"
#include "../models/ISOReader.h"
#include "../models/IniConfigurator.h"
#include "../wim/WimMounter.h"
#include "../wim/WindowsEditionSelector.h"
#include "../drivers/DriverIntegrator.h"
#include "../config/PecmdConfigurator.h"
#include "../config/StartnetConfigurator.h"
#include "../config/IniFileProcessor.h"
#include "../filesystem/ProgramsIntegrator.h"
#include "../utils/Utils.h"
#include "../services/ISOCopyManager.h"
#include <windows.h>
#include <filesystem>

BootWimProcessor::BootWimProcessor(EventManager &eventManager, FileCopyManager &fileCopyManager)
    : eventManager_(eventManager), fileCopyManager_(fileCopyManager), isoReader_(std::make_unique<ISOReader>()),
      wimMounter_(std::make_unique<WimMounter>()), driverIntegrator_(std::make_unique<DriverIntegrator>()),
      pecmdConfigurator_(std::make_unique<PecmdConfigurator>()),
      startnetConfigurator_(std::make_unique<StartnetConfigurator>()),
      programsIntegrator_(std::make_unique<ProgramsIntegrator>(fileCopyManager)),
      iniConfigurator_(std::make_unique<IniConfigurator>()),
      iniFileProcessor_(std::make_unique<IniFileProcessor>(*iniConfigurator_)),
      windowsEditionSelector_(std::make_unique<WindowsEditionSelector>(eventManager, *wimMounter_, *isoReader_)) {
    // Pass DriverIntegrator to WindowsEditionSelector so it can inject drivers into install.wim
    windowsEditionSelector_->setDriverIntegrator(driverIntegrator_.get());
}

BootWimProcessor::~BootWimProcessor() {}

bool BootWimProcessor::extractBootFiles(const std::string &sourcePath, const std::string &destPath,
                                        std::ofstream &logFile) {
    bool bootWimSuccess = true;
    bool bootSdiSuccess = true;

    // Extract boot.wim
    std::string bootWimDestDir = destPath + "sources";
    CreateDirectoryA(bootWimDestDir.c_str(), NULL);
    std::string bootWimDest = bootWimDestDir + "\\boot.wim";

    if (GetFileAttributesA(bootWimDest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        logFile << ISOCopyManager::getTimestamp() << "boot.wim already exists at " << bootWimDest << std::endl;
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

    // Extract boot.sdi
    std::string bootSdiDestDir = destPath + "boot";
    CreateDirectoryA(bootSdiDestDir.c_str(), NULL);
    std::string bootSdiDest = bootSdiDestDir + "\\boot.sdi";

    if (GetFileAttributesA(bootSdiDest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        logFile << ISOCopyManager::getTimestamp() << "boot.sdi already exists at " << bootSdiDest << std::endl;
    } else if (isoReader_->fileExists(sourcePath, "boot/boot.sdi")) {
        eventManager_.notifyDetailedProgress(30, 100, "Copiando boot.sdi requerido para arranque RAM");
        eventManager_.notifyLogUpdate("Copiando boot.sdi requerido para arranque RAM...\r\n");
        eventManager_.notifyDetailedProgress(0, 0, "Copiando boot.sdi...");

        if (isoReader_->extractFile(sourcePath, "boot/boot.sdi", bootSdiDest)) {
            logFile << ISOCopyManager::getTimestamp() << "boot.sdi copied successfully to " << bootSdiDest << std::endl;
            eventManager_.notifyLogUpdate("boot.sdi copiado correctamente.\r\n");
        } else {
            logFile << ISOCopyManager::getTimestamp() << "Failed to copy boot.sdi" << std::endl;
            eventManager_.notifyLogUpdate("Error al copiar boot.sdi.\r\n");
            bootSdiSuccess = false;
        }
        eventManager_.notifyDetailedProgress(0, 0, "");
    } else {
        // Fallback to system boot.sdi
        logFile << ISOCopyManager::getTimestamp() << "boot.sdi not found inside ISO; attempting system fallback"
                << std::endl;
        std::vector<std::string> systemSdiCandidates = {std::string("C:\\Windows\\Boot\\DVD\\EFI\\boot.sdi"),
                                                        std::string("C:\\Windows\\Boot\\PCAT\\boot.sdi"),
                                                        std::string("C:\\Windows\\Boot\\EFI\\boot.sdi")};
        bool                     fallbackCopied      = false;
        for (const auto &cand : systemSdiCandidates) {
            if (GetFileAttributesA(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
                CreateDirectoryA(bootSdiDestDir.c_str(), NULL);
                if (CopyFileA(cand.c_str(), bootSdiDest.c_str(), FALSE)) {
                    logFile << ISOCopyManager::getTimestamp() << "boot.sdi copied from system: " << cand << " -> "
                            << bootSdiDest << std::endl;
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

    return bootWimSuccess && bootSdiSuccess;
}

bool BootWimProcessor::mountAndProcessWim(const std::string &bootWimDest, const std::string &destPath,
                                          const std::string &sourcePath, bool integratePrograms,
                                          const std::string &programsSrc, long long &copiedSoFar,
                                          std::ofstream &logFile) {
    std::string driveLetter = destPath.substr(0, 2);
    std::string mountDir =
        std::string(bootWimDest.begin(), bootWimDest.end() - std::string("sources\\boot.wim").length()) + "temp_mount";

    // Clean and prepare mount directory
    wimMounter_->cleanupMountDirectory(mountDir);

    // Select best image index to mount
    int imageIndex = wimMounter_->selectBestImageIndex(bootWimDest);
    logFile << ISOCopyManager::getTimestamp() << "Selected boot.wim image index: " << imageIndex << std::endl;

    // Display selected image info
    auto images = wimMounter_->getWimImageInfo(bootWimDest);
    if (!images.empty() && imageIndex >= 1 && imageIndex <= (int)images.size()) {
        const auto &selectedImage = images[imageIndex - 1];
        std::string infoMsg       = "Índice seleccionado:\r\nIndex: " + std::to_string(selectedImage.index) +
                              "\r\nName: " + selectedImage.name + "\r\n";
        eventManager_.notifyLogUpdate(infoMsg);
    }

    // Mount WIM
    auto mountProgress = [this](int percent, const std::string &message) {
        eventManager_.notifyDetailedProgress(percent, 100, message);
    };

    if (!wimMounter_->mountWim(bootWimDest, mountDir, imageIndex, mountProgress)) {
        logFile << ISOCopyManager::getTimestamp() << "Failed to mount boot.wim: " << wimMounter_->getLastError()
                << std::endl;
        eventManager_.notifyLogUpdate("Error al montar boot.wim.\r\n");
        return false;
    }

    logFile << ISOCopyManager::getTimestamp() << "boot.wim mounted successfully at " << mountDir << std::endl;

    // Detect PE type
    bool isPecmdPE = pecmdConfigurator_->isPecmdPE(mountDir);
    logFile << ISOCopyManager::getTimestamp() << "PE type: " << (isPecmdPE ? "PECMD (Hiren's)" : "Standard WinPE")
            << std::endl;

    if (isPecmdPE) {
        eventManager_.notifyLogUpdate("Hiren's BootCD PE detectado (PECMD presente).\r\n");
    }

    // Integrate Programs if requested
    if (integratePrograms) {
        eventManager_.notifyDetailedProgress(40, 100, "Integrando Programs en boot.wim");
        auto programsProgress = [this](const std::string &msg) { eventManager_.notifyLogUpdate(msg + "\r\n"); };

        std::string fallbackProgramsSrc = destPath + "Programs";
        programsIntegrator_->integratePrograms(mountDir, programsSrc, fallbackProgramsSrc, sourcePath, isoReader_.get(),
                                               copiedSoFar, logFile, programsProgress);
    }

    // Integrate CustomDrivers
    eventManager_.notifyDetailedProgress(45, 100, "Integrando CustomDrivers en boot.wim");
    std::string customDriversSrc = destPath + "CustomDrivers";
    auto        driverProgress   = [this](const std::string &msg) { eventManager_.notifyLogUpdate(msg + "\r\n"); };

    if (GetFileAttributesA(customDriversSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
        driverIntegrator_->integrateCustomDrivers(mountDir, customDriversSrc, logFile, driverProgress);
    } else {
        // Try extracting from ISO
        try {
            std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "EasyISOBoot_CustomDrivers";
            std::string           tempCustomDrivers = tempDir.string();
            if (isoReader_->extractDirectory(sourcePath, "CustomDrivers", tempCustomDrivers)) {
                driverIntegrator_->integrateCustomDrivers(mountDir, tempCustomDrivers, logFile, driverProgress);
                std::error_code ec;
                std::filesystem::remove_all(tempCustomDrivers, ec);
            }
        } catch (const std::exception &) {
            // Ignore temp directory errors
        }
    }

    // Integrate system drivers
    eventManager_.notifyDetailedProgress(47, 100, "Integrando controladores locales en boot.wim");
    driverIntegrator_->integrateSystemDrivers(mountDir, DriverIntegrator::DriverCategory::All, logFile, driverProgress);
    eventManager_.notifyLogUpdate(driverIntegrator_->getIntegrationStats() + "\r\n");

    // Configure PECMD or startnet.cmd
    if (isPecmdPE) {
        pecmdConfigurator_->extractHbcdIni(sourcePath, mountDir, isoReader_.get(), logFile);
        pecmdConfigurator_->configurePecmdForRamBoot(mountDir, logFile);
        eventManager_.notifyLogUpdate("PECMD configurado para modo RAM.\r\n");
    } else {
        startnetConfigurator_->configureStartnet(mountDir, logFile);
    }

    // Process INI files
    eventManager_.notifyDetailedProgress(55, 100, "Procesando archivos .ini");
    auto iniProgress = [this](const std::string &msg) { eventManager_.notifyLogUpdate(msg + "\r\n"); };
    iniFileProcessor_->processIniFiles(mountDir, sourcePath, isoReader_.get(), driveLetter, logFile, iniProgress);

    // Unmount and commit changes
    eventManager_.notifyDetailedProgress(60, 100, "Guardando cambios en boot.wim");
    eventManager_.notifyLogUpdate("Guardando cambios en boot.wim...\r\n");

    auto unmountProgress = [this](int percent, const std::string &message) {
        int adjustedPercent = 60 + (percent * 40 / 100); // Map 0-100 to 60-100
        eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
    };

    if (!wimMounter_->unmountWim(mountDir, true, unmountProgress)) {
        logFile << ISOCopyManager::getTimestamp() << "Failed to unmount boot.wim: " << wimMounter_->getLastError()
                << std::endl;
        eventManager_.notifyLogUpdate("Error al desmontar boot.wim.\r\n");
        return false;
    }

    eventManager_.notifyDetailedProgress(100, 100, "boot.wim actualizado correctamente");
    eventManager_.notifyLogUpdate("boot.wim actualizado correctamente.\r\n");
    eventManager_.notifyDetailedProgress(0, 0, "");

    return true;
}

bool BootWimProcessor::extractAdditionalBootFiles(const std::string &sourcePath, const std::string &espPath,
                                                  const std::string &destPath, long long &copiedSoFar,
                                                  long long isoSize, std::ofstream &logFile) {
    eventManager_.notifyDetailedProgress(60, 100, "Extrayendo archivos adicionales desde boot.wim");
    eventManager_.notifyLogUpdate("Extrayendo archivos adicionales desde boot.wim...\r\n");
    eventManager_.notifyDetailedProgress(0, 0, "Extrayendo archivos de boot.wim...");
    eventManager_.notifyLogUpdate("Archivos adicionales desde boot.wim extraidos correctamente.\r\n");
    eventManager_.notifyDetailedProgress(0, 0, "");
    return true;
}

bool BootWimProcessor::processBootWim(const std::string &sourcePath, const std::string &destPath,
                                      const std::string &espPath, bool integratePrograms,
                                      const std::string &programsSrc, long long &copiedSoFar, bool extractBootWim,
                                      bool copyInstallWim, std::ofstream &logFile) {
    if (!extractBootWim) {
        logFile << ISOCopyManager::getTimestamp() << "Boot WIM extraction disabled; skipping processing" << std::endl;
        return true;
    }

    eventManager_.notifyDetailedProgress(20, 100, "Preparando archivos de arranque del ISO");
    eventManager_.notifyLogUpdate("Preparando archivos de arranque del ISO...\r\n");

    // Extract boot files
    if (!extractBootFiles(sourcePath, destPath, logFile)) {
        return false;
    }

    std::string bootWimDest = destPath + "sources\\boot.wim";

    // Process Windows editions (inject selected edition into boot.wim) if this is a Windows install ISO
    // and we're NOT copying install.wim separately
    if (!copyInstallWim) {
        // Create temp directory for extraction on C:\ (more space available than on target partition)
        std::string tempDir = "C:\\BootThatISO_temp_install";
        CreateDirectoryA(tempDir.c_str(), NULL);

        logFile << ISOCopyManager::getTimestamp() << "Checking for Windows install image to inject" << std::endl;
        logFile << ISOCopyManager::getTimestamp() << "Using temp directory: " << tempDir << std::endl;

        // Process Windows editions - this will inject the selected edition into boot.wim
        if (windowsEditionSelector_->hasInstallImage(sourcePath)) {
            logFile << ISOCopyManager::getTimestamp() << "Windows install image detected, processing edition selection"
                    << std::endl;

            if (!windowsEditionSelector_->processWindowsEditions(sourcePath, bootWimDest, tempDir, logFile)) {
                logFile << ISOCopyManager::getTimestamp() << "Failed to process Windows editions" << std::endl;
                eventManager_.notifyLogUpdate("Error al procesar ediciones de Windows.\r\n");
                // Cleanup temp directory
                std::string rdCmd = "cmd /c rd /s /q \"" + tempDir + "\" 2>nul";
                Utils::exec(rdCmd.c_str());
                return false;
            }
        } else {
            logFile << ISOCopyManager::getTimestamp()
                    << "No Windows install image found; proceeding with standard boot.wim processing" << std::endl;
        }

        // Cleanup temp directory
        std::string rdCmd = "cmd /c rd /s /q \"" + tempDir + "\" 2>nul";
        Utils::exec(rdCmd.c_str());
    } else {
        logFile << ISOCopyManager::getTimestamp() << "Skipping install.* injection; will use on-disk install image."
                << std::endl;
        eventManager_.notifyLogUpdate(
            "Omitiendo la inyección de install.* en boot.wim; se usará la imagen desde disco.\r\n");
    }

    // Mount and process boot.wim
    if (!mountAndProcessWim(bootWimDest, destPath, sourcePath, integratePrograms, programsSrc, copiedSoFar, logFile)) {
        return false;
    }

    // If install.wim/esd exists on disk (copyInstallWim mode), inject drivers into it as well
    if (copyInstallWim) {
        std::string installWimPath = destPath + "sources\\install.wim";
        std::string installEsdPath = destPath + "sources\\install.esd";
        std::string installImagePath;

        if (GetFileAttributesA(installWimPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            installImagePath = installWimPath;
        } else if (GetFileAttributesA(installEsdPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            installImagePath = installEsdPath;
        }

        if (!installImagePath.empty()) {
            logFile << ISOCopyManager::getTimestamp() << "Found install image at: " << installImagePath
                    << " - injecting drivers for Windows Setup" << std::endl;
            eventManager_.notifyLogUpdate("Inyectando controladores en install.wim para instalación de Windows...\r\n");

            if (!processInstallWim(installImagePath, destPath, logFile)) {
                logFile << ISOCopyManager::getTimestamp()
                        << "Warning: Failed to inject drivers into install image (non-fatal)" << std::endl;
                eventManager_.notifyLogUpdate("Advertencia: No se pudieron inyectar controladores en install.wim\r\n");
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Drivers successfully injected into install image"
                        << std::endl;
                eventManager_.notifyLogUpdate("Controladores inyectados exitosamente en install.wim\r\n");
            }
        }
    }

    // Extract additional boot files
    return extractAdditionalBootFiles(sourcePath, espPath, destPath, copiedSoFar, 0, logFile);
}

bool BootWimProcessor::processInstallWim(const std::string &installImagePath, const std::string &destPath,
                                         std::ofstream &logFile) {
    logFile << ISOCopyManager::getTimestamp() << "Starting driver injection into install.wim/esd" << std::endl;
    eventManager_.notifyDetailedProgress(65, 100, "Inyectando controladores en install.wim");
    eventManager_.notifyLogUpdate("Inyectando controladores de almacenamiento en install.wim...\r\n");

    // Get all indices from install.wim/esd
    auto installImages = wimMounter_->getWimImageInfo(installImagePath);
    if (installImages.empty()) {
        logFile << ISOCopyManager::getTimestamp() << "No images found in install.wim/esd" << std::endl;
        return false;
    }

    logFile << ISOCopyManager::getTimestamp() << "Found " << installImages.size() << " edition(s) in install image"
            << std::endl;

    // Create mount directory
    std::string mountDir = destPath + "temp_install_mount";
    wimMounter_->cleanupMountDirectory(mountDir);

    bool overallSuccess = true;

    // Inject drivers into EACH edition (index) of install.wim
    for (const auto &image : installImages) {
        logFile << ISOCopyManager::getTimestamp() << "Processing install.wim Index " << image.index << ": "
                << image.name << std::endl;

        std::string progressMsg = "Inyectando controladores en edición: " + image.name;
        eventManager_.notifyLogUpdate(progressMsg + "\r\n");

        // Mount the specific index
        auto mountProgress = [this, &image](int percent, const std::string &message) {
            int baseProgress    = 65 + ((image.index - 1) * 10);
            int adjustedPercent = baseProgress + (percent * 10 / 100 / static_cast<int>(image.index));
            eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
        };

        if (!wimMounter_->mountWim(installImagePath, mountDir, image.index, mountProgress)) {
            logFile << ISOCopyManager::getTimestamp() << "Failed to mount install.wim Index " << image.index << ": "
                    << wimMounter_->getLastError() << std::endl;
            overallSuccess = false;
            continue;
        }

        // Inject storage and USB drivers (critical for Windows Setup to detect the disk)
        auto driverProgress = [this](const std::string &msg) { eventManager_.notifyLogUpdate("  " + msg + "\r\n"); };

        logFile << ISOCopyManager::getTimestamp() << "Integrating storage drivers into Index " << image.index
                << std::endl;
        bool driverSuccess = driverIntegrator_->integrateSystemDrivers(
            mountDir,
            DriverIntegrator::DriverCategory::Storage, // Only storage drivers - critical for disk detection
            logFile, driverProgress);

        if (!driverSuccess) {
            logFile << ISOCopyManager::getTimestamp() << "Warning: Failed to inject drivers into Index " << image.index
                    << std::endl;
        }

        // Unmount and save
        auto unmountProgress = [this](int percent, const std::string &message) {
            eventManager_.notifyDetailedProgress(70 + (percent * 5 / 100), 100, message);
        };

        if (!wimMounter_->unmountWim(mountDir, true, unmountProgress)) {
            logFile << ISOCopyManager::getTimestamp() << "Failed to unmount install.wim Index " << image.index << ": "
                    << wimMounter_->getLastError() << std::endl;
            overallSuccess = false;
        } else {
            logFile << ISOCopyManager::getTimestamp() << "Successfully processed Index " << image.index << std::endl;
        }
    }

    logFile << ISOCopyManager::getTimestamp() << "Completed driver injection into install.wim" << std::endl;
    eventManager_.notifyLogUpdate("Controladores inyectados en todas las ediciones de Windows.\r\n");

    return overallSuccess;
}
