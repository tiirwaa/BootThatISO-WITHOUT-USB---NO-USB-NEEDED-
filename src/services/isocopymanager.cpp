#include "isocopymanager.h"
#include "../utils/constants.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <cctype>
#include <set>
#include <ctime>
#include <iomanip>
#include "../utils/Utils.h"

// Forward declarations for helper functions defined later in this file
static bool isValidPE(const std::string& path);
static uint16_t getPEMachine(const std::string& path);
static BOOL copyFileUtf8(const std::string& src, const std::string& dst);

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

void ISOCopyManager::validateAndFixEFIFiles(const std::string& efiDestPath, std::ofstream& logFile) {
    // Check common EFI boot files
    std::vector<std::string> efiFiles = {
        efiDestPath + "\\boot\\bootx64.efi",
        efiDestPath + "\\boot\\bootia32.efi", 
        efiDestPath + "\\BOOT\\BOOTX64.EFI",
        efiDestPath + "\\BOOT\\BOOTIA32.EFI"
    };
    
    for (const auto& efiFile : efiFiles) {
        DWORD attrs = GetFileAttributesA(efiFile.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!isValidPE(efiFile)) {
                logFile << this->getTimestamp() << "EFI file is invalid (not PE): " << efiFile << std::endl;
                // Try to replace with system EFI file
                std::string systemEFI = "C:\\Windows\\Boot\\EFI\\bootmgfw.efi";
                DWORD sysAttrs = GetFileAttributesA(systemEFI.c_str());
                if (sysAttrs != INVALID_FILE_ATTRIBUTES) {
                    // Ensure destination directory exists
                    size_t lastSlash = efiFile.find_last_of("\\");
                    if (lastSlash != std::string::npos) {
                        std::string dstDir = efiFile.substr(0, lastSlash);
                        CreateDirectoryA(dstDir.c_str(), NULL);
                    }
                    // Remove invalid file
                    DeleteFileA(efiFile.c_str());
                    // Copy system file
                    if (copyFileUtf8(systemEFI, efiFile)) {
                        uint16_t machine = getPEMachine(efiFile);
                        logFile << this->getTimestamp() << "Replaced invalid EFI file with system bootmgfw.efi: " << efiFile << " (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
                    } else {
                        logFile << this->getTimestamp() << "Failed to replace invalid EFI file: " << efiFile << std::endl;
                    }
                } else {
                    logFile << this->getTimestamp() << "System EFI file not found, cannot replace invalid file: " << efiFile << std::endl;
                }
            } else {
                uint16_t machine = getPEMachine(efiFile);
                logFile << this->getTimestamp() << "EFI file is valid: " << efiFile << " (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
            }
        }
    }
}

std::string ISOCopyManager::getTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::stringstream timeStream;
    timeStream << std::put_time(&localTime, "[%Y-%m-%d %H:%M:%S] ");
    return timeStream.str();
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
    if (eventManager) {
        if (eventManager->isCancelRequested()) {
            return PROGRESS_CANCEL;
        }
        if (TotalFileSize.QuadPart > 0) {
            eventManager->notifyDetailedProgress(TotalBytesTransferred.QuadPart, TotalFileSize.QuadPart, "Copiando ISO");
        }
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

void ISOCopyManager::listDirectoryRecursive(std::ofstream& log, const std::string& path, int depth, int maxDepth) {
    if (depth >= maxDepth) return;
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((path + "*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        std::string fileName = findData.cFileName;
        if (fileName != "." && fileName != "..") {
            std::string indent(depth * 2, ' ');
            log << indent << fileName;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                log << "/" << std::endl;
                listDirectoryRecursive(log, path + fileName + "\\", depth + 1, maxDepth);
            } else {
                log << std::endl;
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}

bool ISOCopyManager::copyDirectoryWithProgress(const std::string& source, const std::string& dest, EventManager& eventManager, long long totalSize, long long& copiedSoFar, const std::set<std::string>& excludeDirs) {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    // Skip creating directory if it's a drive root (e.g., "Z:\")
    bool isDriveRoot = (dest.length() == 3 && dest[1] == ':' && dest[2] == '\\');
    if (!isDriveRoot) {
        BOOL result = CreateDirectoryA(dest.c_str(), NULL);
        if (!result) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                std::ofstream errorLog(logDir + "\\" + COPY_ERROR_LOG_FILE, std::ios::app);
                errorLog << getTimestamp() << "Failed to create directory: " << dest << " Error code: " << error << "\n";
                errorLog.close();
                eventManager.notifyLogUpdate("Error: Failed to create directory " + dest + " (Error " + std::to_string(error) + ")\r\n");
                return false;
            } else {
                std::ofstream errorLog(logDir + "\\" + COPY_ERROR_LOG_FILE, std::ios::app);
                errorLog << getTimestamp() << "Directory already exists: " << dest << "\n";
                errorLog.close();
            }
        } else {
            std::ofstream errorLog(logDir + "\\" + COPY_ERROR_LOG_FILE, std::ios::app);
            errorLog << getTimestamp() << "Created directory: " << dest << "\n";
            errorLog.close();
        }
    }
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((source + "\\*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return true;
    do {
        std::string name = findData.cFileName;
        if (eventManager.isCancelRequested()) {
            FindClose(hFind);
            return false;
        }

        if (name != "." && name != ".." && excludeDirs.find(name) == excludeDirs.end()) {
            std::string srcItem = source + "\\" + name;
            std::string destItem = dest + "\\" + name;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!copyDirectoryWithProgress(srcItem, destItem, eventManager, totalSize, copiedSoFar, excludeDirs)) {
                    if (eventManager.isCancelRequested()) {
                        FindClose(hFind);
                        return false;
                    }
                    std::ofstream errorLog(logDir + "\\" + COPY_ERROR_LOG_FILE, std::ios::app);
                    errorLog << getTimestamp() << "Failed to copy directory: " << srcItem << " to " << destItem << "\n";
                    errorLog.close();
                    eventManager.notifyLogUpdate("Error: Failed to copy directory " + srcItem + " to " + destItem + "\r\n");
                    FindClose(hFind);
                    return false;
                }
            } else {
                // Log source file info before copying only on error
                long long fileSize = ((long long)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;

                // Attempt to normalize source file attributes to avoid copy issues
                SetFileAttributesA(srcItem.c_str(), FILE_ATTRIBUTE_NORMAL);

                // Also normalize destination file attributes if it exists
                DWORD destAttrs = GetFileAttributesA(destItem.c_str());
                if (destAttrs != INVALID_FILE_ATTRIBUTES) {
                    SetFileAttributesA(destItem.c_str(), FILE_ATTRIBUTE_NORMAL);
                }

                if (eventManager.isCancelRequested()) {
                    FindClose(hFind);
                    return false;
                }

                if (!copyFileUtf8(srcItem, destItem)) {
                    DWORD error = GetLastError();
                    std::ofstream errorLog2(logDir + "\\" + COPY_ERROR_LOG_FILE, std::ios::app);
                    errorLog2 << getTimestamp() << "Failed to copy file: " << srcItem << " to " << destItem << " Error code: " << error << "\n";
                    // Additional error details
                    errorLog2 << getTimestamp() << "Error description: ";
                    switch (error) {
                        case ERROR_ACCESS_DENIED: errorLog2 << "Access denied. Check permissions.\n"; break;
                        case ERROR_FILE_NOT_FOUND: errorLog2 << "Source file not found.\n"; break;
                        case ERROR_PATH_NOT_FOUND: errorLog2 << "Path not found.\n"; break;
                        case ERROR_INVALID_DRIVE: errorLog2 << "Invalid drive.\n"; break;
                        case ERROR_SHARING_VIOLATION: errorLog2 << "Sharing violation.\n"; break;
                        default: errorLog2 << "Unknown error.\n"; break;
                    }
                    // Check destination attributes
                    DWORD destAttrs = GetFileAttributesA(destItem.c_str());
                    errorLog2 << getTimestamp() << "Destination attributes after failure: " << destAttrs << "\n";
                    errorLog2.close();
                    eventManager.notifyLogUpdate("Error: Failed to copy file " + srcItem + " to " + destItem + " (Error " + std::to_string(error) + ")\r\n");
                    FindClose(hFind);
                    return false;
                }
                // If this is an EFI/PE file, do a quick validation to ensure it's a valid PE image
                auto isEfiFile = [](const std::string& p)->bool {
                    size_t pos = p.find_last_of('.');
                    if (pos == std::string::npos) return false;
                    std::string ext = p.substr(pos);
                    for (auto &c : ext) c = (char)tolower((unsigned char)c);
                    return ext == ".efi" || ext == ".exe" || ext == ".dll";
                };
                if (isEfiFile(destItem)) {
                    if (!isValidPE(destItem)) {
                        std::ofstream errorLog2(logDir + "\\" + COPY_ERROR_LOG_FILE, std::ios::app);
                        errorLog2 << getTimestamp() << "Copied file appears invalid (not PE): " << destItem << "\n";
                        errorLog2.close();
                        eventManager.notifyLogUpdate("Error: Copied file appears invalid (not PE): " + destItem + "\r\n");
                        FindClose(hFind);
                        return false;
                    }
                }

                copiedSoFar += fileSize;
                eventManager.notifyDetailedProgress(copiedSoFar, totalSize, excludeDirs.empty() ? "Copiando EFI" : "Copiando contenido del ISO");
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return true;
}

bool ISOCopyManager::extractISOContents(EventManager& eventManager, const std::string& isoPath, const std::string& destPath, const std::string& espPath, bool extractContent)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    // Create log file for debugging
    std::ofstream logFile(logDir + "\\" + ISO_EXTRACT_LOG_FILE);
    logFile << getTimestamp() << "Starting ISO extraction from: " << isoPath << std::endl;
    logFile << getTimestamp() << "Destination (data): " << destPath << std::endl;
    logFile << getTimestamp() << "ESP path: " << espPath << std::endl;
    logFile << getTimestamp() << "Extract content: " << (extractContent ? "Yes" : "No") << std::endl;
    
    long long isoSize = Utils::getFileSize(isoPath);
    long long copiedSoFar = 0;
    eventManager.notifyDetailedProgress(0, isoSize, "Montando ISO");
    
    // Mount the ISO using PowerShell and get drive letter
    std::string mountCmd = "powershell -Command \"$iso = Mount-DiskImage -ImagePath '" + isoPath + "' -PassThru; $volume = Get-DiskImage -ImagePath '" + isoPath + "' | Get-Volume; if ($volume) { $volume.DriveLetter } else { 'FAILED' }\"";
    logFile << getTimestamp() << "Mount command: " << mountCmd << std::endl;
    
    std::string mountResult = exec(mountCmd.c_str());
    // Check if mount was successful by looking for a drive letter in the result
    std::string driveLetterStr;
    for (char c : mountResult) {
        if (isalpha(c)) {
            driveLetterStr = c;
            break;
        }
    }
    bool mountSuccess = !driveLetterStr.empty();
    logFile << getTimestamp() << "Mount " << (mountSuccess ? "successful" : "failed") << std::endl;
    
    if (!mountSuccess) {
        logFile << getTimestamp() << "Failed to mount ISO" << std::endl;
        logFile.close();
        eventManager.notifyLogUpdate("Error: Falló el montaje del ISO.\r\n");
        return false;
    }
    
    eventManager.notifyLogUpdate("ISO montado exitosamente en " + driveLetterStr + ":.\r\n");
    
    std::string sourcePath = driveLetterStr + ":\\";
    logFile << getTimestamp() << "Source path: " << sourcePath << std::endl;
    
    // Add delay to allow the drive to be ready
    logFile << getTimestamp() << "Waiting 5 seconds for drive to be ready" << std::endl;
    Sleep(5000);
    logFile << getTimestamp() << "Drive ready, proceeding with analysis" << std::endl;

    // Prepare dismount command so we can ensure unmount on early exit
    std::string dismountCmd = "powershell -Command \"Dismount-DiskImage -ImagePath '" + isoPath + "'\"";

    if (eventManager.isCancelRequested()) {
        logFile << getTimestamp() << "Operation cancelled after mount preparation\n";
        exec(dismountCmd.c_str());
        logFile.close();
        return false;
    }
    
    eventManager.notifyDetailedProgress(isoSize / 10, isoSize, "Analizando contenido ISO");
    
    // List all files and directories recursively in the ISO (up to 10 levels deep)
    std::ofstream contentLog(logDir + "\\" + ISO_CONTENT_LOG_FILE);
    listDirectoryRecursive(contentLog, sourcePath, 0, 10);
    contentLog.close();
    
    eventManager.notifyLogUpdate("Analizando contenido del ISO...\r\n");
    
    // Check if it's Windows ISO
    DWORD sourcesAttrs = GetFileAttributesA((sourcePath + "sources").c_str());
    bool hasSources = (sourcesAttrs != INVALID_FILE_ATTRIBUTES && (sourcesAttrs & FILE_ATTRIBUTE_DIRECTORY));
    bool isWindowsISO = false;
    if (hasSources) {
        DWORD installWimAttrs = GetFileAttributesA((sourcePath + "sources\\install.wim").c_str());
        DWORD installEsdAttrs = GetFileAttributesA((sourcePath + "sources\\install.esd").c_str());
        isWindowsISO = (installWimAttrs != INVALID_FILE_ATTRIBUTES) || (installEsdAttrs != INVALID_FILE_ATTRIBUTES);
    }
    logFile << getTimestamp() << "Is Windows ISO: " << (isWindowsISO ? "Yes" : "No") << std::endl;
    
    if (isWindowsISO) {
        eventManager.notifyLogUpdate("ISO de Windows detectado.\r\n");
    } else {
        eventManager.notifyLogUpdate("ISO no-Windows detectado.\r\n");
    }
    
    if (extractContent) {
        // Extract all ISO contents to data partition, excluding EFI
        std::set<std::string> excludeDirs = {"efi", "EFI"};
        if (!copyDirectoryWithProgress(sourcePath, destPath, eventManager, isoSize, copiedSoFar, excludeDirs)) {
            logFile << getTimestamp() << "Failed to copy content or cancelled" << std::endl;
            // Ensure ISO is dismounted
            exec(dismountCmd.c_str());
            logFile.close();
            return false;
        }
    } else {
        logFile << getTimestamp() << "Skipping content extraction (Boot desde Memoria mode)" << std::endl;
    }

    // Extract EFI directory to ESP
    logFile << getTimestamp() << "Extracting EFI directory to ESP" << std::endl;
    eventManager.notifyLogUpdate("Extrayendo archivos EFI al ESP...\r\n");
    // Check if source EFI directory exists
    std::string efiSourcePath = sourcePath + "efi";
    DWORD efiAttrs = GetFileAttributesA(efiSourcePath.c_str());
    if (efiAttrs == INVALID_FILE_ATTRIBUTES || !(efiAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        logFile << getTimestamp() << "EFI directory not found at: " << efiSourcePath << std::endl;
        // Try alternative paths
        std::string altEfiPath = sourcePath + "EFI";
        DWORD altAttrs = GetFileAttributesA(altEfiPath.c_str());
        if (altAttrs != INVALID_FILE_ATTRIBUTES && (altAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
            efiSourcePath = altEfiPath;
            logFile << getTimestamp() << "Found EFI directory at alternative path: " << efiSourcePath << std::endl;
        } else {
            logFile << getTimestamp() << "EFI directory not found at alternative path either" << std::endl;
            logFile.close();
            return false;
        }
    }
    
    // Create destination EFI directory on ESP
    std::string efiDestPath = espPath + "EFI";
    if (!CreateDirectoryA(efiDestPath.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        logFile << getTimestamp() << "Failed to create destination EFI directory: " << efiDestPath << std::endl;
        logFile.close();
        return false;
    }
    
    // Copy EFI files with progress
    std::set<std::string> noExclude;
    if (!copyDirectoryWithProgress(efiSourcePath, efiDestPath, eventManager, isoSize, copiedSoFar, noExclude)) {
        logFile << getTimestamp() << "Failed to copy EFI or cancelled" << std::endl;
        exec(dismountCmd.c_str());
        logFile.close();
        return false;
    }
    
    // Validate and fix EFI files after copying
    logFile << getTimestamp() << "Validating copied EFI files" << std::endl;
    validateAndFixEFIFiles(efiDestPath, logFile);
    
    // For non-Windows ISOs, copy bootmgr.efi from root to ESP if it exists
    std::string bootmgrSource = sourcePath + "bootmgr.efi";
    bool bootmgrCopied = false;
    if (GetFileAttributesA(bootmgrSource.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string bootmgrDest = espPath + "EFI\\Microsoft\\Boot\\bootmgr.efi";
        // Ensure destination directory exists
        std::string bootmgrDir = espPath + "EFI\\Microsoft\\Boot";
        CreateDirectoryA(bootmgrDir.c_str(), NULL); // Ignore error if exists
        DWORD destAttrs = GetFileAttributesA(bootmgrDest.c_str());
        if (destAttrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesA(bootmgrDest.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
                if (copyFileUtf8(bootmgrSource, bootmgrDest)) {
            logFile << getTimestamp() << "Copied bootmgr.efi to ESP" << std::endl;
            bootmgrCopied = true;
        } else {
            logFile << getTimestamp() << "Failed to copy bootmgr.efi to ESP, error: " << GetLastError() << std::endl;
            // Try cmd copy
            std::string cmdCopy = "cmd /c copy \"" + bootmgrSource + "\" \"" + bootmgrDest + "\" >nul 2>&1";
            std::string copyResult = exec(cmdCopy.c_str());
            if (GetFileAttributesA(bootmgrDest.c_str()) != INVALID_FILE_ATTRIBUTES) {
                logFile << getTimestamp() << "Copied bootmgr.efi to ESP using cmd" << std::endl;
                bootmgrCopied = true;
            } else {
                logFile << getTimestamp() << "Failed to copy bootmgr.efi to ESP using cmd" << std::endl;
            }
        }
        
        // For non-Windows ISOs, do not copy bootmgr.efi to BOOTX64.EFI as it may overwrite the correct EFI boot file from the ISO
    }
    
    // If boot.wim exists and this is a Windows ISO, extract additional boot files from it
    std::string bootWimPath = sourcePath + "sources\\boot.wim";
    if (isWindowsISO && GetFileAttributesA(bootWimPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        logFile << getTimestamp() << "Extracting additional boot files from boot.wim" << std::endl;
            // Create temp dir for mounting
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string tempDir = std::string(tempPath) + "EasyISOBoot_WimMount\\";
            if (!CreateDirectoryA(tempDir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                logFile << getTimestamp() << "Failed to create temp dir for WIM mount" << std::endl;
            } else {
                // Mount boot.wim index 1
                std::string mountCmd = "dism /Mount-Wim /WimFile:\"" + bootWimPath + "\" /index:1 /MountDir:\"" + tempDir + "\" /ReadOnly";
                logFile << getTimestamp() << "Mount command: " << mountCmd << std::endl;
                std::string mountResult = exec(mountCmd.c_str());
                bool mountSuccessWim = mountResult.find("The operation completed successfully") != std::string::npos;
                logFile << getTimestamp() << "Mount WIM " << (mountSuccessWim ? "successful" : "failed") << std::endl;
                if (!mountSuccessWim) {
                    logFile << getTimestamp() << "DISM output: " << mountResult << std::endl;
                }
                
                if (mountSuccessWim) {
                    // Extract EFI files to ESP
                    std::vector<std::pair<std::string, std::string>> efiFiles = {
                        {"Windows\\Boot\\EFI\\bootx64.efi", espPath + "EFI\\BOOT\\BOOTX64.EFI"},
                        {"Windows\\Boot\\EFI\\bootmgfw.efi", espPath + "EFI\\Microsoft\\Boot\\bootmgfw.efi"},
                        {"Windows\\Boot\\EFI\\bootmgr.efi", espPath + "EFI\\Microsoft\\Boot\\bootmgr.efi"},
                        {"Windows\\Boot\\BCD", espPath + "EFI\\Microsoft\\Boot\\BCD"},
                        {"Windows\\System32\\memtest.efi", espPath + "EFI\\Microsoft\\Boot\\memtest.efi"}
                    };
                    
                    for (auto& filePair : efiFiles) {
                        std::string src = tempDir + filePair.first;
                        std::string dst = filePair.second;
                        // Ensure destination directory exists
                        size_t lastSlash = dst.find_last_of("\\");
                        if (lastSlash != std::string::npos) {
                            std::string dstDir = dst.substr(0, lastSlash);
                            CreateDirectoryA(dstDir.c_str(), NULL); // Ignore error if exists
                        }
                                        if (copyFileUtf8(src, dst)) {
                                        // Validate PE for EFI files and log machine type
                                        bool valid = true;
                                        uint16_t machine = 0;
                                        size_t pos = dst.find_last_of('.');
                                        if (pos != std::string::npos) {
                                            std::string ext = dst.substr(pos);
                                            for (auto &c : ext) c = (char)tolower((unsigned char)c);
                                            if (ext == ".efi") {
                                                valid = isValidPE(dst);
                                                machine = getPEMachine(dst);
                                            }
                                        }
                                        if (!valid) {
                                            logFile << getTimestamp() << "Copied EFI file appears invalid: " << dst << std::endl;
                                        } else {
                                            logFile << getTimestamp() << "Instalado: " << filePair.first << " to " << dst << " (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
                                        }
                                    } else {
                                        logFile << getTimestamp() << "Failed to extract: " << filePair.first << " error: " << GetLastError() << std::endl;
                                    }
                    }
                    
                    // Extract data files if extracting content
                    if (extractContent) {
                        std::vector<std::pair<std::string, std::string>> dataFiles = {
                            {"Boot\\BCD", destPath + "boot\\bcd"},
                            {"Boot\\boot.sdi", destPath + "boot\\boot.sdi"},
                            {"Windows\\Boot\\EFI\\bootmgfw.efi", destPath + "bootmgr.efi"}
                        };
                        
                        for (auto& filePair : dataFiles) {
                            std::string src = tempDir + filePair.first;
                            std::string dst = filePair.second;
                            size_t lastSlash = dst.find_last_of("\\");
                            if (lastSlash != std::string::npos) {
                                std::string dstDir = dst.substr(0, lastSlash);
                                CreateDirectoryA(dstDir.c_str(), NULL);
                            }
                            if (copyFileUtf8(src, dst)) {
                                uint16_t machine = 0;
                                size_t pos = dst.find_last_of('.');
                                if (pos != std::string::npos) {
                                    std::string ext = dst.substr(pos);
                                    for (auto &c : ext) c = (char)tolower((unsigned char)c);
                                    if (ext == ".efi") machine = getPEMachine(dst);
                                }
                                logFile << getTimestamp() << "Instalado: " << filePair.first << " to " << dst << " (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
                            } else {
                                logFile << getTimestamp() << "Failed to extract: " << filePair.first << " error: " << GetLastError() << std::endl;
                            }
                        }
                        
                        // Copy fonts and resources directories
                        std::string fontsSrc = tempDir + "Windows\\Boot\\Fonts";
                        std::string fontsDst = destPath + "boot\\fonts";
                        if (GetFileAttributesA(fontsSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            copyDirectoryWithProgress(fontsSrc, fontsDst, eventManager, isoSize, copiedSoFar, noExclude);
                        }
                        
                        std::string resourcesSrc = tempDir + "Windows\\Boot\\Resources";
                        std::string resourcesDst = destPath + "boot\\resources";
                        if (GetFileAttributesA(resourcesSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            copyDirectoryWithProgress(resourcesSrc, resourcesDst, eventManager, isoSize, copiedSoFar, noExclude);
                        }
                        
                        // Copy setup.exe from ISO
                        std::string setupSrc = sourcePath + "sources\\setup.exe";
                        std::string setupDst = destPath + "setup.exe";
                        if (copyFileUtf8(setupSrc, setupDst)) {
                            logFile << getTimestamp() << "Copied setup.exe" << std::endl;
                        }
                    }
                    
                    // Unmount WIM
                    std::string unmountCmd = "dism /Unmount-Wim /MountDir:\"" + tempDir + "\" /discard";
                    logFile << getTimestamp() << "Unmount command: " << unmountCmd << std::endl;
                    std::string unmountResult = exec(unmountCmd.c_str());
                    bool unmountSuccess = unmountResult.find("The operation completed successfully") != std::string::npos;
                    logFile << getTimestamp() << "Unmount WIM " << (unmountSuccess ? "successful" : "failed") << std::endl;
                    if (!unmountSuccess) {
                        logFile << getTimestamp() << "DISM unmount output: " << unmountResult << std::endl;
                    }
                    
                    // Remove temp dir
                    RemoveDirectoryA(tempDir.c_str());
                } else {
                    logFile << getTimestamp() << "Failed to mount boot.wim" << std::endl;
                }
            }
        }
    
    // Check if bootx64.efi or bootia32.efi was copied to ESP
    std::string bootFilePath = efiDestPath + "\\BOOT\\BOOTX64.EFI";
    DWORD bootAttrs = GetFileAttributesA(bootFilePath.c_str());
    bool bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));

    if (!bootFileExists) {
        bootFilePath = efiDestPath + "\\boot\\BOOTX64.EFI";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = efiDestPath + "\\boot\\bootx64.efi";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = efiDestPath + "\\BOOT\\BOOTIA32.EFI";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = efiDestPath + "\\boot\\BOOTIA32.EFI";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = efiDestPath + "\\boot\\bootia32.efi";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }
    
    logFile << getTimestamp() << "Boot file check: " << bootFilePath << " - " << (bootFileExists ? "EXISTS" : "NOT FOUND") << std::endl;
    
    // If BOOTX64.EFI is missing, copy from system to ensure compatibility
    if (!bootFileExists) {
        logFile << getTimestamp() << "Copying BOOTX64.EFI from system for compatibility" << std::endl;
        // Create EFI\BOOT directory
        std::string bootDir = efiDestPath + "\\BOOT";
        if (!CreateDirectoryA(bootDir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            logFile << getTimestamp() << "Failed to create BOOT directory" << std::endl;
        } else {
            std::string systemBootmgr = "C:\\Windows\\Boot\\EFI\\bootmgfw.efi";
            std::string destBootx64 = bootDir + "\\BOOTX64.EFI";
            // Only copy system boot file if BOOTX64.EFI does not already exist to avoid overwriting
            DWORD existingAttrs = GetFileAttributesA(destBootx64.c_str());
            bool shouldCopy = (existingAttrs == INVALID_FILE_ATTRIBUTES);
            if (!shouldCopy) {
                // If exists, compare sizes to decide
                WIN32_FILE_ATTRIBUTE_DATA srcData, dstData;
                if (GetFileAttributesExA(systemBootmgr.c_str(), GetFileExInfoStandard, &srcData) && GetFileAttributesExA(destBootx64.c_str(), GetFileExInfoStandard, &dstData)) {
                    ULONGLONG srcSize = ((ULONGLONG)srcData.nFileSizeHigh << 32) | srcData.nFileSizeLow;
                    ULONGLONG dstSize = ((ULONGLONG)dstData.nFileSizeHigh << 32) | dstData.nFileSizeLow;
                    if (srcSize != dstSize) shouldCopy = true;
                } else {
                    shouldCopy = true; // can't compare, try copying
                }
            }

            if (shouldCopy) {
                if (copyFileUtf8(systemBootmgr, destBootx64)) {
                    uint16_t machine = getPEMachine(destBootx64);
                    logFile << getTimestamp() << "Successfully copied bootmgfw.efi as BOOTX64.EFI (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
                    bootFileExists = true;
                    bootFilePath = destBootx64;
                } else {
                    logFile << getTimestamp() << "Failed to copy bootmgfw.efi, error: " << GetLastError() << std::endl;
                    // Try alternative path
                    std::string altSystemBoot = "C:\\Windows\\Boot\\EFI\\bootx64.efi";
                    if (copyFileUtf8(altSystemBoot, destBootx64)) {
                        uint16_t machine = getPEMachine(destBootx64);
                        logFile << getTimestamp() << "Successfully copied bootx64.efi as BOOTX64.EFI (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
                        bootFileExists = true;
                        bootFilePath = destBootx64;
                    } else {
                        logFile << getTimestamp() << "Failed to copy bootx64.efi, error: " << GetLastError() << std::endl;
                    }
                }
            } else {
                logFile << getTimestamp() << "BOOTX64.EFI already exists on ESP, skipping system copy\n";
                bootFileExists = true;
                bootFilePath = destBootx64;
            }
        }
    }
    
    // Dismount the ISO
    eventManager.notifyDetailedProgress(isoSize * 9 / 10, isoSize, "Desmontando ISO");
    logFile << getTimestamp() << "Dismount command: " << dismountCmd << std::endl;
    std::string dismountResult = exec(dismountCmd.c_str());
    logFile << getTimestamp() << "Dismount ISO completed" << std::endl;
    
    logFile << getTimestamp() << "EFI extraction " << (bootFileExists ? "SUCCESS" : "FAILED") << std::endl;
    if (extractContent) {
        logFile << getTimestamp() << "Content extraction completed." << std::endl;
    }
    logFile.close();
    
    eventManager.notifyDetailedProgress(0, 0, "");
    return bootFileExists;
}

bool ISOCopyManager::copyISOFile(EventManager& eventManager, const std::string& isoPath, const std::string& destPath)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::string destFile = destPath + "iso.iso";
    
    // Create log file for debugging
    std::ofstream logFile(logDir + "\\" + ISO_FILE_COPY_LOG_FILE);
    logFile << getTimestamp() << "Copying ISO file from: " << isoPath << std::endl;
    logFile << getTimestamp() << "To: " << destFile << std::endl;
    
    BOOL result = CopyFileExA(isoPath.c_str(), destFile.c_str(), CopyProgressRoutine, &eventManager, NULL, 0);
    if (result) {
        logFile << getTimestamp() << "ISO file copied successfully." << std::endl;
        eventManager.notifyDetailedProgress(0, 0, ""); // Clear
    } else {
        logFile << getTimestamp() << "Failed to copy ISO file. Error: " << GetLastError() << std::endl;
    }
    logFile.close();
    
    return result != FALSE;
}

// Quick validation for PE/EFI files: check for 'MZ' header
static bool isValidPE(const std::string& path) {
    std::wstring wpath = Utils::utf8_to_wstring(path);
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CHAR hdr[2] = {0};
    DWORD read = 0;
    BOOL ok = ReadFile(h, hdr, 2, &read, NULL);
    CloseHandle(h);
    if (!ok || read < 2) return false;
    return hdr[0] == 'M' && hdr[1] == 'Z';
}

// Returns IMAGE_FILE_MACHINE_* value or 0 on failure
static uint16_t getPEMachine(const std::string& path) {
    std::wstring wpath = Utils::utf8_to_wstring(path);
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD read = 0;
    DWORD e_lfanew = 0;
    // Read e_lfanew at offset 0x3C (DWORD)
    SetFilePointer(h, 0x3C, NULL, FILE_BEGIN);
    if (!ReadFile(h, &e_lfanew, sizeof(DWORD), &read, NULL) || read != sizeof(DWORD)) { CloseHandle(h); return 0; }
    // Seek to PE signature + Machine (e_lfanew + 4)
    DWORD machine = 0;
    SetFilePointer(h, e_lfanew + 4, NULL, FILE_BEGIN);
    if (!ReadFile(h, &machine, sizeof(uint16_t), &read, NULL) || read != sizeof(uint16_t)) { CloseHandle(h); return 0; }
    CloseHandle(h);
    return static_cast<uint16_t>(machine & 0xFFFF);
}

// Copy using wide APIs from UTF-8 input paths
static BOOL copyFileUtf8(const std::string& src, const std::string& dst) {
    std::wstring wsrc = Utils::utf8_to_wstring(src);
    std::wstring wdst = Utils::utf8_to_wstring(dst);
    return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE);
}