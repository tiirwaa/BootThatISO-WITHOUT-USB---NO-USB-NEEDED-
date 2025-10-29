#include "isocopymanager.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <cstdio>

ISOCopyManager::ISOCopyManager()
{
}

ISOCopyManager::~ISOCopyManager()
{
}

std::string ISOCopyManager::exec(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = _popen(cmd, "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof buffer, pipe) != NULL) {
        result += buffer;
    }
    _pclose(pipe);
    return result;
}

bool ISOCopyManager::extractEFIFiles(const std::string& isoPath, const std::string& destPath)
{
    // Mount the ISO using PowerShell
    std::string mountCmd = "powershell -Command \"Mount-DiskImage -ImagePath '" + isoPath + "' | Get-Volume | Select -ExpandProperty DriveLetter\"";
    std::string mountResult = exec(mountCmd.c_str());
    
    if (mountResult.empty() || mountResult.find("error") != std::string::npos) {
        return false;
    }
    
    // Get the drive letter (should be the last character before newline)
    char driveLetter = mountResult[mountResult.length() - 2]; // -2 because of \r\n
    std::string sourcePath = std::string(1, driveLetter) + ":\\";
    
    // Copy EFI files
    std::string copyCmd = "xcopy /E /I /H /Y \"" + sourcePath + "efi\" \"" + destPath + "efi\"";
    std::string copyResult = exec(copyCmd.c_str());
    
    // Also try to copy boot folder if it exists
    std::string copyBootCmd = "xcopy /E /I /H /Y \"" + sourcePath + "boot\" \"" + destPath + "boot\"";
    exec(copyBootCmd.c_str());
    
    // Dismount the ISO
    std::string dismountCmd = "powershell -Command \"Dismount-DiskImage -ImagePath '" + isoPath + "'\"";
    exec(dismountCmd.c_str());
    
    // Check if EFI files were copied
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((destPath + "efi\\boot\\bootx64.efi").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }
    FindClose(hFind);
    
    return true;
}