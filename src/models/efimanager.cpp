#include "efimanager.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <vector>
#include <set>
#include <algorithm>
#include "../utils/Utils.h"
#include "filecopymanager.h"

EFIManager::EFIManager(EventManager& eventManager, FileCopyManager& fileCopyManager)
    : eventManager(eventManager), fileCopyManager(fileCopyManager)
{
}

EFIManager::~EFIManager()
{
}

bool EFIManager::extractEFI(const std::string& sourcePath, const std::string& espPath, bool isWindowsISO, long long& copiedSoFar, long long isoSize)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile(logDir + "\\iso_extract.log", std::ios::app);
    logFile << getTimestamp() << "Extracting EFI directory to ESP" << std::endl;
    eventManager.notifyLogUpdate("Extrayendo archivos EFI al ESP...\r\n");

    // Check if source EFI directory exists
    std::string efiSourcePath = sourcePath + "efi";
    DWORD efiAttrs = GetFileAttributesA(efiSourcePath.c_str());
    if (efiAttrs == INVALID_FILE_ATTRIBUTES || !(efiAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
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
    if (!fileCopyManager.copyDirectoryWithProgress(efiSourcePath, efiDestPath, isoSize, copiedSoFar, noExclude, "Copiando EFI")) {
        logFile << getTimestamp() << "Failed to copy EFI or cancelled" << std::endl;
        logFile.close();
        return false;
    }

    // Validate and fix EFI files after copying
    validateAndFixEFIFiles(efiDestPath, logFile);

    // For non-Windows ISOs, copy bootmgr.efi from root to ESP if it exists
    if (!isWindowsISO) {
        copyBootmgrForNonWindows(sourcePath, espPath);
    }

    // For Windows ISOs, ensure we have a Secure Boot compatible bootloader
    if (isWindowsISO) {
        ensureSecureBootCompatibleBootloader(espPath);
    }

    // Check if bootx64.efi or bootia32.efi was copied to ESP
    return ensureBootFileExists(espPath);
}

bool EFIManager::extractEFIDirectory(const std::string& sourcePath, const std::string& espPath, long long& copiedSoFar, long long isoSize)
{
    // Implementation similar to original
    return true;
}

bool EFIManager::extractBootFilesFromWIM(const std::string& sourcePath, const std::string& espPath, const std::string& dataPath, long long& copiedSoFar, long long isoSize)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    std::ofstream logFile(logDir + "\\iso_extract.log", std::ios::app);

    auto ensureDirectoryRecursive = [](const std::string& directory) {
        if (directory.empty()) return;
        std::string normalized = directory;
        std::replace(normalized.begin(), normalized.end(), '/', '\\');
        if (normalized.empty()) return;
        if (normalized.back() == '\\') normalized.pop_back();
        size_t start = 0;
        if (normalized.size() >= 2 && normalized[1] == ':') {
            start = 2;
        }
        while (start < normalized.size()) {
            size_t pos = normalized.find('\\', start);
            std::string current = normalized.substr(0, pos == std::string::npos ? normalized.size() : pos);
            if (!current.empty() && current.back() != ':') {
                CreateDirectoryA(current.c_str(), NULL);
            }
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
        if (!normalized.empty() && normalized.back() != ':') {
            CreateDirectoryA(normalized.c_str(), NULL);
        }
    };

    std::string bootWimPath = sourcePath + "sources\\boot.wim";
    if (GetFileAttributesA(bootWimPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        logFile << getTimestamp() << "boot.wim not found while extracting additional boot files" << std::endl;
        return false;
    }

    logFile << getTimestamp() << "Extracting additional boot files from boot.wim" << std::endl;

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string tempDir = std::string(tempPath) + "EasyISOBoot_WimMount\\";

    ensureTempDirectoryClean(tempDir);

    if (!(CreateDirectoryA(tempDir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS)) {
        logFile << getTimestamp() << "Failed to create temp dir for WIM mount" << std::endl;
        return false;
    }

    if (!isDirectoryEmpty(tempDir)) {
        logFile << getTimestamp() << "Temp directory not empty after cleanup, skipping WIM mount" << std::endl;
        RemoveDirectoryA(tempDir.c_str());
        return false;
    }

    std::string mountCmd = "cmd /c dism /Mount-Wim /WimFile:\"" + bootWimPath + "\" /index:1 /MountDir:\"" + tempDir.substr(0, tempDir.size()-1) + "\" /ReadOnly";
    logFile << getTimestamp() << "Mount command: " << mountCmd << std::endl;
    std::string mountResult = exec(mountCmd.c_str());
    bool mountSuccessWim = mountResult.find("The operation completed successfully") != std::string::npos;
    logFile << getTimestamp() << "Mount WIM " << (mountSuccessWim ? "successful" : "failed") << std::endl;

    if (!mountSuccessWim) {
        logFile << getTimestamp() << "DISM output: " << mountResult << std::endl;
        RemoveDirectoryA(tempDir.c_str());
        return false;
    }

    bool overallSuccess = true;
    std::string normalizedEsp = espPath;
    if (!normalizedEsp.empty() && normalizedEsp.back() != '\\') normalizedEsp += "\\";
    std::string normalizedData = dataPath;
    if (!normalizedData.empty() && normalizedData.back() != '\\') normalizedData += "\\";

    std::vector<std::pair<std::string, std::string>> fileCopies = {
        {"Windows\\Boot\\EFI\\bootmgfw.efi", normalizedEsp + "EFI\\Microsoft\\Boot\\bootmgfw.efi"}
    };

    if (!normalizedData.empty()) {
        fileCopies.push_back({"Windows\\System32\\Boot\\winload.efi", normalizedData + "Windows\\System32\\Boot\\winload.efi"});
        fileCopies.push_back({"Windows\\System32\\Boot\\winload.exe", normalizedData + "Windows\\System32\\Boot\\winload.exe"});
    }

    for (auto& filePair : fileCopies) {
        std::string src = tempDir + filePair.first;
        std::string dst = filePair.second;
        if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
            logFile << getTimestamp() << "File not found in boot.wim: " << filePair.first << std::endl;
            overallSuccess = false;
            continue;
        }

        size_t lastSlash = dst.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            std::string dstDir = dst.substr(0, lastSlash);
            ensureDirectoryRecursive(dstDir);
        }

        if (copyFileUtf8(src, dst)) {
            bool valid = true;
            uint16_t machine = 0;
            size_t pos = dst.find_last_of('.');
            if (pos != std::string::npos) {
                std::string ext = dst.substr(pos);
                for (auto& c : ext) c = (char)tolower((unsigned char)c);
                if (ext == ".efi" || ext == ".exe") {
                    valid = isValidPE(dst);
                    machine = getPEMachine(dst);
                }
            }
            if (!valid) {
                logFile << getTimestamp() << "Copied file appears invalid: " << dst << std::endl;
                overallSuccess = false;
            } else {
                logFile << getTimestamp() << "Extracted: " << filePair.first << " -> " << dst;
                if (machine != 0) {
                    logFile << " (machine=0x" << std::hex << machine << std::dec << ")";
                }
                logFile << std::endl;
            }
        } else {
            logFile << getTimestamp() << "Failed to extract: " << filePair.first << " error: " << GetLastError() << std::endl;
            overallSuccess = false;
        }
    }

    std::string unmountCmd = "cmd /c dism /Unmount-Wim /MountDir:\"" + tempDir.substr(0, tempDir.size()-1) + "\" /discard";
    logFile << getTimestamp() << "Unmount command: " << unmountCmd << std::endl;
    std::string unmountResult = exec(unmountCmd.c_str());
    bool unmountSuccess = unmountResult.find("The operation completed successfully") != std::string::npos;
    logFile << getTimestamp() << "Unmount WIM " << (unmountSuccess ? "successful" : "failed") << std::endl;
    if (!unmountSuccess) {
        logFile << getTimestamp() << "DISM unmount output: " << unmountResult << std::endl;
        overallSuccess = false;
    }

    RemoveDirectoryA(tempDir.c_str());
    return overallSuccess;
}



bool EFIManager::copyBootmgrForNonWindows(const std::string& sourcePath, const std::string& espPath)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    std::ofstream logFile(logDir + "\\iso_extract.log", std::ios::app);

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
    return bootmgrCopied;
}

bool EFIManager::validateAndFixEFIFiles(const std::string& efiDestPath, std::ofstream& logFile)
{
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
                logFile << getTimestamp() << "EFI file is invalid (not PE): " << efiFile << std::endl;
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
                        logFile << getTimestamp() << "Replaced invalid EFI file with system bootmgfw.efi: " << efiFile << " (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
                    } else {
                        logFile << getTimestamp() << "Failed to replace invalid EFI file: " << efiFile << std::endl;
                    }
                } else {
                    logFile << getTimestamp() << "System EFI file not found, cannot replace invalid file: " << efiFile << std::endl;
                }
            } else {
                uint16_t machine = getPEMachine(efiFile);
                logFile << getTimestamp() << "EFI file is valid: " << efiFile << " (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
            }
        }
    }
    return true;
}bool EFIManager::ensureBootFileExists(const std::string& espPath)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    std::ofstream logFile(logDir + "\\iso_extract.log", std::ios::app);

    // Check if BOOTX64.EFI is missing, copy from system to ensure compatibility
    std::string bootFilePath = espPath + "EFI\\BOOT\\BOOTX64.EFI";
    DWORD bootAttrs = GetFileAttributesA(bootFilePath.c_str());
    bool bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));

    if (!bootFileExists) {
        bootFilePath = espPath + "EFI\\boot\\BOOTX64.EFI";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = espPath + "EFI\\boot\\bootx64.efi";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = espPath + "EFI\\BOOT\\BOOTIA32.EFI";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = espPath + "EFI\\boot\\BOOTIA32.EFI";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    if (!bootFileExists) {
        bootFilePath = espPath + "EFI\\boot\\bootia32.efi";
        bootAttrs = GetFileAttributesA(bootFilePath.c_str());
        bootFileExists = (bootAttrs != INVALID_FILE_ATTRIBUTES && !(bootAttrs & FILE_ATTRIBUTE_DIRECTORY));
    }

    logFile << getTimestamp() << "Boot file check: " << bootFilePath << " - " << (bootFileExists ? "EXISTS" : "NOT FOUND") << std::endl;

    if (!bootFileExists) {
        logFile << getTimestamp() << "BOOTX64.EFI not found - EFI boot files should be provided by the ISO extraction process" << std::endl;
        logFile << getTimestamp() << "If this is a Windows ISO, ensure EFI directory was copied correctly" << std::endl;
        // Don't copy system bootmgfw.efi as it may not be compatible with custom BCD configurations
        // Let the BCD configuration handle bootloader selection instead
    }

    return bootFileExists;
}

// Utility functions
std::string EFIManager::exec(const char* cmd) {
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

const char* EFIManager::getTimestamp() {
    static char buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return buffer;
}

bool EFIManager::isValidPE(const std::string& path) {
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

uint16_t EFIManager::getPEMachine(const std::string& path) {
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

bool EFIManager::copyFileUtf8(const std::string& src, const std::string& dst) {
    std::wstring wsrc = Utils::utf8_to_wstring(src);
    std::wstring wdst = Utils::utf8_to_wstring(dst);
    return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE);
}

void EFIManager::ensureTempDirectoryClean(const std::string& tempDir) {
    // First try to unmount any existing WIM mount
    std::string mountDir = tempDir.substr(0, tempDir.size() - 1); // Remove trailing backslash
    std::string unmountCmd = "cmd /c dism /Unmount-Wim /MountDir:\"" + mountDir + "\" /discard 2>nul";
    exec(unmountCmd.c_str());

    // Wait a moment for unmount to complete
    Sleep(1000);

    // Try multiple approaches to remove the directory
    bool removed = false;

    // Approach 1: Use rd command
    std::string rdCmd = "cmd /c rd /s /q \"" + mountDir + "\" 2>nul";
    exec(rdCmd.c_str());
    if (GetFileAttributesA(tempDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        removed = true;
    }

    // Approach 2: If still exists, try rmdir
    if (!removed) {
        std::string rmdirCmd = "cmd /c rmdir /s /q \"" + mountDir + "\" 2>nul";
        exec(rmdirCmd.c_str());
        if (GetFileAttributesA(tempDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
            removed = true;
        }
    }

    // Approach 3: If still exists, try PowerShell
    if (!removed) {
        std::string psCmd = "powershell -Command \"if (Test-Path '" + mountDir + "') { Remove-Item -Path '" + mountDir + "' -Recurse -Force -ErrorAction SilentlyContinue }\"";
        exec(psCmd.c_str());
        if (GetFileAttributesA(tempDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
            removed = true;
        }
    }
}

bool EFIManager::isDirectoryEmpty(const std::string& dirPath) {
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((dirPath + "*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return true; // Directory doesn't exist or can't be accessed
    }

    bool isEmpty = true;
    do {
        std::string name = findData.cFileName;
        if (name != "." && name != "..") {
            isEmpty = false;
            break;
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return isEmpty;
}

bool EFIManager::ensureSecureBootCompatibleBootloader(const std::string& espPath)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    std::ofstream logFile(logDir + "\\iso_extract.log", std::ios::app);

    logFile << getTimestamp() << "Ensuring Secure Boot compatible bootloader..." << std::endl;

    // Use system's Secure Boot signed bootmgfw.efi for EFI booting
    std::string systemBootmgfw = "C:\\Windows\\Boot\\EFI\\bootmgfw.efi";
    std::string destBootmgfw = espPath + "EFI\\Microsoft\\Boot\\bootmgfw.efi";

    DWORD sysAttrs = GetFileAttributesA(systemBootmgfw.c_str());
    logFile << getTimestamp() << "System bootmgfw.efi exists: " << (sysAttrs != INVALID_FILE_ATTRIBUTES ? "YES" : "NO") << std::endl;

    if (sysAttrs != INVALID_FILE_ATTRIBUTES) {
        // Ensure destination directory exists
        std::string destDir = espPath + "EFI\\Microsoft\\Boot";
        CreateDirectoryA(destDir.c_str(), NULL);

        // Check if destination already exists
        DWORD destAttrs = GetFileAttributesA(destBootmgfw.c_str());
        logFile << getTimestamp() << "Destination bootmgfw.efi exists: " << (destAttrs != INVALID_FILE_ATTRIBUTES ? "YES" : "NO") << std::endl;

        // Copy system bootmgfw.efi to ESP
        if (copyFileUtf8(systemBootmgfw, destBootmgfw)) {
            uint16_t machine = getPEMachine(destBootmgfw);
            logFile << getTimestamp() << "Copied Secure Boot compatible bootmgfw.efi to ESP (machine=0x" << std::hex << machine << std::dec << ")" << std::endl;
            return true;
        } else {
            logFile << getTimestamp() << "Failed to copy system bootmgfw.efi to ESP, error: " << GetLastError() << std::endl;
        }
    } else {
        logFile << getTimestamp() << "System bootmgfw.efi not found" << std::endl;
    }
    return false;
}
