#include "ProcessService.h"
#include "../utils/Utils.h"
#include "../utils/AppKeys.h"
#include "../utils/LocalizationHelpers.h"
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
    // CRITICAL: Check for duplicate ISOEFI partitions
    int efiCount = partitionManager->countEfiPartitions();
    if (efiCount > 1) {
        eventManager.notifyLogUpdate("\r\n");
        eventManager.notifyLogUpdate("========================================\r\n");
        eventManager.notifyLogUpdate(LocalizedOrUtf8("log.efi.duplicate_warning", "CRITICAL WARNING:\r\n"));
        eventManager.notifyLogUpdate(LocalizedFormatUtf8("log.efi.duplicate_count", {Utils::utf8_to_wstring(std::to_string(efiCount))}, "{0} duplicate ISOEFI partitions detected.\r\n"));
        eventManager.notifyLogUpdate(LocalizedOrUtf8("log.efi.duplicate_boot_issue", "This may cause boot problems.\r\n"));
        eventManager.notifyLogUpdate("========================================\r\n");
        eventManager.notifyLogUpdate("\r\n");
        eventManager.notifyLogUpdate(LocalizedOrUtf8("log.efi.deleting_all", "Deleting ALL partitions to recreate them correctly...\r\n"));

        // Force cleanup and recreation
        if (!partitionManager->recoverSpace()) {
            return {false, LocalizedOrUtf8("error.efi.cleanup_failed", "Error cleaning duplicate partitions.")};
        }
        // After cleanup, continue with normal creation
    }

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

            eventManager.notifyLogUpdate(LocalizedFormatUtf8("log.efi.wrong_size_warning", 
                {Utils::utf8_to_wstring(std::to_string(currentEfiSizeMB)), Utils::utf8_to_wstring(std::to_string(REQUIRED_EFI_SIZE_MB))},
                "WARNING: EFI partition with incorrect size ({0} MB, required {1} MB)\r\n"));
            eventManager.notifyLogUpdate(LocalizedOrUtf8("log.efi.deleting_both", "Deleting BOTH partitions to recreate with new size...\r\n"));
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
                return {false, LocalizedOrUtf8("error.partition.reformatFailed", "Error reformatting partition.")};
            }
        }
    } else {
        if (!partitionManager->createPartition(format, skipIntegrityCheck)) {
            return {false, LocalizedOrUtf8("error.partition.createFailed", "Error creating partition.")};
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

    // Check free space on EFI partition
    ULARGE_INTEGER freeBytesAvailable, totalBytes, freeBytes;
    std::string    efiRoot = espDrive.substr(0, 2) + "\\";
    if (GetDiskFreeSpaceExA(efiRoot.c_str(), &freeBytesAvailable, &totalBytes, &freeBytes)) {
        const ULONGLONG minFreeMB = 100; // 100 MB mínimo libre
        ULONGLONG       freeMB    = freeBytesAvailable.QuadPart / (1024 * 1024);
        if (freeMB < minFreeMB) {
            return {false, "La partición EFI no tiene suficiente espacio libre (" + std::to_string(freeMB) +
                               " MB libres, se requieren al menos " + std::to_string(minFreeMB) + " MB)."};
        }
    } else {
        // If can't check free space, log warning but continue
        eventManager.notifyLogUpdate(LocalizedOrUtf8("log.efi.freeSpaceWarning", "Warning: Could not verify free space in EFI partition.\r\n"));
    }

    // Reformat EFI if needed
    if (!partitionExists) {
        if (!partitionManager->reformatEfiPartition()) {
            return {false, LocalizedOrUtf8("error.efi.reformatFailed", "Error reformatting EFI partition.")};
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
        return {false, LocalizedOrUtf8("error.iso.prepareFailed", "Error preparing ISO content.")};
    }
}

ProcessService::ProcessResult ProcessService::configureBoot(const std::string &modeKey) {
    if (modeKey == AppKeys::BootModeRam || modeKey == AppKeys::BootModeExtract) {
        auto strategy = BootStrategyFactory::createStrategy(modeKey);
        if (!strategy) {
            return {false, "Modo de boot no válido."};
        }
        std::string error = bcdManager->configureBCD(partitionDrive.substr(0, 2), espDrive.substr(0, 2), *strategy);
        if (!error.empty()) {
            return {false, LocalizedFormatUtf8("error.bcd.configureFailed", {Utils::utf8_to_wstring(error)}, "Error configuring BCD: {0}")};
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
        // In disk mode (EXTRACT):
        // - Windows ISOs: Extract all content, process boot.wim, and copy install.wim
        // - Non-Windows ISOs: Just extract all content, no Windows-specific processing
        bool extractBootWim = isWindowsISO; // Only process boot.wim for Windows ISOs
        bool copyInstallWim = isWindowsISO; // Only copy install.wim/esd for Windows ISOs

        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDriveLocal, true, extractBootWim,
                                               copyInstallWim, modeKey, format, injectDrivers)) {
            return true;
        }
    }
    return false;
}