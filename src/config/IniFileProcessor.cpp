#include "IniFileProcessor.h"
#include "../models/IniConfigurator.h"
#include "../models/ISOReader.h"
#include "../services/ISOCopyManager.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>

IniFileProcessor::IniFileProcessor(IniConfigurator &iniConfigurator) : iniConfigurator_(iniConfigurator) {}

IniFileProcessor::~IniFileProcessor() {}

int IniFileProcessor::reconfigureExistingIniFiles(const std::string &mountDir, const std::string &driveLetter,
                                                  std::ofstream &logFile) {
    int              count = 0;
    WIN32_FIND_DATAA findData;
    HANDLE           hFind = FindFirstFileA((mountDir + "\\*.ini").c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    do {
        std::string iniName = findData.cFileName;
        std::string iniPath = mountDir + "\\" + iniName;

        iniConfigurator_.configureIniFile(iniPath, driveLetter);
        logFile << ISOCopyManager::getTimestamp() << "Existing " << iniName << " in boot.wim reconfigured"
                << std::endl;
        count++;
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return count;
}

std::vector<std::string> IniFileProcessor::getIniFilesFromIso(const std::string &isoPath, ISOReader *isoReader) {
    if (!isoReader)
        return {};

    std::vector<std::string> iniFiles;
    auto                     isoFileList = isoReader->listFiles(isoPath);

    for (const auto &entry : isoFileList) {
        std::string lower    = Utils::toLower(entry);
        bool        hasSlash = entry.find('\\') != std::string::npos || entry.find('/') != std::string::npos;

        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".ini" && !hasSlash) {
            iniFiles.push_back(entry);
        }
    }

    return iniFiles;
}

int IniFileProcessor::extractAndProcessIniFilesFromIso(const std::string &mountDir, const std::string &isoPath,
                                                       ISOReader *isoReader, const std::string &driveLetter,
                                                       std::ofstream &logFile) {
    if (!isoReader) {
        lastError_ = "ISOReader is null";
        return 0;
    }

    auto iniFiles = getIniFilesFromIso(isoPath, isoReader);
    if (iniFiles.empty())
        return 0;

    // Create temporary directory for INI file caching
    std::filesystem::path tempIniDir;
    bool                  tempDirAvailable = true;

    try {
        std::ostringstream tempDirName;
        tempDirName << "EasyISOBoot_ini_cache_" << reinterpret_cast<uintptr_t>(this);
        tempIniDir = std::filesystem::temp_directory_path() / tempDirName.str();

        std::error_code dirEc;
        std::filesystem::create_directories(tempIniDir, dirEc);
        if (dirEc)
            tempDirAvailable = false;
    } catch (const std::exception &) {
        tempDirAvailable = false;
    }

    int processedCount = 0;

    for (const auto &entry : iniFiles) {
        std::string iniName = entry;
        std::replace(iniName.begin(), iniName.end(), '\\', '/');

        size_t slashPos = iniName.find_last_of('/');
        if (slashPos != std::string::npos)
            iniName = iniName.substr(slashPos + 1);

        std::string isoPathInArchive = entry;
        std::replace(isoPathInArchive.begin(), isoPathInArchive.end(), '\\', '/');

        std::string iniDest   = mountDir + "\\" + iniName;
        bool        processed = false;

        if (tempDirAvailable) {
            std::filesystem::path tempIniPath = tempIniDir / iniName;

            // Remove existing temp file
            std::error_code remEc;
            std::filesystem::remove(tempIniPath, remEc);

            // Extract to temp
            if (isoReader->extractFile(isoPath, isoPathInArchive, tempIniPath.u8string())) {
                // Process and copy to destination
                if (iniConfigurator_.processIniFile(tempIniPath.u8string(), iniDest, driveLetter)) {
                    logFile << ISOCopyManager::getTimestamp() << iniName
                            << " processed and copied to boot.wim successfully" << std::endl;
                    processed = true;
                    processedCount++;
                }
            }

            // Cleanup temp file
            std::error_code cleanupEc;
            std::filesystem::remove(tempIniPath, cleanupEc);
        }

        // Fallback: direct extraction without processing
        if (!processed) {
            if (isoReader->extractFile(isoPath, isoPathInArchive, iniDest)) {
                logFile << ISOCopyManager::getTimestamp() << iniName << " copied to boot.wim successfully (fallback)"
                        << std::endl;
                processedCount++;
            } else {
                logFile << ISOCopyManager::getTimestamp() << "Failed to copy " << iniName << " to boot.wim"
                        << std::endl;
            }
        }
    }

    // Cleanup temp directory
    if (tempDirAvailable) {
        std::error_code cleanupDirEc;
        std::filesystem::remove_all(tempIniDir, cleanupDirEc);
    }

    return processedCount;
}

bool IniFileProcessor::processIniFiles(const std::string &mountDir, const std::string &isoPath, ISOReader *isoReader,
                                       const std::string &driveLetter, std::ofstream &logFile,
                                       ProgressCallback progressCallback) {
    if (progressCallback)
        progressCallback("Procesando archivos .ini...");

    // Reconfigure existing INI files
    int existingCount = reconfigureExistingIniFiles(mountDir, driveLetter, logFile);

    // Extract and process INI files from ISO
    int extractedCount = extractAndProcessIniFilesFromIso(mountDir, isoPath, isoReader, driveLetter, logFile);

    if (existingCount + extractedCount > 0) {
        logFile << ISOCopyManager::getTimestamp() << "INI files processed: " << existingCount
                << " existing reconfigured, " << extractedCount << " extracted from ISO" << std::endl;

        if (progressCallback)
            progressCallback("Archivos .ini integrados y reconfigurados correctamente");

        return true;
    }

    logFile << ISOCopyManager::getTimestamp() << "No INI files found to process" << std::endl;
    return true; // Not an error
}
