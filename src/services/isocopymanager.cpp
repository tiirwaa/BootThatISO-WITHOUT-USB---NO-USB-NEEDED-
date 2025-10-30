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
#include <memory>
#include "../utils/Utils.h"
#include "isotypedetector.h"
#include "../models/efimanager.h"
#include "../models/isomounter.h"
#include "../models/filecopymanager.h"

// Forward declarations for helper functions defined later in this file
static bool isValidPE(const std::string& path);
static uint16_t getPEMachine(const std::string& path);
static BOOL copyFileUtf8(const std::string& src, const std::string& dst);

ISOCopyManager& ISOCopyManager::getInstance() {
    static ISOCopyManager instance;
    return instance;
}

ISOCopyManager::ISOCopyManager()
    : typeDetector(std::make_unique<ISOTypeDetector>()),
      efiManager(nullptr),
      isoMounter(std::make_unique<ISOMounter>()),
      fileCopyManager(nullptr),
      isWindowsISODetected(false)
{
}

ISOCopyManager::~ISOCopyManager()
{
}

bool ISOCopyManager::getIsWindowsISO() const {
    return isWindowsISODetected;
}



const char* ISOCopyManager::getTimestamp() {
    static char buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return buffer;
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

bool ISOCopyManager::extractISOContents(EventManager& eventManager, const std::string& isoPath, const std::string& destPath, const std::string& espPath, bool extractContent)
{
    // Initialize managers with EventManager
    fileCopyManager = std::make_unique<FileCopyManager>(eventManager);
    efiManager = std::make_unique<EFIManager>(eventManager, *fileCopyManager);

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
    
    // Mount the ISO
    std::string driveLetterStr;
    if (!isoMounter->mountISO(isoPath, driveLetterStr)) {
        logFile << getTimestamp() << "Failed to mount ISO" << std::endl;
        logFile.close();
        eventManager.notifyLogUpdate("Error: Falló el montaje del ISO.\r\n");
        return false;
    }
    
    eventManager.notifyLogUpdate("ISO montado exitosamente en " + driveLetterStr + ":.\r\n");
    
    std::string sourcePath = driveLetterStr + ":\\";
    logFile << getTimestamp() << "Source path: " << sourcePath << std::endl;
    
    // Add delay to allow the drive to be ready
    logFile << getTimestamp() << "Waiting 10 seconds for drive to be ready" << std::endl;
    Sleep(10000);
    logFile << getTimestamp() << "Drive ready, proceeding with analysis" << std::endl;

    if (eventManager.isCancelRequested()) {
        logFile << getTimestamp() << "Operation cancelled after mount preparation\n";
        isoMounter->unmountISO(isoPath);
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
    bool isWindowsISO = typeDetector->isWindowsISO(sourcePath);
    logFile << getTimestamp() << "Is Windows ISO: " << (isWindowsISO ? "Yes" : "No") << std::endl;
    
    if (isWindowsISO) {
        eventManager.notifyLogUpdate("ISO de Windows detectado.\r\n");
    } else {
        eventManager.notifyLogUpdate("ISO no-Windows detectado.\r\n");
    }
    
    isWindowsISODetected = isWindowsISO;
    
    if (extractContent) {
        // Extract all ISO contents to data partition, excluding EFI
        std::set<std::string> excludeDirs = {"efi", "EFI"};
        if (!fileCopyManager->copyDirectoryWithProgress(sourcePath, destPath, isoSize, copiedSoFar, excludeDirs, "Copiando contenido del ISO")) {
            logFile << getTimestamp() << "Failed to copy content or cancelled" << std::endl;
            // Ensure ISO is dismounted
            isoMounter->unmountISO(isoPath);
            logFile.close();
            return false;
        }
    } else {
        logFile << getTimestamp() << "Skipping content extraction (Boot desde Memoria mode)" << std::endl;
    }

    // Extract EFI
    bool efiSuccess = efiManager->extractEFI(sourcePath, espPath, isWindowsISO, copiedSoFar, isoSize);
    
    // Dismount the ISO
    eventManager.notifyDetailedProgress(isoSize * 9 / 10, isoSize, "Desmontando ISO");
    if (!isoMounter->unmountISO(isoPath)) {
        logFile << getTimestamp() << "Warning: Failed to unmount ISO" << std::endl;
    }
    logFile << getTimestamp() << "Dismount ISO completed" << std::endl;
    
    logFile << getTimestamp() << "EFI extraction " << (efiSuccess ? "SUCCESS" : "FAILED") << std::endl;
    if (extractContent) {
        logFile << getTimestamp() << "Content extraction completed." << std::endl;
    }
    logFile.close();
    
    eventManager.notifyDetailedProgress(0, 0, "");
    return efiSuccess;
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