#include "isocopymanager.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <cctype>
#include <set>
#include "../utils/Utils.h"

ISOCopyManager& ISOCopyManager::getInstance() {
    static ISOCopyManager instance;
    return instance;
}

ISOCopyManager::ISOCopyManager()
{
    isWindowsISO = false;
}

ISOCopyManager::~ISOCopyManager()
{
}

std::string ISOCopyManager::exec(const char* cmd) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "";
    }

    CloseHandle(hWrite);

    char buffer[128];
    std::string result = "";
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

DWORD CALLBACK CopyProgressRoutine(
    LARGE_INTEGER TotalFileSize,
    LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER StreamSize,
    LARGE_INTEGER StreamBytesTransferred,
    DWORD dwStreamNumber,
    DWORD dwCallbackReason,
    HANDLE hSourceFile,
    HANDLE hDestinationFile,
    LPVOID lpData
) {
    EventManager* eventManager = static_cast<EventManager*>(lpData);
    if (eventManager && TotalFileSize.QuadPart > 0) {
        eventManager->notifyDetailedProgress(TotalBytesTransferred.QuadPart, TotalFileSize.QuadPart, "Copiando ISO");
    }
    return PROGRESS_CONTINUE;
}

struct CopyProgressContext {
    EventManager* eventManager;
    long long totalSize;
    long long& copiedSoFar;
    std::string operation;
};

static DWORD CALLBACK CopyFileProgressRoutine(
    LARGE_INTEGER TotalFileSize,
    LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER StreamSize,
    LARGE_INTEGER StreamBytesTransferred,
    DWORD dwStreamNumber,
    DWORD dwCallbackReason,
    HANDLE hSourceFile,
    HANDLE hDestinationFile,
    LPVOID lpData
) {
    CopyProgressContext* ctx = static_cast<CopyProgressContext*>(lpData);
    if (ctx && ctx->eventManager && TotalFileSize.QuadPart > 0) {
        long long current = ctx->copiedSoFar + TotalBytesTransferred.QuadPart;
        ctx->eventManager->notifyDetailedProgress(current, ctx->totalSize, ctx->operation);
    }
    return PROGRESS_CONTINUE;
}

long long ISOCopyManager::getDirectorySize(const std::string& path) {
    long long size = 0;
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        std::string name = findData.cFileName;
        if (name != "." && name != "..") {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                size += getDirectorySize(path + "\\" + name);
            } else {
                size += ((long long)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return size;
}

bool ISOCopyManager::copyDirectoryWithProgress(const std::string& source, const std::string& dest, EventManager& eventManager, long long totalSize, long long& copiedSoFar, const std::set<std::string>& excludeDirs) {
    BOOL result = CreateDirectoryA(dest.c_str(), NULL);
    if (!result) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            std::ofstream errorLog(Utils::getExeDirectory() + "copy_error_log.txt", std::ios::app);
            errorLog << "Failed to create directory: " << dest << " Error code: " << error << "\n";
            errorLog.close();
            return false;
        }
    }
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((source + "\\*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return true;
    do {
        std::string name = findData.cFileName;
        if (name != "." && name != ".." && excludeDirs.find(name) == excludeDirs.end()) {
            std::string srcItem = source + "\\" + name;
            std::string destItem = dest + "\\" + name;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!copyDirectoryWithProgress(srcItem, destItem, eventManager, totalSize, copiedSoFar, excludeDirs)) {
                    std::ofstream errorLog(Utils::getExeDirectory() + "copy_error_log.txt", std::ios::app);
                    errorLog << "Failed to copy directory: " << srcItem << " to " << destItem << "\n";
                    errorLog.close();
                    FindClose(hFind);
                    return false;
                }
            } else {
                CopyProgressContext ctx = { &eventManager, totalSize, copiedSoFar, excludeDirs.empty() ? "Copiando EFI" : "Copiando contenido del ISO" };
                if (!CopyFileExA(srcItem.c_str(), destItem.c_str(), CopyFileProgressRoutine, &ctx, NULL, 0)) {
                    // Log the error
                    std::ofstream errorLog(Utils::getExeDirectory() + "copy_error_log.txt", std::ios::app);
                    errorLog << "Failed to copy file: " << srcItem << " to " << destItem << " Error code: " << GetLastError() << "\n";
                    errorLog.close();
                    FindClose(hFind);
                    return false;
                }
                long long fileSize = ((long long)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
                copiedSoFar += fileSize;
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return true;
}

bool ISOCopyManager::extractISOContents(EventManager& eventManager, const std::string& isoPath, const std::string& destPath, const std::string& espPath, bool extractContent)
{
    // Create log file for debugging
    std::ofstream logFile(Utils::getExeDirectory() + "iso_extract_log.txt");
    logFile << "Starting ISO extraction from: " << isoPath << "\n";
    logFile << "Destination (data): " << destPath << "\n";
    logFile << "ESP path: " << espPath << "\n";
    logFile << "Extract content: " << (extractContent ? "Yes" : "No") << "\n";
    
    long long isoSize = Utils::getFileSize(isoPath);
    long long copiedSoFar = 0;
    eventManager.notifyDetailedProgress(0, isoSize, "Montando ISO");
    
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
    
    eventManager.notifyDetailedProgress(isoSize / 10, isoSize, "Analizando contenido ISO");
    
    // List all files in the root of the ISO
    std::ofstream contentLog(Utils::getExeDirectory() + "iso_content.log");
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((sourcePath + "*").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string fileName = findData.cFileName;
            if (fileName != "." && fileName != "..") {
                contentLog << fileName << "\n";
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    contentLog.close();
    
    // Check if it's Windows ISO
    DWORD sourcesAttrs = GetFileAttributesA((sourcePath + "sources").c_str());
    bool hasSources = (sourcesAttrs != INVALID_FILE_ATTRIBUTES && (sourcesAttrs & FILE_ATTRIBUTE_DIRECTORY));
    bool isWindowsISO = false;
    if (hasSources) {
        DWORD installWimAttrs = GetFileAttributesA((sourcePath + "sources\\install.wim").c_str());
        DWORD installEsdAttrs = GetFileAttributesA((sourcePath + "sources\\install.esd").c_str());
        isWindowsISO = (installWimAttrs != INVALID_FILE_ATTRIBUTES) || (installEsdAttrs != INVALID_FILE_ATTRIBUTES);
    }
    logFile << "Is Windows ISO: " << (isWindowsISO ? "Yes" : "No") << "\n";
    
    if (extractContent) {
        // Extract all ISO contents to data partition, excluding EFI
        std::set<std::string> excludeDirs = {"efi", "EFI"};
        if (!copyDirectoryWithProgress(sourcePath, destPath, eventManager, isoSize, copiedSoFar, excludeDirs)) {
            logFile << "Failed to copy content\n";
            logFile.close();
            return false;
        }
    } else {
        logFile << "Skipping content extraction (RAMDISK mode)\n";
    }

    // Extract EFI directory to ESP
    logFile << "Extracting EFI directory to ESP\n";
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
    
    // Create destination EFI directory on ESP
    std::string efiDestPath = espPath + "EFI";
    if (!CreateDirectoryA(efiDestPath.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        logFile << "Failed to create destination EFI directory: " << efiDestPath << "\n";
        logFile.close();
        return false;
    }
    
    // Copy EFI files with progress
    std::set<std::string> noExclude;
    if (!copyDirectoryWithProgress(efiSourcePath, efiDestPath, eventManager, isoSize, copiedSoFar, noExclude)) {
        logFile << "Failed to copy EFI\n";
        logFile.close();
        return false;
    }
    
    // Check if bootx64.efi or bootia32.efi was copied to ESP
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
    eventManager.notifyDetailedProgress(isoSize * 9 / 10, isoSize, "Desmontando ISO");
    std::string dismountCmd = "powershell -Command \"Dismount-DiskImage -ImagePath '" + isoPath + "'\"";
    logFile << "Dismount command: " << dismountCmd << "\n";
    std::string dismountResult = exec(dismountCmd.c_str());
    logFile << "Dismount result: " << dismountResult << "\n";
    
    logFile << "EFI extraction " << (bootFileExists ? "SUCCESS" : "FAILED") << "\n";
    if (extractContent) {
        logFile << "Content extraction completed.\n";
    }
    logFile.close();
    
    eventManager.notifyDetailedProgress(0, 0, "");
    return bootFileExists;
}

bool ISOCopyManager::copyISOFile(EventManager& eventManager, const std::string& isoPath, const std::string& destPath)
{
    std::string destFile = destPath + "iso.iso";
    
    // Create log file for debugging
    std::ofstream logFile(Utils::getExeDirectory() + "iso_file_copy_log.txt");
    logFile << "Copying ISO file from: " << isoPath << "\n";
    logFile << "To: " << destFile << "\n";
    
    BOOL result = CopyFileExA(isoPath.c_str(), destFile.c_str(), CopyProgressRoutine, &eventManager, NULL, 0);
    if (result) {
        logFile << "ISO file copied successfully.\n";
        eventManager.notifyDetailedProgress(0, 0, ""); // Clear
    } else {
        logFile << "Failed to copy ISO file. Error: " << GetLastError() << "\n";
    }
    logFile.close();
    
    return result != FALSE;
}