#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <memory>
#include <cwctype>
#include "controllers/ProcessController.h"
#include "models/EventManager.h"
#include "models/ConsoleObserver.h"
#include "services/partitionmanager.h"
#include "services/isocopymanager.h"
#include "services/bcdmanager.h"
#include "utils/Utils.h"
#include "utils/constants.h"
#include "utils/LocalizationManager.h"
#include "utils/LocalizationHelpers.h"
#include "utils/AppKeys.h"
#include "utils/Logger.h"

// Función para detectar el disco del sistema disponible
std::string detectSystemDrive() {
    // Buscar el disco donde está instalado Windows (normalmente C:)
    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        std::string driveRoot = std::string(1, drive) + ":\\";
        UINT        driveType = GetDriveTypeA(driveRoot.c_str());

        if (driveType == DRIVE_FIXED) {
            // Verificar que el drive esté accesible y tenga espacio
            DWORD sectorsPerCluster, bytesPerSector, numberOfFreeClusters, totalNumberOfClusters;
            if (GetDiskFreeSpaceA(driveRoot.c_str(), &sectorsPerCluster, &bytesPerSector, &numberOfFreeClusters,
                                  &totalNumberOfClusters)) {
                // Verificar que haya al menos 1GB de espacio libre
                unsigned long long freeSpace =
                    (unsigned long long)sectorsPerCluster * bytesPerSector * numberOfFreeClusters;
                if (freeSpace > 1024LL * 1024 * 1024) { // 1GB
                    return driveRoot;
                }
            }
        }
    }

    return ""; // No se encontró un disco del sistema adecuado
}

BOOL IsRunAsAdmin() {
    BOOL   bElevated = FALSE;
    HANDLE hToken    = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD           dwSize;
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            bElevated = elevation.TokenIsElevated;
        }
    }
    if (hToken) {
        CloseHandle(hToken);
    }
    return bElevated;
}

void ClearLogs() {
    Logger::instance().resetProcessLogs();
}

int main(int argc, char *argv[]) {
    LocalizationManager &localization = LocalizationManager::getInstance();
    std::string          exeDir       = Utils::getExeDirectory();
    std::string          langDir      = exeDir + "lang\\";
    if (!localization.initialize() || !localization.hasLanguages()) {
        std::cout << "Error: No language files found in 'lang' directory." << std::endl;
        return 1;
    }
    localization.loadLanguageByIndex(0);

    bool        unattended = false;
    std::string isoPath;
    std::string modeKey;
    std::string format;
    bool        chkdsk     = false;
    bool        autoreboot = false;
    std::string languageCodeArg;

    std::cout << "Procesando " << argc << " argumentos" << std::endl;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::cout << "Arg " << i << ": " << arg << std::endl;
        if (arg == "-unattended") {
            unattended = true;
        } else if (arg.rfind("-iso=", 0) == 0) {
            isoPath = arg.substr(5);
        } else if (arg.rfind("-mode=", 0) == 0) {
            std::string value = arg.substr(6);
            std::transform(value.begin(), value.end(), value.begin(), ::tolower);
            std::string modeCandidate = value;
            if (modeCandidate == "ram") {
                modeKey = AppKeys::BootModeRam;
            } else if (modeCandidate == "extract" || modeCandidate == "extracted") {
                modeKey = AppKeys::BootModeExtract;
            } else {
                modeKey = modeCandidate;
            }
        } else if (arg.rfind("-format=", 0) == 0) {
            format = arg.substr(8);
        } else if (arg.rfind("-chkdsk=", 0) == 0) {
            std::string value = arg.substr(8);
            std::transform(value.begin(), value.end(), value.begin(), ::tolower);
            chkdsk = (value == "true" || value == "1" || value == "s" || value == "y");
        } else if (arg.rfind("-autoreboot=", 0) == 0) {
            std::string value = arg.substr(12);
            std::transform(value.begin(), value.end(), value.begin(), ::tolower);
            autoreboot = (value == "true" || value == "1" || value == "s" || value == "y");
        } else if (arg.rfind("-lang=", 0) == 0) {
            languageCodeArg = arg.substr(6);
        }
    }

    if (!languageCodeArg.empty()) {
        if (!localization.loadLanguageByCode(Utils::utf8_to_wstring(languageCodeArg))) {
            std::string languageError =
                LocalizedOrUtf8("message.languageLoadFailed", "The requested language could not be loaded.");
            std::cout << "ERROR: " << languageError << std::endl;
        }
    }

    if (!unattended) {
        std::cout << "Modo GUI no disponible en esta version. Use -unattended para modo consola." << std::endl;
        return 0;
    }

    if (!IsRunAsAdmin()) {
        std::cout << "ERROR: Este programa requiere privilegios de administrador para el modo unattended." << std::endl;
        return 1;
    }

    ClearLogs();
    if (unattended) {
        std::cout << "Modo unattended iniciado" << std::endl;

        // Detectar disco del sistema disponible
        std::string systemDrive = detectSystemDrive();
        if (systemDrive.empty()) {
            std::cout << "ERROR: No se encontró un disco del sistema con suficiente espacio disponible." << std::endl;
            std::cout << "Se requiere al menos 1GB de espacio libre en el disco del sistema." << std::endl;
            return 1;
        }

        std::cout << "Disco del sistema detectado: " << systemDrive << std::endl;

        // Usar el disco del sistema disponible
        std::string targetDrive = systemDrive;
        std::cout << "Usando disco del sistema: " << targetDrive << std::endl;

        // Configurar PartitionManager para usar el disco del sistema
        PartitionManager::getInstance().setMonitoredDrive(targetDrive);

        std::string   debugLogPath = Utils::getExeDirectory() + "logs\\" + UNATTENDED_DEBUG_LOG_FILE;
        std::ofstream debugLog(debugLogPath.c_str());
        debugLog << "Unattended mode started\n";
        debugLog << "Target Drive: " << targetDrive << "\n";
        debugLog << "isoPath: " << isoPath << "\n";
        debugLog << "format: " << format << "\n";
        debugLog << "mode: " << modeKey << "\n";
        debugLog << "chkdsk: " << chkdsk << "\n";
        debugLog << "autoreboot: " << autoreboot << "\n";
        debugLog.close();

        std::cout << "Creando EventManager y ConsoleObserver" << std::endl;
        EventManager    eventManager;
        ConsoleObserver consoleObserver;
        eventManager.addObserver(&consoleObserver);
        ProcessController processController(eventManager);

        std::cout << "Iniciando proceso..." << std::endl;

        // Default to RAM mode if not specified
        if (modeKey.empty()) {
            modeKey = AppKeys::BootModeRam;
        }

        std::string unattendedFallback = (modeKey == AppKeys::BootModeRam ? "Boot desde Memoria" : "Boot desde Disco");
        std::string unattendedLabel    = LocalizedOrUtf8("bootMode." + modeKey, unattendedFallback.c_str());
        processController.startProcess(isoPath, format, modeKey, unattendedLabel, !chkdsk, true);
        return 0;
    }

    // This should never be reached since we check for unattended above
    return 0;
}
