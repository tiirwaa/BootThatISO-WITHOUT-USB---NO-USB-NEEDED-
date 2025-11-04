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
    std::string enhancedContent =
        "@echo off\r\n"
        "echo Mounting disk partitions before WinPE initialization...\r\n"
        "\r\n"
        "rem Force rescan of all disks\r\n"
        "echo rescan > X:\\rescan.txt\r\n"
        "diskpart /s X:\\rescan.txt > nul 2>&1\r\n"
        "del X:\\rescan.txt 2>nul\r\n"
        "\r\n"
        "rem Assign letters to all partitions on disk 0 (ignore errors for hidden partitions)\r\n"
        "for /L %%p in (1,1,6) do (\r\n"
        "  echo select disk 0 > X:\\assign.txt\r\n"
        "  echo select partition %%p >> X:\\assign.txt\r\n"
        "  echo assign >> X:\\assign.txt\r\n"
        "  diskpart /s X:\\assign.txt > nul 2>&1\r\n"
        "  del X:\\assign.txt 2>nul\r\n"
        ")\r\n"
        "\r\n"
        "rem Wait for filesystem initialization and partition stabilization\r\n"
        "ping 127.0.0.1 -n 4 > nul\r\n"
        "\r\n"
        "rem Start wpeinit in background and continue\r\n"
        "start /min wpeinit\r\n"
        "\r\n"
        "rem Wait for wpeinit to initialize network and drivers\r\n"
        "ping 127.0.0.1 -n 5 > nul\r\n"
        "\r\n"
        "rem Execute partition mounting and Setup launcher\r\n"
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
                               "echo.\r\n"
                               "echo ====================================================\r\n"
                               "echo Preparing Windows Setup environment...\r\n"
                               "echo ====================================================\r\n"
                               "echo.\r\n"
                               "\r\n"
                               "rem Rescan disks\r\n"
                               "echo [1/4] Rescanning disks...\r\n"
                               "echo rescan > X:\\rescan2.txt\r\n"
                               "diskpart /s X:\\rescan2.txt\r\n"
                               "del X:\\rescan2.txt 2>nul\r\n"
                               "\r\n"
                               "rem Mount all partitions with verbose output\r\n"
                               "echo [2/4] Mounting all partitions on disk 0...\r\n"
                               "for /L %%p in (1,1,6) do (\r\n"
                               "  echo Mounting partition %%p...\r\n"
                               "  echo select disk 0 > X:\\mount%%p.txt\r\n"
                               "  echo select partition %%p >> X:\\mount%%p.txt\r\n"
                               "  echo assign >> X:\\mount%%p.txt\r\n"
                               "  diskpart /s X:\\mount%%p.txt > nul 2>&1\r\n"
                               "  del X:\\mount%%p.txt 2>nul\r\n"
                               ")\r\n"
                               "\r\n"
                               "rem Wait for partitions to stabilize\r\n"
                               "echo [3/4] Waiting for partitions to stabilize...\r\n"
                               "ping 127.0.0.1 -n 3 > nul\r\n"
                               "\r\n"
                               "rem List all available drives for debugging\r\n"
                               "echo [4/4] Searching for Windows installation files...\r\n"
                               "echo Available drives:\r\n"
                               "for %%d in (C D E F G H I J K L M N O P Q R S T U V W X Y Z) do (\r\n"
                               "  if exist %%d:\\ echo   %%d:\r\n"
                               ")\r\n"
                               "echo.\r\n"
                               "\r\n"
                               "rem Find partition with install.esd/wim and setup.exe (exclude only X: WinPE)\r\n"
                               "set INSTALL_DRIVE=\r\n"
                               "for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do (\r\n"
                               "  rem Skip X: drive (WinPE)\r\n"
                               "  if not \"%%d\"==\"X\" (\r\n"
                               "    if exist %%d:\\sources\\install.esd if exist %%d:\\setup.exe (\r\n"
                               "      set INSTALL_DRIVE=%%d\r\n"
                               "      echo Found install.esd on %%d:\r\n"
                               "    )\r\n"
                               "    if exist %%d:\\sources\\install.wim if exist %%d:\\setup.exe (\r\n"
                               "      set INSTALL_DRIVE=%%d\r\n"
                               "      echo Found install.wim on %%d:\r\n"
                               "    )\r\n"
                               "  )\r\n"
                               ")\r\n"
                               "\r\n"
                               "rem If installation partition found, launch Setup from there\r\n"
                               "if defined INSTALL_DRIVE (\r\n"
                               "  echo.\r\n"
                               "  echo ====================================================\r\n"
                               "  echo SUCCESS: Found Windows installation on %INSTALL_DRIVE%:\r\n"
                               "  echo ====================================================\r\n"
                               "  echo.\r\n"
                               "  echo Launching Windows Setup from %INSTALL_DRIVE%:...\r\n"
                               "  \r\n"
                               "  rem Kill any Setup running from X: and launch from install drive\r\n"
                               "  taskkill /IM setup.exe /F > nul 2>&1\r\n"
                               "  ping 127.0.0.1 -n 2 > nul\r\n"
                               "  \r\n"
                               "  rem Launch Setup from the partition with install files\r\n"
                               "  cd /d %INSTALL_DRIVE%:\\\r\n"
                               "  start setup.exe\r\n"
                               "  \r\n"
                               "  echo Setup launched successfully from %INSTALL_DRIVE%:\r\n"
                               "  echo.\r\n"
                               ") else (\r\n"
                               "  echo.\r\n"
                               "  echo ====================================================\r\n"
                               "  echo WARNING: No Windows installation files found\r\n"
                               "  echo ====================================================\r\n"
                               "  echo.\r\n"
                               "  echo Searched for:\r\n"
                               "  echo   - sources\\install.esd or sources\\install.wim\r\n"
                               "  echo   - setup.exe\r\n"
                               "  echo.\r\n"
                               "  echo Please mount partitions manually and run setup.exe\r\n"
                               "  echo.\r\n"
                               "  pause\r\n"
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
    // Always create/overwrite startnet.cmd for Windows ISOs to inject partition mounting
    logFile << ISOCopyManager::getTimestamp() << "Configuring startnet.cmd with partition mounting support"
            << std::endl;
    return createMinimalStartnet(mountDir, logFile);
}
