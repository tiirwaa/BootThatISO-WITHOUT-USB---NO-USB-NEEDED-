#include "isocopymanager.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <cctype>

ISOCopyManager::ISOCopyManager()
{
    isWindowsISO = false;
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

bool ISOCopyManager::extractISOContents(const std::string& isoPath, const std::string& destPath)
{
    // Create log file for debugging
    std::ofstream logFile("iso_extract_log.txt");
    logFile << "Starting EFI extraction from: " << isoPath << "\n";
    logFile << "Destination: " << destPath << "\n";
    
    // Mount the ISO using PowerShell and get drive letter
    std::string mountCmd = "powershell -Command \"$iso = Mount-DiskImage -ImagePath '" + isoPath + "' -PassThru; $volume = Get-DiskImage -ImagePath '" + isoPath + "' | Get-Volume; if ($volume) { $volume.DriveLetter } else { 'FAILED' }\"";
    logFile << "Mount command: " << mountCmd << "\n";
    
    std::string mountResult = exec(mountCmd.c_str());
    logFile << "Mount result: '" << mountResult << "'\n";
    
    if (mountResult.empty() || mountResult.find("FAILED") != std::string::npos || mountResult.find("error") != std::string::npos) {
        logFile << "Failed to mount ISO\n";
        logFile.close();
        return false;
    }
    
    // Extract drive letter (remove whitespace and newlines)
    std::string driveLetterStr;
    for (char c : mountResult) {
        if (isalpha(c)) {
            driveLetterStr = c;
            break;
        }
    }
    
    if (driveLetterStr.empty()) {
        logFile << "Could not extract drive letter from mount result\n";
        logFile.close();
        return false;
    }
    
    std::string sourcePath = driveLetterStr + ":\\";
    logFile << "Source path: " << sourcePath << "\n";
    
    // Check if it's Windows ISO
    DWORD windowsAttrs = GetFileAttributesA((sourcePath + "windows").c_str());
    isWindowsISO = (windowsAttrs != INVALID_FILE_ATTRIBUTES && (windowsAttrs & FILE_ATTRIBUTE_DIRECTORY));
    logFile << "Is Windows ISO: " << (isWindowsISO ? "Yes" : "No") << "\n";
    
    if (isWindowsISO) {
        // Extract entire ISO contents for Windows ISOs
        logFile << "Extracting entire ISO contents for Windows ISO\n";
        std::string copyCmd = "robocopy \"" + sourcePath + "\" \"" + destPath + "\" /E /R:1 /W:1 /NFL /NDL";
        logFile << "Full extract command: " << copyCmd << "\n";
        std::string copyResult = exec(copyCmd.c_str());
        logFile << "Full extract result: " << copyResult << "\n";
        
        // Check if windows directory was copied
        std::string windowsDestPath = destPath + "windows";
        DWORD winDestAttrs = GetFileAttributesA(windowsDestPath.c_str());
        bool windowsExtracted = (winDestAttrs != INVALID_FILE_ATTRIBUTES && (winDestAttrs & FILE_ATTRIBUTE_DIRECTORY));
        logFile << "Windows directory extracted: " << (windowsExtracted ? "Yes" : "No") << "\n";
        
        // Dismount the ISO
        std::string dismountCmd = "powershell -Command \"Dismount-DiskImage -ImagePath '" + isoPath + "'\"";
        logFile << "Dismount command: " << dismountCmd << "\n";
        std::string dismountResult = exec(dismountCmd.c_str());
        logFile << "Dismount result: " << dismountResult << "\n";
        
        logFile << "ISO contents extraction " << (windowsExtracted ? "SUCCESS" : "FAILED") << "\n";
        logFile.close();
        
        return windowsExtracted;
    } else {
        // For non-Windows ISOs, extract only EFI directory
        logFile << "Extracting EFI directory for non-Windows ISO\n";
        // Check if source EFI directory exists
        std::string efiSourcePath = sourcePath + "efi";
        DWORD efiAttrs = GetFileAttributesA(efiSourcePath.c_str());
        if (efiAttrs == INVALID_FILE_ATTRIBUTES || !(efiAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
            logFile << "EFI directory not found at: " << efiSourcePath << "\n";
            // Try alternative paths
            std::string altEfiPath = sourcePath + "EFI";
            DWORD altAttrs = GetFileAttributesA(altEfiPath.c_str());
            if (altAttrs != INVALID_FILE_ATTRIBUTES && (altAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
                efiSourcePath = altEfiPath;
                logFile << "Found EFI directory at alternative path: " << efiSourcePath << "\n";
            } else {
                logFile << "EFI directory not found at alternative path either\n";
                logFile.close();
                return false;
            }
        }
        
        // Create destination EFI directory
        std::string efiDestPath = destPath + "efi";
        if (!CreateDirectoryA(efiDestPath.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            logFile << "Failed to create destination EFI directory: " << efiDestPath << "\n";
            logFile.close();
            return false;
        }
        
        // Copy EFI files using robocopy
        std::string copyCmd = "robocopy \"" + efiSourcePath + "\" \"" + efiDestPath + "\" /E /R:1 /W:1 /NFL /NDL";
        logFile << "EFI copy command: " << copyCmd << "\n";
        std::string copyResult = exec(copyCmd.c_str());
        logFile << "EFI copy result: " << copyResult << "\n";
        
        // Check if bootx64.efi or bootia32.efi was copied
        std::string bootFilePath = efiDestPath + "\\boot\\bootx64.efi";
        DWORD bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bool bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));

        if (!bootFileExists) {
            bootFilePath = efiDestPath + "\\boot\\bootia32.efi";
            bootAttrs = GetFileAttributesA(bootFilePath.c_str());
            bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
        }

        // Also check for alternative boot file names
        if (!bootFileExists) {
            std::string altBootPath = efiDestPath + "\\BOOT\\BOOTX64.EFI";
            DWORD altAttrs = GetFileAttributesA(altBootPath.c_str());
            bootFileExists = (altAttrs != INVALID_FILE_ATTRIBUTES && !(altAttrs & FILE_ATTRIBUTE_DIRECTORY));
            if (bootFileExists) {
                bootFilePath = altBootPath;
            } else {
                altBootPath = efiDestPath + "\\BOOT\\BOOTIA32.EFI";
                altAttrs = GetFileAttributesA(altBootPath.c_str());
                bootFileExists = (altAttrs != INVALID_FILE_ATTRIBUTES && !(altAttrs & FILE_ATTRIBUTE_DIRECTORY));
                if (bootFileExists) {
                    bootFilePath = altBootPath;
                }
            }
        }
        
        logFile << "Boot file check: " << bootFilePath << " - " << (bootFileExists ? "EXISTS" : "NOT FOUND") << "\n";
        
        // Dismount the ISO
        std::string dismountCmd = "powershell -Command \"Dismount-DiskImage -ImagePath '" + isoPath + "'\"";
        logFile << "Dismount command: " << dismountCmd << "\n";
        std::string dismountResult = exec(dismountCmd.c_str());
        logFile << "Dismount result: " << dismountResult << "\n";
        
        logFile << "EFI extraction " << (bootFileExists ? "SUCCESS" : "FAILED") << "\n";
        logFile.close();
        
        return bootFileExists;
    }
}

bool ISOCopyManager::copyISOFile(const std::string& isoPath, const std::string& destPath)
{
    std::string destFile = destPath + "iso.iso";
    
    // Create log file for debugging
    std::ofstream logFile("iso_file_copy_log.txt");
    logFile << "Copying ISO file from: " << isoPath << "\n";
    logFile << "To: " << destFile << "\n";
    
    BOOL result = CopyFileA(isoPath.c_str(), destFile.c_str(), FALSE);
    if (result) {
        logFile << "ISO file copied successfully.\n";
    } else {
        logFile << "Failed to copy ISO file. Error: " << GetLastError() << "\n";
    }
    logFile.close();
    
    return result != FALSE;
}