#include "ProcessService.h"
#include "../utils/Utils.h"
#include "../utils/AppKeys.h"
#include "../../include/models/HashInfo.h"
#include <fstream>
#include "../utils/constants.h"

static HashInfo readHashInfo(const std::string& path) {
    HashInfo info = {"", "", ""};
    std::ifstream file(path);
    if (file.is_open()) {
        std::getline(file, info.hash);
        std::getline(file, info.mode);
        std::getline(file, info.format);
    }
    return info;
}

ProcessService::ProcessService(PartitionManager* pm, ISOCopyManager* icm, BCDManager* bcm, EventManager& em)
    : partitionManager(pm), isoCopyManager(icm), bcdManager(bcm), eventManager(em) {}

ProcessService::ProcessResult ProcessService::validateAndPrepare(const std::string& isoPath, const std::string& format, bool skipIntegrityCheck) {
    bool partitionExists = partitionManager->partitionExists();
    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            return {false, validation.errorMessage};
        }
    }

    if (partitionExists) {
        std::string partDrive = partitionManager->getPartitionDriveLetter();
        if (!partDrive.empty()) {
            std::string hashFilePath = partDrive + "\\ISOBOOTHASH";
            std::string md5 = Utils::calculateMD5(isoPath);
            HashInfo existing = readHashInfo(hashFilePath);
            if (existing.hash == md5 && existing.format == format && !existing.hash.empty()) {
                // Skip format
            } else {
                if (!partitionManager->reformatPartition(format)) {
                    return {false, "Error al reformatear la partición."};
                }
            }
        } else {
            if (!partitionManager->reformatPartition(format)) {
                return {false, "Error al reformatear la partición."};
            }
        }
    } else {
        if (!partitionManager->createPartition(format, skipIntegrityCheck)) {
            return {false, "Error al crear la partición."};
        }
    }

    // Get drives
    partitionDrive = partitionManager->getPartitionDriveLetter();
    if (partitionDrive.empty()) {
        return {false, "No se puede acceder a la partición ISOBOOT."};
    }

    espDrive = partitionManager->getEfiPartitionDriveLetter();
    if (espDrive.empty()) {
        return {false, "No se puede acceder a la partición ISOEFI."};
    }

    // Reformat EFI if needed
    if (!partitionExists) {
        if (!partitionManager->reformatEfiPartition()) {
            return {false, "Error al reformatear la partición EFI."};
        }
    }

    return {true, ""};
}

ProcessService::ProcessResult ProcessService::copyIsoContent(const std::string& isoPath, const std::string& format, const std::string& modeKey, const std::string& modeLabel) {
    if (copyISO(isoPath, partitionDrive, espDrive, modeKey, modeLabel, format)) {
        return {true, ""};
    } else {
        return {false, "Error al preparar el contenido del ISO."};
    }
}

ProcessService::ProcessResult ProcessService::configureBoot(const std::string& modeKey) {
    if (isoCopyManager->getIsWindowsISO() || modeKey == AppKeys::BootModeRam) {
        auto strategy = BootStrategyFactory::createStrategy(modeKey);
        if (!strategy) {
            return {false, "Modo de boot no válido."};
        }
        std::string error = bcdManager->configureBCD(partitionDrive.substr(0, 2), espDrive.substr(0, 2), *strategy);
        if (!error.empty()) {
            return {false, "Error al configurar BCD: " + error};
        }
    }
    return {true, ""};
}

bool ProcessService::copyISO(const std::string& isoPath, const std::string& destPath, const std::string& espPath, const std::string& modeKey, const std::string& modeLabel, const std::string& format) {
    std::string drive = destPath;
    std::string espDriveLocal = espPath;

    if (modeKey == AppKeys::BootModeRam) {
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDriveLocal, false, true, true, modeKey, format)) {
            return true;
        }
    } else {
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDriveLocal, true, true, true, modeKey, format)) {
            return true;
        }
    }
    return false;
}