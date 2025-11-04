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

    // Create enhanced startnet.cmd for Windows ISO installation
    // CRITICAL: Mount all partitions so Windows Setup can find install.esd on data partition
    // Unlike USB boot (auto-mounts), internal disk boot requires explicit diskpart scripting
    std::string enhancedContent =
        "@echo off\r\n"
        "rem Mount all partitions for Windows Setup to find install.esd\r\n"
        "echo select disk 0 > X:\\mountall.txt\r\n"
        "echo list partition >> X:\\mountall.txt\r\n"
        "diskpart /s X:\\mountall.txt > X:\\partitions.txt\r\n"
        "\r\n"
        "rem Assign letters to partitions 1-4 (skip system/reserved partitions)\r\n"
        "for /L %%i in (1,1,4) do (\r\n"
        "  echo select disk 0 > X:\\assign%%i.txt\r\n"
        "  echo select partition %%i >> X:\\assign%%i.txt\r\n"
        "  echo assign >> X:\\assign%%i.txt\r\n"
        "  diskpart /s X:\\assign%%i.txt > nul 2>&1\r\n"
        "  del X:\\assign%%i.txt\r\n"
        ")\r\n"
        "\r\n"
        "rem Search for install.esd/wim in all mounted drives\r\n"
        "for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do (\r\n"
        "  if exist %%d:\\sources\\install.esd echo Found install.esd on %%d: >> X:\\found.txt\r\n"
        "  if exist %%d:\\sources\\install.wim echo Found install.wim on %%d: >> X:\\found.txt\r\n"
        ")\r\n"
        "\r\n"
        "wpeinit\r\n";

    std::ofstream snOut(startnetPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!snOut) {
        lastError_ = "Failed to create startnet.cmd";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    snOut.write(enhancedContent.data(), (std::streamsize)enhancedContent.size());
    snOut.flush();

    logFile << ISOCopyManager::getTimestamp() << "Created enhanced startnet.cmd with partition mounting for WinPE"
            << std::endl;

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
