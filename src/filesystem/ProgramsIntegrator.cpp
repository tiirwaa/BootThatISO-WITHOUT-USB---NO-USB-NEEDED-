#include "ProgramsIntegrator.h"
#include "../models/FileCopyManager.h"
#include "../models/ISOReader.h"
#include "../services/ISOCopyManager.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationHelpers.h"
#include <windows.h>
#include <filesystem>

ProgramsIntegrator::ProgramsIntegrator(FileCopyManager &fileCopyManager) : fileCopyManager_(fileCopyManager) {}

ProgramsIntegrator::~ProgramsIntegrator() {}

bool ProgramsIntegrator::tryCopyFromDirectory(const std::string &sourceDir, const std::string &destDir,
                                              long long &copiedSoFar, std::ofstream &logFile) {
    if (sourceDir.empty() || GetFileAttributesA(sourceDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    long long             programsSize = Utils::getDirectorySize(sourceDir);
    std::set<std::string> excludeDirs;

    if (fileCopyManager_.copyDirectoryWithProgress(sourceDir, destDir, programsSize, copiedSoFar, excludeDirs,
                                                   "Integrando Programs en boot.wim")) {
        logFile << ISOCopyManager::getTimestamp() << "Programs integrated into boot.wim successfully from " << sourceDir
                << std::endl;
        return true;
    }

    logFile << ISOCopyManager::getTimestamp() << "Failed to integrate Programs into boot.wim from " << sourceDir
            << std::endl;
    return false;
}

bool ProgramsIntegrator::tryExtractFromIso(const std::string &isoPath, const std::string &destDir, ISOReader *isoReader,
                                           long long &copiedSoFar, std::ofstream &logFile) {
    if (!isoReader) {
        lastError_ = "ISOReader is null";
        return false;
    }

    // Remove existing Programs directory if extraction is attempted
    std::error_code removeProgramsEc;
    std::filesystem::remove_all(destDir, removeProgramsEc);

    if (isoReader->extractDirectory(isoPath, "Programs", destDir)) {
        copiedSoFar += Utils::getDirectorySize(destDir);
        logFile << ISOCopyManager::getTimestamp() << "Programs extracted from ISO and integrated into boot.wim"
                << std::endl;
        return true;
    }

    logFile << ISOCopyManager::getTimestamp() << "Programs directory not found in ISO or extraction failed"
            << std::endl;
    return false;
}

bool ProgramsIntegrator::integratePrograms(const std::string &mountDir, const std::string &programsSource,
                                           const std::string &fallbackProgramsSource, const std::string &isoPath,
                                           ISOReader *isoReader, long long &copiedSoFar, std::ofstream &logFile,
                                           ProgressCallback progressCallback) {
    if (progressCallback)
        progressCallback(LocalizedOrUtf8("log.bootwim.integratingPrograms", "Integrando Programs en boot.wim..."));

    std::string programsDest = mountDir + "\\Programs";

    // Try primary source
    if (tryCopyFromDirectory(programsSource, programsDest, copiedSoFar, logFile)) {
        if (progressCallback)
            progressCallback(
                LocalizedOrUtf8("log.bootwim.programsIntegrated", "Programs integrado en boot.wim correctamente"));
        return true;
    }

    // Try fallback source (if different from primary)
    if (fallbackProgramsSource != programsSource &&
        tryCopyFromDirectory(fallbackProgramsSource, programsDest, copiedSoFar, logFile)) {
        if (progressCallback)
            progressCallback(
                LocalizedOrUtf8("log.bootwim.programsIntegrated", "Programs integrado en boot.wim correctamente"));
        return true;
    }

    // Try extracting from ISO
    if (tryExtractFromIso(isoPath, programsDest, isoReader, copiedSoFar, logFile)) {
        if (progressCallback)
            progressCallback(
                LocalizedOrUtf8("log.bootwim.programsIntegrated", "Programs integrado en boot.wim correctamente"));
        return true;
    }

    // If Programs is not present anywhere, just skip silently with an info message
    logFile << ISOCopyManager::getTimestamp() << "Programs directory not found in any source; skipping integration"
            << std::endl;
    if (progressCallback)
        progressCallback("Carpeta 'Programs' no encontrada; se omite su integración");

    return false; // Not an error, just not found
}
