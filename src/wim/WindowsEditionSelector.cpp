#include "WindowsEditionSelector.h"
#include "WimMounter.h"
#include "../models/ISOReader.h"
#include "../models/EventManager.h"
#include "../views/EditionSelectorDialog.h"
#include "../drivers/DriverIntegrator.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"
#include <windows.h>
#include <sstream>
#include <iomanip>

WindowsEditionSelector::WindowsEditionSelector(EventManager &eventManager, WimMounter &wimMounter, ISOReader &isoReader)
    : eventManager_(eventManager), wimMounter_(wimMounter), isoReader_(isoReader), driverIntegrator_(nullptr),
      isEsd_(false) {}

WindowsEditionSelector::~WindowsEditionSelector() {
    // Cleanup extracted install image if it exists
    if (!installImagePath_.empty() && GetFileAttributesA(installImagePath_.c_str()) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(installImagePath_.c_str());
    }
}

bool WindowsEditionSelector::hasInstallImage(const std::string &isoPath) {
    return isoReader_.fileExists(isoPath, "sources/install.wim") ||
           isoReader_.fileExists(isoPath, "sources/install.esd");
}

std::string WindowsEditionSelector::extractInstallImage(const std::string &isoPath, const std::string &destDir,
                                                        std::ofstream &logFile) {
    // Check if install.esd exists (preferred for modern Windows ISOs)
    bool hasEsd = isoReader_.fileExists(isoPath, "sources/install.esd");
    bool hasWim = isoReader_.fileExists(isoPath, "sources/install.wim");

    if (!hasEsd && !hasWim) {
        logFile << "[WindowsEditionSelector] No install.wim or install.esd found in ISO" << std::endl;
        return "";
    }

    isEsd_                 = hasEsd;
    std::string sourceFile = isEsd_ ? "sources/install.esd" : "sources/install.wim";
    std::string destFile   = destDir + "\\" + (isEsd_ ? "install.esd" : "install.wim");
    installImagePath_      = destFile;

    logFile << "[WindowsEditionSelector] Extracting " << sourceFile << " from ISO..." << std::endl;
    eventManager_.notifyLogUpdate(
        LocalizedOrUtf8("log.edition.extracting", "Extrayendo imagen de instalación de Windows...") + "\r\n");

    if (!isoReader_.extractFile(isoPath, sourceFile, destFile)) {
        logFile << "[WindowsEditionSelector] Failed to extract " << sourceFile << std::endl;
        eventManager_.notifyLogUpdate(
            LocalizedOrUtf8("log.edition.extractError", "Error al extraer imagen de instalación.") + "\r\n");
        return "";
    }

    logFile << "[WindowsEditionSelector] Successfully extracted to " << destFile << std::endl;
    return destFile;
}

std::string WindowsEditionSelector::formatSize(long long bytes) {
    const double GB = 1024.0 * 1024.0 * 1024.0;
    const double MB = 1024.0 * 1024.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= GB) {
        oss << (bytes / GB) << " GB";
    } else if (bytes >= MB) {
        oss << (bytes / MB) << " MB";
    } else {
        oss << bytes << " bytes";
    }

    return oss.str();
}

std::vector<WindowsEditionSelector::WindowsEdition>
WindowsEditionSelector::getAvailableEditions(const std::string &isoPath, const std::string &tempDir,
                                             std::ofstream &logFile) {
    std::vector<WindowsEdition> editions;

    // Extract install.wim/esd if not already done
    if (installImagePath_.empty()) {
        installImagePath_ = extractInstallImage(isoPath, tempDir, logFile);
        if (installImagePath_.empty()) {
            return editions;
        }
    }

    // Get image info using WimMounter
    logFile << "[WindowsEditionSelector] Reading Windows editions from " << installImagePath_ << std::endl;
    auto wimImages = wimMounter_.getWimImageInfo(installImagePath_);

    if (wimImages.empty()) {
        logFile << "[WindowsEditionSelector] No editions found in install image" << std::endl;
        eventManager_.notifyLogUpdate(
            LocalizedOrUtf8("log.edition.noEditionsFound", "No se encontraron ediciones de Windows.") + "\r\n");
        return editions;
    }

    // Convert WimImageInfo to WindowsEdition
    for (const auto &img : wimImages) {
        WindowsEdition edition;
        edition.index       = img.index;
        edition.name        = img.name;
        edition.description = img.description;
        edition.size        = img.size; // Use actual size from DISM

        editions.push_back(edition);

        logFile << "[WindowsEditionSelector] Found edition " << edition.index << ": " << edition.name;
        if (edition.size > 0) {
            logFile << " (Size: " << formatSize(edition.size) << ")";
        }
        logFile << std::endl;
    }

    return editions;
}

int WindowsEditionSelector::promptUserSelection(const std::vector<WindowsEdition> &editions, std::ofstream &logFile) {
    if (editions.empty()) {
        return 0;
    }

    // If only one edition, auto-select it
    if (editions.size() == 1) {
        logFile << "[WindowsEditionSelector] Only one edition available, auto-selecting index 1" << std::endl;
        eventManager_.notifyLogUpdate("Solo hay una edición disponible, seleccionando automáticamente.\r\n");
        return 1;
    }

    // Show graphical dialog for selection
    logFile << "[WindowsEditionSelector] Showing edition selection dialog" << std::endl;

    std::vector<int> selectedIndices;
    HINSTANCE        hInstance = GetModuleHandle(NULL);

    // Get LocalizationManager instance
    LocalizationManager *locManager = &LocalizationManager::getInstance();

    // Find main window to use as parent (for proper modal behavior and centering)
    HWND hMainWindow = FindWindowW(L"BootThatISOClass", NULL);

    if (EditionSelectorDialog::show(hInstance, hMainWindow, editions, selectedIndices, locManager)) {
        if (!selectedIndices.empty()) {
            int firstSelected = selectedIndices[0];
            logFile << "[WindowsEditionSelector] User selected edition index: " << firstSelected << std::endl;

            // Find edition name for logging
            for (const auto &ed : editions) {
                if (ed.index == firstSelected) {
                    std::string message = LocalizedOrUtf8("log.edition.selected", "Seleccionada edición: {0}");
                    size_t      pos     = message.find("{0}");
                    if (pos != std::string::npos) {
                        message.replace(pos, 3, ed.name);
                    }
                    eventManager_.notifyLogUpdate(message + "\r\n");
                    break;
                }
            }

            return firstSelected;
        }
    }

    logFile << "[WindowsEditionSelector] User cancelled selection" << std::endl;
    return 0;
}

bool WindowsEditionSelector::promptUserMultiSelection(const std::vector<WindowsEdition> &editions,
                                                      std::vector<int> &selectedIndices, std::ofstream &logFile) {
    if (editions.empty()) {
        return false;
    }

    // If only one edition, auto-select it
    if (editions.size() == 1) {
        logFile << "[WindowsEditionSelector] Only one edition available, auto-selecting" << std::endl;
        eventManager_.notifyLogUpdate(
            LocalizedOrUtf8("log.edition.autoSelectingSingle",
                            "Solo hay una edición disponible, seleccionando automáticamente.") +
            "\r\n");
        selectedIndices.push_back(1);
        return true;
    }

    // Show graphical dialog for multi-selection
    logFile << "[WindowsEditionSelector] Showing edition multi-selection dialog" << std::endl;

    HINSTANCE hInstance = GetModuleHandle(NULL);

    // Get LocalizationManager instance
    LocalizationManager *locManager = &LocalizationManager::getInstance();

    // Find main window to use as parent (for proper modal behavior and centering)
    HWND hMainWindow = FindWindowW(L"BootThatISOClass", NULL);

    if (EditionSelectorDialog::show(hInstance, hMainWindow, editions, selectedIndices, locManager)) {
        if (!selectedIndices.empty()) {
            logFile << "[WindowsEditionSelector] User selected " << selectedIndices.size() << " edition(s)"
                    << std::endl;

            std::string msg = "Seleccionadas " + std::to_string(selectedIndices.size()) + " edición(es):\r\n";
            for (int idx : selectedIndices) {
                for (const auto &ed : editions) {
                    if (ed.index == idx) {
                        msg += "  - " + ed.name + "\r\n";
                        logFile << "[WindowsEditionSelector]   Index " << idx << ": " << ed.name << std::endl;
                        break;
                    }
                }
            }
            eventManager_.notifyLogUpdate(msg);

            return true;
        }
    }

    logFile << "[WindowsEditionSelector] User cancelled selection" << std::endl;
    return false;
}

bool WindowsEditionSelector::exportSelectedEditions(const std::string      &sourceInstallPath,
                                                    const std::vector<int> &selectedIndices,
                                                    const std::string &destInstallPath, std::ofstream &logFile) {
    logFile << "[WindowsEditionSelector] Exporting selected editions to new install image" << std::endl;

    // Delete destination file if it exists
    if (GetFileAttributesA(destInstallPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(destInstallPath.c_str());
    }

    // Export each selected index
    for (size_t i = 0; i < selectedIndices.size(); ++i) {
        int srcIndex  = selectedIndices[i];
        int destIndex = static_cast<int>(i) + 1; // 1-based indexing

        logFile << "[WindowsEditionSelector] Exporting index " << srcIndex << " as index " << destIndex << std::endl;

        auto exportProgress = [this, srcIndex, i, &selectedIndices](int percent, const std::string &message) {
            // Calculate overall progress (35-60% range for export - this is the slowest operation)
            int totalEditions          = static_cast<int>(selectedIndices.size());
            int baseProgress           = 35;
            int rangeProgress          = 25; // 25% range for export (slow operation)
            int currentEditionProgress = static_cast<int>((static_cast<double>(i) / totalEditions) * rangeProgress);
            int individualProgress =
                static_cast<int>((static_cast<double>(percent) / totalEditions / 100.0) * rangeProgress);
            int overallPercent = baseProgress + currentEditionProgress + individualProgress;

            std::string msg = "Exportando edición " + std::to_string(srcIndex) + " (" +
                              std::to_string(static_cast<int>(i) + 1) + "/" + std::to_string(totalEditions) + ")";
            eventManager_.notifyDetailedProgress(overallPercent, 100, msg);
        };

        bool success =
            wimMounter_.exportWimIndex(sourceInstallPath, srcIndex, destInstallPath, destIndex, exportProgress);

        if (!success) {
            logFile << "[WindowsEditionSelector] Failed to export index " << srcIndex << ": "
                    << wimMounter_.getLastError() << std::endl;
            return false;
        }
    }

    logFile << "[WindowsEditionSelector] Successfully exported " << selectedIndices.size() << " edition(s)"
            << std::endl;
    return true;
}

bool WindowsEditionSelector::injectEditionIntoBootWim(const std::string &isoPath, const std::string &bootWimPath,
                                                      int selectedIndex, const std::string &tempDir,
                                                      std::ofstream &logFile) {
    // Ensure install image is extracted
    if (installImagePath_.empty()) {
        installImagePath_ = extractInstallImage(isoPath, tempDir, logFile);
        if (installImagePath_.empty()) {
            return false;
        }
    }

    logFile << "[WindowsEditionSelector] Preparing to create filtered Windows install image for RAM boot" << std::endl;
    eventManager_.notifyDetailedProgress(
        35, 100, LocalizedOrUtf8("log.edition.preparingRAM", "Preparando edición seleccionada para arranque RAM..."));
    eventManager_.notifyLogUpdate(
        LocalizedOrUtf8("log.edition.preparingRAM", "Preparando edición seleccionada para arranque RAM...") + "\r\n");

    // Step 1: Create a new install.wim with only the selected edition
    // Extract boot.wim path to get the destination partition
    std::string bootWimDir      = bootWimPath.substr(0, bootWimPath.find_last_of("\\/"));
    std::string destPartition   = bootWimDir.substr(0, bootWimDir.find("sources"));
    std::string destInstallPath = destPartition + "sources\\install.wim";

    std::vector<int> selectedIndices = {selectedIndex};

    logFile << "[WindowsEditionSelector] Creating filtered install.wim with only selected edition" << std::endl;
    logFile << "[WindowsEditionSelector] Destination: " << destInstallPath << std::endl;
    eventManager_.notifyLogUpdate(
        LocalizedOrUtf8("log.edition.creatingFiltered", "Creando imagen de instalación filtrada...") + "\r\n");

    // Export directly to the destination to save space
    if (!exportSelectedEditions(installImagePath_, selectedIndices, destInstallPath, logFile)) {
        logFile << "[WindowsEditionSelector] Failed to create filtered install image" << std::endl;
        eventManager_.notifyLogUpdate(LocalizedOrUtf8("log.edition.filteredError", "Error al crear imagen filtrada.") +
                                      "\r\n");
        return false;
    }

    // Step 1.5: Inject storage drivers into the filtered install.wim
    if (driverIntegrator_) {
        logFile << "[WindowsEditionSelector] Injecting storage drivers into filtered install.wim" << std::endl;
        eventManager_.notifyDetailedProgress(
            62, 100, LocalizedOrUtf8("log.edition.injectingDrivers", "Inyectando controladores en install.wim"));
        eventManager_.notifyLogUpdate(LocalizedOrUtf8("log.edition.injectingDrivers",
                                                      "Inyectando controladores de almacenamiento en install.wim...") +
                                      "\r\n");

        std::string installMountDir = tempDir + "\\install_wim_mount";
        CreateDirectoryA(installMountDir.c_str(), NULL);

        auto mountProgress = [this](int percent, const std::string &message) {
            int adjustedPercent = 62 + (percent * 3 / 100); // Map 0-100 to 62-65
            eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
        };

        // Mount the filtered install.wim (it only has 1 index now)
        if (wimMounter_.mountWim(destInstallPath, installMountDir, 1, mountProgress)) {
            // Inject storage drivers
            auto driverProgress = [this](const std::string &msg) {
                eventManager_.notifyLogUpdate("  " + msg + "\r\n");
            };

            driverIntegrator_->integrateSystemDrivers(
                installMountDir,
                DriverIntegrator::DriverCategory::Storage, // Critical for disk detection
                logFile, driverProgress);

            // Unmount and save
            auto unmountProgress = [this](int percent, const std::string &message) {
                int adjustedPercent = 65 + (percent * 5 / 100); // Map 0-100 to 65-70
                eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
            };

            if (wimMounter_.unmountWim(installMountDir, true, unmountProgress)) {
                logFile << "[WindowsEditionSelector] Storage drivers successfully injected into install.wim"
                        << std::endl;
                eventManager_.notifyLogUpdate(
                    LocalizedOrUtf8("log.edition.driversInjected", "Controladores inyectados en install.wim.") +
                    "\r\n");
            } else {
                logFile << "[WindowsEditionSelector] Warning: Failed to save install.wim with drivers" << std::endl;
                eventManager_.notifyLogUpdate(
                    LocalizedOrUtf8("log.edition.driversSaveFailed",
                                    "Advertencia: No se pudieron guardar los controladores.") +
                    "\r\n");
            }
        } else {
            logFile << "[WindowsEditionSelector] Warning: Failed to mount install.wim for driver injection"
                    << std::endl;
            eventManager_.notifyLogUpdate(
                LocalizedOrUtf8("log.edition.driversInjectFailed",
                                "Advertencia: No se pudo inyectar controladores en install.wim.") +
                "\r\n");
        }
    } else {
        logFile << "[WindowsEditionSelector] No DriverIntegrator available, skipping driver injection" << std::endl;
    }

    // Step 2: Mount boot.wim Index 2 (Windows Setup) to modify startnet.cmd and inject drivers
    std::string mountDir = tempDir + "\\boot_wim_mount";
    CreateDirectoryA(mountDir.c_str(), NULL);

    logFile << "[WindowsEditionSelector] Mounting boot.wim Index 2 (Windows Setup) to configure boot script"
            << std::endl;
    eventManager_.notifyDetailedProgress(
        70, 100, LocalizedOrUtf8("log.edition.configuringBoot", "Configurando entorno de arranque..."));
    eventManager_.notifyLogUpdate(
        LocalizedOrUtf8("log.edition.configuringBoot", "Configurando entorno de arranque...") + "\r\n");

    auto mountProgress = [this](int percent, const std::string &message) {
        int adjustedPercent = 70 + (percent * 5 / 100); // Map 0-100 to 70-75
        eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
    };

    if (!wimMounter_.mountWim(bootWimPath, mountDir, 2, mountProgress)) {
        logFile << "[WindowsEditionSelector] Failed to mount boot.wim Index 2: " << wimMounter_.getLastError()
                << std::endl;
        eventManager_.notifyLogUpdate(LocalizedOrUtf8("log.edition.bootMountError", "Error al montar boot.wim.") +
                                      "\r\n");
        DeleteFileA(destInstallPath.c_str());
        return false;
    }

    // Step 3: Modify startnet.cmd to assign drive letter and launch setup from disk
    std::string startnetPath = mountDir + "\\Windows\\System32\\startnet.cmd";

    logFile << "[WindowsEditionSelector] Modifying startnet.cmd to find and mount install partition" << std::endl;

    std::ofstream startnet(startnetPath, std::ios::trunc);
    if (startnet.is_open()) {
        startnet << "@echo off\r\n";
        startnet << "echo Initializing Windows Setup from RAM...\r\n";
        startnet << "wpeinit\r\n";
        startnet << "echo.\r\n";
        startnet << "echo Searching for Windows installation files...\r\n";
        startnet << "echo.\r\n";
        startnet << "\r\n";
        startnet << "REM Try to find the drive containing install.wim\r\n";
        startnet << "for %%d in (C D E F G H I J K L M N O P Q R S T U V W X Y Z) do (\r\n";
        startnet << "  if exist %%d:\\sources\\install.wim (\r\n";
        startnet << "    echo Found install.wim on %%d:\r\n";
        startnet << "    cd /d %%d:\\sources\r\n";
        startnet << "    start setup.exe\r\n";
        startnet << "    goto :eof\r\n";
        startnet << "  )\r\n";
        startnet << "  if exist %%d:\\sources\\install.esd (\r\n";
        startnet << "    echo Found install.esd on %%d:\r\n";
        startnet << "    cd /d %%d:\\sources\r\n";
        startnet << "    start setup.exe\r\n";
        startnet << "    goto :eof\r\n";
        startnet << "  )\r\n";
        startnet << ")\r\n";
        startnet << "\r\n";
        startnet << "echo ERROR: Could not find Windows installation files!\r\n";
        startnet << "echo The storage drivers may not have been loaded correctly.\r\n";
        startnet << "echo.\r\n";
        startnet << "pause\r\n";
        startnet << "cmd\r\n";
        startnet.close();
        logFile << "[WindowsEditionSelector] startnet.cmd configured successfully" << std::endl;
    } else {
        logFile << "[WindowsEditionSelector] Failed to create startnet.cmd" << std::endl;
        wimMounter_.unmountWim(mountDir, false, nullptr);
        DeleteFileA(destInstallPath.c_str());
        return false;
    }

    // Step 4: Unmount and save changes
    logFile << "[WindowsEditionSelector] Saving changes to boot.wim" << std::endl;
    eventManager_.notifyDetailedProgress(75, 100,
                                         LocalizedOrUtf8("log.edition.savingConfig", "Guardando configuración..."));
    eventManager_.notifyLogUpdate(LocalizedOrUtf8("log.edition.savingConfig", "Guardando configuración...") + "\r\n");

    auto unmountProgress = [this](int percent, const std::string &message) {
        int adjustedPercent = 75 + (percent * 25 / 100); // Map 0-100 to 75-100
        eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
    };

    bool success = wimMounter_.unmountWim(mountDir, true, unmountProgress);

    if (success) {
        logFile << "[WindowsEditionSelector] Successfully configured Windows Setup for RAM boot" << std::endl;
        logFile << "[WindowsEditionSelector] install.wim location: " << destInstallPath << std::endl;
        eventManager_.notifyLogUpdate(
            LocalizedOrUtf8("log.edition.preparedSuccess", "Edición de Windows preparada correctamente.") + "\r\n");
        eventManager_.notifyLogUpdate(LocalizedOrUtf8("log.edition.setupWillSearch",
                                                      "Windows Setup buscará install.wim en la partición de datos.") +
                                      "\r\n");
    } else {
        logFile << "[WindowsEditionSelector] Failed to save boot.wim changes: " << wimMounter_.getLastError()
                << std::endl;
        eventManager_.notifyLogUpdate(
            LocalizedOrUtf8("log.edition.saveConfigError", "Error al guardar configuración.") + "\r\n");
        DeleteFileA(destInstallPath.c_str());
    }

    return success;
}

bool WindowsEditionSelector::processWindowsEditions(const std::string &isoPath, const std::string &bootWimPath,
                                                    const std::string &tempDir, std::ofstream &logFile) {
    logFile << "[WindowsEditionSelector] Starting Windows Install ISO processing (simplified mode)" << std::endl;

    // Check if install image exists
    if (!hasInstallImage(isoPath)) {
        logFile << "[WindowsEditionSelector] No install image found, skipping" << std::endl;
        return true; // Not an error, just not a Windows install ISO
    }

    eventManager_.notifyLogUpdate(
        LocalizedOrUtf8("log.edition.windowsIsoDetected", "Detectado ISO de instalación de Windows.") + "\r\n");

    // Extract boot.wim path to get the destination data partition
    std::string bootWimDir    = bootWimPath.substr(0, bootWimPath.find_last_of("\\/"));
    std::string destPartition = bootWimDir.substr(0, bootWimDir.find("sources"));
    std::string sourcesDir    = destPartition + "sources";

    // Detect install.esd vs install.wim
    bool hasEsd = isoReader_.fileExists(isoPath, "sources/install.esd");
    bool hasWim = isoReader_.fileExists(isoPath, "sources/install.wim");

    if (!hasEsd && !hasWim) {
        logFile << "[WindowsEditionSelector] ERROR: No install.wim/esd found" << std::endl;
        return false;
    }

    std::string sourceFile = hasEsd ? "sources/install.esd" : "sources/install.wim";
    std::string destFile   = sourcesDir + "\\" + (hasEsd ? "install.esd" : "install.wim");

    // Copy install.wim/esd COMPLETE (all editions) to Z:\sources\
    logFile << "[WindowsEditionSelector] Copying COMPLETE " << sourceFile << " to data partition" << std::endl;
    eventManager_.notifyLogUpdate(LocalizedOrUtf8("log.edition.copyingFullImage",
                                                  "Copiando imagen de instalación completa (todas las ediciones)...") +
                                  "\r\n");

    // Get file size to calculate progress
    unsigned long long fileSize = 0;
    isoReader_.getFileSize(isoPath, sourceFile, fileSize);

    // Setup progress callback (35% to 60% of total progress)
    auto progressCallback = [this, fileSize](unsigned long long completed, unsigned long long total) {
        if (total == 0)
            total = fileSize; // Fallback to pre-calculated size
        if (total > 0) {
            int         percentage     = static_cast<int>((completed * 100) / total);
            int         mappedProgress = 35 + (percentage * 25 / 100); // Map 0-100% to 35-60%
            std::string sizeStr =
                std::to_string(completed / (1024 * 1024)) + " MB / " + std::to_string(total / (1024 * 1024)) + " MB";
            eventManager_.notifyDetailedProgress(mappedProgress, 100, "Copiando install.wim/esd: " + sizeStr);
        }
    };

    eventManager_.notifyDetailedProgress(35, 100, "Iniciando copia de install.wim/esd...");

    if (!isoReader_.extractFile(isoPath, sourceFile, destFile, progressCallback)) {
        logFile << "[WindowsEditionSelector] ERROR: Failed to copy " << sourceFile << std::endl;
        eventManager_.notifyLogUpdate(
            LocalizedOrUtf8("log.edition.copyImageError", "Error al copiar imagen de instalación.") + "\r\n");
        return false;
    }

    logFile << "[WindowsEditionSelector] Successfully copied complete install image to: " << destFile << std::endl;
    logFile << "[WindowsEditionSelector] Windows Setup will display edition selection screen" << std::endl;
    eventManager_.notifyLogUpdate(
        LocalizedOrUtf8("log.edition.imageCopiedSuccess",
                        "Imagen de instalación copiada. Windows Setup mostrará lista de ediciones.") +
        "\r\n");

    // TODO: If injectDrivers flag is true, mount ALL editions and inject drivers (slow but thorough)
    // For now, skip driver injection - let user enable it via checkbox

    return true;
}
