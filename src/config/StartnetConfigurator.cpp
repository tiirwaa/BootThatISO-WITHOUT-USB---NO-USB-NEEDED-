#include "StartnetConfigurator.h"
#include "../services/ISOCopyManager.h"
#include <windows.h>
#include <fstream>

StartnetConfigurator::StartnetConfigurator() {}

StartnetConfigurator::~StartnetConfigurator() {}

std::string StartnetConfigurator::getStartnetPath(const std::string &mountDir) {
    return mountDir + "\\Windows\\System32\\startnet.cmd";
}

bool StartnetConfigurator::ensureSystem32Exists(const std::string &mountDir) {
    std::string windowsDir  = mountDir + "\\Windows";
    std::string system32Dir = windowsDir + "\\System32";

    CreateDirectoryA(windowsDir.c_str(), NULL);
    CreateDirectoryA(system32Dir.c_str(), NULL);

    return (GetFileAttributesA(system32Dir.c_str()) != INVALID_FILE_ATTRIBUTES);
}

bool StartnetConfigurator::startnetExists(const std::string &mountDir) {
    std::string startnetPath = getStartnetPath(mountDir);
    return (GetFileAttributesA(startnetPath.c_str()) != INVALID_FILE_ATTRIBUTES);
}

bool StartnetConfigurator::createMinimalStartnet(const std::string &mountDir, std::ofstream &logFile) {
    if (!ensureSystem32Exists(mountDir)) {
        lastError_ = "Failed to create Windows\\System32 directory";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    std::string startnetPath = getStartnetPath(mountDir);

    // Create minimal startnet.cmd for standard WinPE
    std::string minimalContent = "@echo off\r\nwpeinit\r\n";

    std::ofstream snOut(startnetPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!snOut) {
        lastError_ = "Failed to create startnet.cmd";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    snOut.write(minimalContent.data(), (std::streamsize)minimalContent.size());
    snOut.flush();

    logFile << ISOCopyManager::getTimestamp() << "Created minimal startnet.cmd for WinPE" << std::endl;

    return true;
}

bool StartnetConfigurator::configureStartnet(const std::string &mountDir, std::ofstream &logFile) {
    if (startnetExists(mountDir)) {
        // Preserve existing startnet.cmd
        logFile << ISOCopyManager::getTimestamp() << "startnet.cmd found: preserving without changes" << std::endl;
        return true;
    }

    // Create minimal startnet.cmd for non-PECMD WinPE
    logFile << ISOCopyManager::getTimestamp() << "startnet.cmd not present, creating minimal WinPE init" << std::endl;
    return createMinimalStartnet(mountDir, logFile);
}
