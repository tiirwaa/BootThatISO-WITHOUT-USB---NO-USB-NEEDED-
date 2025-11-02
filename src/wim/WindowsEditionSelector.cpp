#include "WindowsEditionSelector.h"
#include "WimMounter.h"
#include "../models/ISOReader.h"
#include "../models/EventManager.h"
#include "../views/EditionSelectorDialog.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include <windows.h>
#include <sstream>
#include <iomanip>

WindowsEditionSelector::WindowsEditionSelector(EventManager &eventManager, WimMounter &wimMounter, ISOReader &isoReader)
    : eventManager_(eventManager), wimMounter_(wimMounter), isoReader_(isoReader), isEsd_(false) {}

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
    eventManager_.notifyLogUpdate("Extrayendo imagen de instalación de Windows...\r\n");

    if (!isoReader_.extractFile(isoPath, sourceFile, destFile)) {
        logFile << "[WindowsEditionSelector] Failed to extract " << sourceFile << std::endl;
        eventManager_.notifyLogUpdate("Error al extraer imagen de instalación.\r\n");
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
        eventManager_.notifyLogUpdate("No se encontraron ediciones de Windows.\r\n");
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
                    eventManager_.notifyLogUpdate("Seleccionada edición: " + ed.name + "\r\n");
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
        eventManager_.notifyLogUpdate("Solo hay una edición disponible, seleccionando automáticamente.\r\n");
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

bool WindowsEditionSelector::exportSelectedEditions(const std::string &sourceInstallPath,
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
            int totalEditions = static_cast<int>(selectedIndices.size());
            int baseProgress = 35;
            int rangeProgress = 25; // 25% range for export (slow operation)
            int currentEditionProgress = static_cast<int>((static_cast<double>(i) / totalEditions) * rangeProgress);
            int individualProgress = static_cast<int>((static_cast<double>(percent) / totalEditions / 100.0) * rangeProgress);
            int overallPercent = baseProgress + currentEditionProgress + individualProgress;
            
            std::string msg = "Exportando edición " + std::to_string(srcIndex) + " (" + 
                             std::to_string(static_cast<int>(i) + 1) + "/" + std::to_string(totalEditions) + ")";
            eventManager_.notifyDetailedProgress(overallPercent, 100, msg);
        };

        bool success = wimMounter_.exportWimIndex(sourceInstallPath, srcIndex, destInstallPath, destIndex, exportProgress);

        if (!success) {
            logFile << "[WindowsEditionSelector] Failed to export index " << srcIndex << ": "
                    << wimMounter_.getLastError() << std::endl;
            return false;
        }
    }

    logFile << "[WindowsEditionSelector] Successfully exported " << selectedIndices.size() << " edition(s)" << std::endl;
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
    eventManager_.notifyDetailedProgress(35, 100, "Preparando imagen de instalación para RAM boot");
    eventManager_.notifyLogUpdate("Preparando edición seleccionada para arranque RAM...\r\n");

    // Step 1: Create a new install.wim with only the selected edition
    // Extract boot.wim path to get the destination partition
    std::string bootWimDir = bootWimPath.substr(0, bootWimPath.find_last_of("\\/"));
    std::string destPartition = bootWimDir.substr(0, bootWimDir.find("sources"));
    std::string destInstallPath = destPartition + "sources\\install.wim";
    
    std::vector<int> selectedIndices = {selectedIndex};

    logFile << "[WindowsEditionSelector] Creating filtered install.wim with only selected edition" << std::endl;
    logFile << "[WindowsEditionSelector] Destination: " << destInstallPath << std::endl;
    eventManager_.notifyLogUpdate("Creando imagen de instalación filtrada...\r\n");

    // Export directly to the destination to save space
    if (!exportSelectedEditions(installImagePath_, selectedIndices, destInstallPath, logFile)) {
        logFile << "[WindowsEditionSelector] Failed to create filtered install image" << std::endl;
        eventManager_.notifyLogUpdate("Error al crear imagen filtrada.\r\n");
        return false;
    }

    // Step 2: Mount boot.wim Index 2 (Windows Setup) to modify startnet.cmd
    std::string mountDir = tempDir + "\\boot_wim_mount";
    CreateDirectoryA(mountDir.c_str(), NULL);

    logFile << "[WindowsEditionSelector] Mounting boot.wim Index 2 (Windows Setup) to configure boot script" << std::endl;
    eventManager_.notifyDetailedProgress(70, 100, "Configurando boot.wim para RAM boot");
    eventManager_.notifyLogUpdate("Configurando entorno de arranque...\r\n");

    auto mountProgress = [this](int percent, const std::string &message) {
        int adjustedPercent = 70 + (percent * 5 / 100); // Map 0-100 to 70-75
        eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
    };

    if (!wimMounter_.mountWim(bootWimPath, mountDir, 2, mountProgress)) {
        logFile << "[WindowsEditionSelector] Failed to mount boot.wim Index 2: " << wimMounter_.getLastError()
                << std::endl;
        eventManager_.notifyLogUpdate("Error al montar boot.wim.\r\n");
        DeleteFileA(destInstallPath.c_str());
        return false;
    }

    // Step 3: Modify startnet.cmd to assign drive letter and launch setup
    std::string startnetPath = mountDir + "\\Windows\\System32\\startnet.cmd";
    std::string sourcesPath = mountDir + "\\sources";
    CreateDirectoryA(sourcesPath.c_str(), NULL);
    
    logFile << "[WindowsEditionSelector] Modifying startnet.cmd to mount install partition" << std::endl;
    
    std::ofstream startnet(startnetPath, std::ios::trunc);
    if (startnet.is_open()) {
        startnet << "@echo off\r\n";
        startnet << "echo Initializing Windows Setup from RAM...\r\n";
        startnet << "wpeinit\r\n";
        startnet << "echo Mounting install partition...\r\n";
        // Assign drive letter to the NTFS data partition containing install.wim/esd
        startnet << "for /f \"tokens=2\" %%a in ('echo list volume ^| diskpart ^| findstr /C:\"NTFS\" ^| findstr /V /C:\"Hidden\"') do (\r\n";
        startnet << "  if exist %%a:\\sources\\install.wim (\r\n";
        startnet << "    set INSTALL_DRIVE=%%a:\r\n";
        startnet << "    goto :found\r\n";
        startnet << "  )\r\n";
        startnet << "  if exist %%a:\\sources\\install.esd (\r\n";
        startnet << "    set INSTALL_DRIVE=%%a:\r\n";
        startnet << "    goto :found\r\n";
        startnet << "  )\r\n";
        startnet << ")\r\n";
        startnet << ":found\r\n";
        startnet << "if defined INSTALL_DRIVE (\r\n";
        startnet << "  echo Install image found on %INSTALL_DRIVE%\r\n";
        startnet << "  cd /d %INSTALL_DRIVE%\\sources\r\n";
        startnet << "  start setup.exe\r\n";
        startnet << ") else (\r\n";
        startnet << "  echo ERROR: Install image not found!\r\n";
        startnet << "  cmd\r\n";
        startnet << ")\r\n";
        startnet.close();
        logFile << "[WindowsEditionSelector] startnet.cmd created successfully" << std::endl;
    } else {
        logFile << "[WindowsEditionSelector] Failed to create startnet.cmd" << std::endl;
        wimMounter_.unmountWim(mountDir, false, nullptr);
        DeleteFileA(destInstallPath.c_str());
        return false;
    }

    // Step 4: Unmount and save changes
    logFile << "[WindowsEditionSelector] Saving changes to boot.wim" << std::endl;
    eventManager_.notifyDetailedProgress(75, 100, "Guardando cambios en boot.wim");
    eventManager_.notifyLogUpdate("Guardando configuración...\r\n");

    auto unmountProgress = [this](int percent, const std::string &message) {
        int adjustedPercent = 75 + (percent * 25 / 100); // Map 0-100 to 75-100
        eventManager_.notifyDetailedProgress(adjustedPercent, 100, message);
    };

    bool success = wimMounter_.unmountWim(mountDir, true, unmountProgress);

    if (success) {
        logFile << "[WindowsEditionSelector] Successfully configured Windows Setup for RAM boot" << std::endl;
        logFile << "[WindowsEditionSelector] install.wim location: " << destInstallPath << std::endl;
        eventManager_.notifyLogUpdate("Edición de Windows preparada correctamente.\r\n");
        eventManager_.notifyLogUpdate("Windows Setup buscará install.wim en la partición de datos.\r\n");
    } else {
        logFile << "[WindowsEditionSelector] Failed to save boot.wim changes: " << wimMounter_.getLastError() << std::endl;
        eventManager_.notifyLogUpdate("Error al guardar configuración.\r\n");
        DeleteFileA(destInstallPath.c_str());
    }

    return success;
}

bool WindowsEditionSelector::processWindowsEditions(const std::string &isoPath, const std::string &bootWimPath,
                                                    const std::string &tempDir, std::ofstream &logFile) {
    logFile << "[WindowsEditionSelector] Starting Windows edition selection process" << std::endl;

    // Check if install image exists
    if (!hasInstallImage(isoPath)) {
        logFile << "[WindowsEditionSelector] No install image found, skipping edition selection" << std::endl;
        return true; // Not an error, just not a Windows install ISO
    }

    eventManager_.notifyLogUpdate("Detectado ISO de instalación de Windows.\r\n");

    // Get available editions
    auto editions = getAvailableEditions(isoPath, tempDir, logFile);
    if (editions.empty()) {
        logFile << "[WindowsEditionSelector] No editions found" << std::endl;
        return false;
    }

    // Prompt user to select edition
    int selectedIndex = promptUserSelection(editions, logFile);
    if (selectedIndex == 0) {
        logFile << "[WindowsEditionSelector] User cancelled or invalid selection" << std::endl;
        return false;
    }

    // Inject selected edition into boot.wim
    bool success = injectEditionIntoBootWim(isoPath, bootWimPath, selectedIndex, tempDir, logFile);

    // Cleanup extracted install image
    if (!installImagePath_.empty() && GetFileAttributesA(installImagePath_.c_str()) != INVALID_FILE_ATTRIBUTES) {
        logFile << "[WindowsEditionSelector] Cleaning up extracted install image" << std::endl;
        DeleteFileA(installImagePath_.c_str());
        installImagePath_.clear();
    }

    return success;
}
