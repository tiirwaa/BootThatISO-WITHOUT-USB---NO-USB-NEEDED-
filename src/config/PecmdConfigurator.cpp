#include "PecmdConfigurator.h"
#include "../models/ISOReader.h"
#include "../services/ISOCopyManager.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>

PecmdConfigurator::PecmdConfigurator() {}

PecmdConfigurator::~PecmdConfigurator() {}

void PecmdConfigurator::getPecmdPaths(const std::string &mountDir, std::string &pecmdExe, std::string &pecmdIni) {
    std::string system32Dir = mountDir + "\\Windows\\System32";
    pecmdExe                = system32Dir + "\\pecmd.exe";
    pecmdIni                = system32Dir + "\\pecmd.ini";
}

bool PecmdConfigurator::isPecmdPE(const std::string &mountDir) {
    std::string pecmdExe, pecmdIni;
    getPecmdPaths(mountDir, pecmdExe, pecmdIni);

    bool hasPecmdExe = (GetFileAttributesA(pecmdExe.c_str()) != INVALID_FILE_ATTRIBUTES);
    bool hasPecmdIni = (GetFileAttributesA(pecmdIni.c_str()) != INVALID_FILE_ATTRIBUTES);

    return hasPecmdExe && hasPecmdIni;
}

bool PecmdConfigurator::hasProgramsDirectory(const std::string &mountDir) {
    std::string programsDir = mountDir + "\\Programs";
    return (GetFileAttributesA(programsDir.c_str()) != INVALID_FILE_ATTRIBUTES);
}

bool PecmdConfigurator::addSubstCommandToPecmdIni(const std::string &pecmdIniPath, std::ofstream &logFile) {
    // Read existing pecmd.ini
    std::ifstream pecmdIn(pecmdIniPath, std::ios::binary);
    if (!pecmdIn) {
        lastError_ = "Could not read pecmd.ini for modification";
        logFile << ISOCopyManager::getTimestamp() << "Warning: " << lastError_ << std::endl;
        return false;
    }

    std::string pecmdContent((std::istreambuf_iterator<char>(pecmdIn)), std::istreambuf_iterator<char>());
    pecmdIn.close();

    // Insert SUBST Y: X:\ command at the very beginning (after first line which is usually {ENTER:...})
    size_t firstNewline = pecmdContent.find('\n');
    if (firstNewline == std::string::npos) {
        lastError_ = "Could not find insertion point in pecmd.ini";
        logFile << ISOCopyManager::getTimestamp() << "Warning: " << lastError_ << std::endl;
        return false;
    }

    std::string substCmd = "// BootThatISO: Map Y: to X: for RAM boot mode\r\n"
                           "EXEC @!X:\\Windows\\System32\\subst.exe Y: X:\\\r\n"
                           "WAIT 500\r\n\r\n";

    pecmdContent.insert(firstNewline + 1, substCmd);

    // Write modified pecmd.ini back
    std::ofstream pecmdOut(pecmdIniPath, std::ios::binary | std::ios::trunc);
    if (!pecmdOut) {
        lastError_ = "Could not write modified pecmd.ini";
        logFile << ISOCopyManager::getTimestamp() << "Warning: " << lastError_ << std::endl;
        return false;
    }

    pecmdOut.write(pecmdContent.data(), (std::streamsize)pecmdContent.size());
    pecmdOut.flush();

    logFile << ISOCopyManager::getTimestamp() << "Added 'subst Y: X:\\' command to pecmd.ini for RAM boot compatibility"
            << std::endl;

    return true;
}

bool PecmdConfigurator::configurePecmdForRamBoot(const std::string &mountDir, std::ofstream &logFile) {
    if (!isPecmdPE(mountDir)) {
        lastError_ = "Not a PECMD PE environment";
        return false;
    }

    logFile << ISOCopyManager::getTimestamp() << "Hiren's/PECMD PE detected in RAM mode: adding Y: -> X: drive mapping"
            << std::endl;

    std::string pecmdExe, pecmdIni;
    getPecmdPaths(mountDir, pecmdExe, pecmdIni);

    return addSubstCommandToPecmdIni(pecmdIni, logFile);
}

bool PecmdConfigurator::extractHbcdIni(const std::string &isoPath, const std::string &mountDir,
                                       ISOReader *isoReader, std::ofstream &logFile) {
    if (!isoReader) {
        lastError_ = "ISOReader is null";
        return false;
    }

    // Check if HBCD_PE.ini exists in ISO root
    auto isoRootFiles = isoReader->listFiles(isoPath);

    bool foundHBCDini = false;
    for (const auto &file : isoRootFiles) {
        std::string lower = Utils::toLower(file);
        std::replace(lower.begin(), lower.end(), '\\', '/');

        // Check if it's HBCD_PE.ini in root (no path separators or only one at the end)
        if ((lower == "hbcd_pe.ini" || lower.find("/hbcd_pe.ini") != std::string::npos) &&
            lower.find_first_of("/\\") == lower.find_last_of("/\\")) {
            foundHBCDini = true;
            break;
        }
    }

    if (!foundHBCDini) {
        logFile << ISOCopyManager::getTimestamp()
                << "HBCD_PE.ini not found in ISO root (normal for some PE variants)" << std::endl;
        return true; // Not an error
    }

    // Extract to boot.wim root (X:\HBCD_PE.ini), accessible as Y:\HBCD_PE.ini via subst
    std::string hbcdIniDest = mountDir + "\\HBCD_PE.ini";
    logFile << ISOCopyManager::getTimestamp() << "Found HBCD_PE.ini in ISO root, extracting to boot.wim root..."
            << std::endl;

    if (isoReader->extractFile(isoPath, "HBCD_PE.ini", hbcdIniDest)) {
        logFile << ISOCopyManager::getTimestamp() << "HBCD_PE.ini copied successfully to boot.wim root: " << hbcdIniDest
                << std::endl;
        return true;
    } else {
        lastError_ = "Failed to extract HBCD_PE.ini from ISO";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }
}
