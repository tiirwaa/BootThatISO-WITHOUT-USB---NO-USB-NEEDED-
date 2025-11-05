#include "SpaceManager.h"
#include "RecoverySteps.h"
#include "VolumeDetector.h"
#include "../services/bcdmanager.h"
#include "../utils/LocalizationHelpers.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include <windows.h>
#include <vds.h>
#include <Objbase.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <algorithm>
#include <memory>

namespace {
std::string normalizeDriveRoot(const std::string &drive) {
    if (drive.empty()) {
        return "";
    }
    std::string normalized = drive;
    if (normalized.size() >= 2 && normalized[1] == ':') {
        normalized = normalized.substr(0, 2);
    }
    if (normalized.size() == 1 && (normalized[0] == '\\' || normalized[0] == '/')) {
        return normalized;
    }
    if (!normalized.empty() && normalized.back() != '\\' && normalized.back() != '/') {
        normalized += "\\";
    }
    return normalized;
}

std::string detectSystemDrive() {
    char  buffer[MAX_PATH] = {0};
    DWORD length           = GetEnvironmentVariableA("SystemDrive", buffer, MAX_PATH);
    if (length >= 2 && buffer[1] == ':') {
        return normalizeDriveRoot(std::string(buffer, length));
    }
    char windowsDir[MAX_PATH] = {0};
    UINT written              = GetWindowsDirectoryA(windowsDir, MAX_PATH);
    if (written >= 2 && windowsDir[1] == ':') {
        return normalizeDriveRoot(std::string(windowsDir, windowsDir + 2));
    }
    return "C:\\";
}
} // namespace

SpaceManager::SpaceManager(EventManager *eventManager)
    : eventManager_(eventManager), monitoredDrive_(detectSystemDrive()) {}

SpaceManager::~SpaceManager() {}

SpaceValidationResult SpaceManager::validateAvailableSpace() {
    long long availableGB = getAvailableSpaceGB();

    SpaceValidationResult result;
    result.availableGB = availableGB;
    result.isValid     = availableGB >= 10;

    // Log to file
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + SPACE_VALIDATION_LOG_FILE).c_str());
    if (logFile) {
        logFile << "Available space: " << availableGB << " GB\n";
        logFile << "Is valid: " << (result.isValid ? "yes" : "no") << "\n";
        if (!result.isValid) {
            std::ostringstream oss;
            oss << "No hay suficiente espacio disponible. Se requieren al menos 10 GB, pero solo hay " << availableGB
                << " GB disponibles.";
            result.errorMessage = oss.str();
            logFile << "Error: " << result.errorMessage << "\n";
        }
        logFile.close();
    }

    return result;
}

long long SpaceManager::getAvailableSpaceGB(const std::string &driveRoot) {
    std::string target = driveRoot.empty() ? monitoredDrive_ : normalizeDriveRoot(driveRoot);
    if (target.empty()) {
        target          = detectSystemDrive();
        monitoredDrive_ = target;
    }

    ULARGE_INTEGER freeBytesAvailable{}, totalNumberOfBytes{}, totalNumberOfFreeBytes{};
    if (GetDiskFreeSpaceExA(target.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return static_cast<long long>(freeBytesAvailable.QuadPart / (1024LL * 1024 * 1024));
    }
    return 0;
}

bool SpaceManager::performSpaceRecovery() {
    // Always recover space to ensure clean state
    if (eventManager_)
        eventManager_->notifyLogUpdate("Recuperando espacio para particiones...\r\n");
    if (!recoverSpace()) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.partition.space_recovery_failed", "Error: Falló la recuperación de espacio.\r\n"));
        return false;
    }
    return true;
}

bool SpaceManager::recoverSpace() {
    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.space.starting_recovery", "Iniciando recuperación de espacio...\r\n"));

    // Open log file
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + RECOVER_LOG_FILE).c_str(), std::ios::app);
    logFile << "Starting space recovery at " << __DATE__ << " " << __TIME__ << "\n";
    logFile.flush();

    try {
        logFile << "Entered try block\n";
        logFile.flush();

        // Create recovery steps
        std::vector<std::pair<std::string, std::string>> volumesToDelete;
        std::unique_ptr<IRecoveryStep> enumerator = std::make_unique<VolumeEnumerator>(eventManager_, volumesToDelete);
        std::unique_ptr<IRecoveryStep> deleter = std::make_unique<VolumeDeleter>(eventManager_, volumesToDelete);
        std::unique_ptr<IRecoveryStep> resizer = std::make_unique<PartitionResizer>(eventManager_);
        std::unique_ptr<IRecoveryStep> cleaner = std::make_unique<BCDCleaner>(eventManager_);

        // Execute steps
        if (!enumerator->execute()) {
            logFile << "Volume enumeration failed\n";
            logFile.close();
            return false;
        }

        if (!deleter->execute()) {
            logFile << "Volume deletion failed\n";
            logFile.close();
            return false;
        }

        if (!resizer->execute()) {
            logFile << "Partition resizing failed\n";
            // Continue anyway, as resizing might not be critical
        }

        if (!cleaner->execute()) {
            logFile << "BCD cleaning failed\n";
            // Continue anyway
        }

        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.space.recovery_successful", "Space recovered successfully.\r\n"));
        logFile << "Recovery successful\n";
        logFile.close();
        return true;
    } catch (const std::exception &e) {
        logFile << "Exception caught: " << e.what() << "\n";
        logFile.close();
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: Exception during space recovery: " + std::string(e.what()) + "\r\n");
        return false;
    } catch (...) {
        logFile << "Unknown exception caught\n";
        logFile.close();
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: Unknown exception during space recovery\r\n");
        return false;
    }
}