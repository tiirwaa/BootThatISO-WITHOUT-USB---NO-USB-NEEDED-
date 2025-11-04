#include "ProcessService.h"
#include "../utils/Utils.h"
#include "../utils/AppKeys.h"
#include "../../include/models/HashInfo.h"
#include "../models/ISOReader.h"
#include <fstream>
#include "../utils/constants.h"

static HashInfo readHashInfo(const std::string &path) {
    HashInfo      info = {"", "", "", "", ""};
    std::ifstream file(path);
    if (file.is_open()) {
        std::getline(file, info.hash);
        std::getline(file, info.mode);
        std::getline(file, info.format);
        std::getline(file, info.driversInjected);
    }
    return info;
}

ProcessService::ProcessService(PartitionManager *pm, ISOCopyManager *icm, BCDManager *bcm, EventManager &em)
    : partitionManager(pm), isoCopyManager(icm), bcdManager(bcm), eventManager(em) {}

ProcessService::ProcessResult ProcessService::validateAndPrepare(const std::string &isoPath, const std::string &format,
                                                                 bool skipIntegrityCheck) {
    bool partitionExists = partitionManager->partitionExists();

    // CRITICAL: Check EFI partition size for backward compatibility
    // If EFI partition exists with wrong size, we must recreate BOTH partitions
    bool needsRecreation = false;
    if (partitionManager->efiPartitionExists()) {
        int currentEfiSizeMB = partitionManager->getEfiPartitionSizeMB();
        if (currentEfiSizeMB > 0 && currentEfiSizeMB != REQUIRED_EFI_SIZE_MB) {
            // Wrong size detected - must recreate both partitions
            needsRecreation = true;
            partitionExists = false; // Force recreation flow

            eventManager.notifyLogUpdate("ADVERTENCIA: Partición EFI con tamaño incorrecto (" +
                                         std::to_string(currentEfiSizeMB) + " MB, se requieren " +
                                         std::to_string(REQUIRED_EFI_SIZE_MB) + " MB)\r\n");
            eventManager.notifyLogUpdate("Eliminando AMBAS particiones para recrearlas con el nuevo tamaño...\r\n");
        }
    }

    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            return {false, validation.errorMessage};
        }
    }

    if (partitionExists && !needsRecreation) {
        std::string partDrive = partitionManager->getPartitionDriveLetter();
        if (!partDrive.empty()) {
            std::string hashFilePath = partDrive + "\\ISOBOOTHASH";
            std::string md5          = Utils::calculateMD5(isoPath);
            HashInfo    existing     = readHashInfo(hashFilePath);
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

ProcessService::ProcessResult ProcessService::copyIsoContent(const std::string &isoPath, const std::string &format,
                                                             const std::string &modeKey, const std::string &modeLabel,
                                                             bool injectDrivers) {
    if (copyISO(isoPath, partitionDrive, espDrive, modeKey, modeLabel, format, injectDrivers)) {
        return {true, ""};
    } else {
        return {false, "Error al preparar el contenido del ISO."};
    }
}

ProcessService::ProcessResult ProcessService::configureBoot(const std::string &modeKey) {
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

bool ProcessService::copyISO(const std::string &isoPath, const std::string &destPath, const std::string &espPath,
                             const std::string &modeKey, const std::string &modeLabel, const std::string &format,
                             bool injectDrivers) {
    std::string drive         = destPath;
    std::string espDriveLocal = espPath;

    // Pre-detect if this is a Windows ISO by checking for sources/boot.wim or sources/install.wim
    ISOReader tempReader;
    bool      isWindowsISO = tempReader.fileExists(isoPath, "sources/boot.wim") ||
                        tempReader.fileExists(isoPath, "sources/install.wim") ||
                        tempReader.fileExists(isoPath, "sources/install.esd");

    if (modeKey == AppKeys::BootModeRam) {
        // In RAM mode:
        // - Windows ISOs: Extract boot.wim AND copy install.wim/esd + Setup files
        // - Non-Windows ISOs: Just extract all content, no boot.wim processing needed
        bool extractBootWim = isWindowsISO;  // Only extract boot.wim for Windows ISOs
        bool extractContent = !isWindowsISO; // Extract full content only for non-Windows ISOs
        bool copyInstallWim = isWindowsISO;  // Copy install.wim/esd + Setup for Windows ISOs

        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDriveLocal, extractContent,
                                               extractBootWim, copyInstallWim, modeKey, format, injectDrivers)) {
            return true;
        }
    } else {
        // In disk mode, we still need install.wim on disk
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDriveLocal, true, true, true, modeKey,
                                               format, injectDrivers)) {
            return true;
        }
    }
    return false;
}