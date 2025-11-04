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

    // Create startnet.cmd that mounts partitions BEFORE wpeinit
    std::string enhancedContent = "@echo off\r\n"
                                  "echo Mounting disk partitions before WinPE initialization...\r\n"
                                  "\r\n"
                                  "rem Force rescan of all disks\r\n"
                                  "echo rescan > X:\\rescan.txt\r\n"
                                  "diskpart /s X:\\rescan.txt > nul 2>&1\r\n"
                                  "del X:\\rescan.txt\r\n"
                                  "\r\n"
                                  "rem Assign letters to all partitions on disk 0\r\n"
                                  "for /L %%p in (1,1,6) do (\r\n"
                                  "  echo select disk 0 > X:\\assign.txt\r\n"
                                  "  echo select partition %%p >> X:\\assign.txt\r\n"
                                  "  echo assign >> X:\\assign.txt\r\n"
                                  "  diskpart /s X:\\assign.txt > nul 2>&1\r\n"
                                  ")\r\n"
                                  "del X:\\assign.txt\r\n"
                                  "\r\n"
                                  "rem Wait for filesystem initialization\r\n"
                                  "ping 127.0.0.1 -n 2 > nul\r\n"
                                  "\r\n"
                                  "wpeinit\r\n"
                                  "\r\n"
                                  "rem After wpeinit, ensure partitions are still mounted\r\n"
                                  "call X:\\mount_partitions.cmd\r\n";

    std::ofstream snOut(startnetPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!snOut) {
        lastError_ = "Failed to create startnet.cmd";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    snOut.write(enhancedContent.data(), (std::streamsize)enhancedContent.size());
    snOut.close();

    // Create persistent mount script in X:\ (will be in WIM root)
    std::string mountScript  = mountDir + "\\mount_partitions.cmd";
    std::string mountContent = "@echo off\r\n"
                               "rem Persistent partition mounting for Windows Setup\r\n"
                               "echo Preparing Windows Setup environment...\r\n"
                               "\r\n"
                               "rem Rescan disks\r\n"
                               "echo rescan > X:\\rescan2.txt\r\n"
                               "diskpart /s X:\\rescan2.txt > nul 2>&1\r\n"
                               "del X:\\rescan2.txt\r\n"
                               "\r\n"
                               "rem Mount all partitions\r\n"
                               "for /L %%p in (1,1,6) do (\r\n"
                               "  echo select disk 0 > X:\\mount%%p.txt\r\n"
                               "  echo select partition %%p >> X:\\mount%%p.txt\r\n"
                               "  echo assign >> X:\\mount%%p.txt\r\n"
                               "  diskpart /s X:\\mount%%p.txt > nul 2>&1\r\n"
                               "  del X:\\mount%%p.txt\r\n"
                               ")\r\n"
                               "\r\n"
                               "rem Wait for partitions to stabilize\r\n"
                               "ping 127.0.0.1 -n 2 > nul\r\n"
                               "\r\n"
                               "rem Find partition with install.esd/wim\r\n"
                               "set INSTALL_DRIVE=\r\n"
                               "for %%d in (D E F G H I J K L M N O P Q R S T U V W Y Z) do (\r\n"
                               "  if exist %%d:\\sources\\install.esd set INSTALL_DRIVE=%%d\r\n"
                               "  if exist %%d:\\sources\\install.wim set INSTALL_DRIVE=%%d\r\n"
                               ")\r\n"
                               "\r\n"
                               "rem If install files found, copy Setup to that drive\r\n"
                               "if defined INSTALL_DRIVE (\r\n"
                               "  echo Found installation files on %INSTALL_DRIVE%:\r\n"
                               "  echo Copying Setup files to %INSTALL_DRIVE%: for proper execution...\r\n"
                               "  \r\n"
                               "  rem Copy setup.exe to root\r\n"
                               "  xcopy X:\\setup.exe %INSTALL_DRIVE%:\\ /Y /H > nul 2>&1\r\n"
                               "  \r\n"
                               "  rem Copy critical files to sources\r\n"
                               "  xcopy X:\\sources\\*.exe %INSTALL_DRIVE%:\\sources\\ /Y /H > nul 2>&1\r\n"
                               "  xcopy X:\\sources\\*.dll %INSTALL_DRIVE%:\\sources\\ /Y /H > nul 2>&1\r\n"
                               "  xcopy X:\\sources\\*.ini %INSTALL_DRIVE%:\\sources\\ /Y /H /E > nul 2>&1\r\n"
                               "  \r\n"
                               "  rem Create launcher that starts Setup from install drive\r\n"
                               "  echo @echo off > X:\\launch_setup.cmd\r\n"
                               "  echo cd /d %INSTALL_DRIVE%:\\ >> X:\\launch_setup.cmd\r\n"
                               "  echo start setup.exe >> X:\\launch_setup.cmd\r\n"
                               "  \r\n"
                               "  rem Kill any Setup running from X: and launch from install drive\r\n"
                               "  taskkill /IM setup.exe /F > nul 2>&1\r\n"
                               "  ping 127.0.0.1 -n 1 > nul\r\n"
                               "  call X:\\launch_setup.cmd\r\n"
                               "  \r\n"
                               "  echo Setup launched from %INSTALL_DRIVE%: successfully\r\n"
                               ") else (\r\n"
                               "  echo WARNING: No install.esd/wim found on any mounted drive\r\n"
                               ")\r\n";

    std::ofstream msOut(mountScript, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!msOut) {
        logFile << ISOCopyManager::getTimestamp() << "Warning: Failed to create mount_partitions.cmd" << std::endl;
    } else {
        msOut.write(mountContent.data(), (std::streamsize)mountContent.size());
        msOut.close();
        logFile << ISOCopyManager::getTimestamp() << "Created mount_partitions.cmd for persistent mounting"
                << std::endl;
    }

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
