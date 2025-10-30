#include "filecopymanager.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include "../utils/Utils.h"

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

FileCopyManager::FileCopyManager(EventManager& eventManager)
    : eventManager(eventManager)
{
}

FileCopyManager::~FileCopyManager()
{
}

bool FileCopyManager::copyDirectoryWithProgress(const std::string& source, const std::string& dest, long long totalSize, long long& copiedSoFar, const std::set<std::string>& excludeDirs, const std::string& operation)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    // Skip creating directory if it's a drive root (e.g., "Z:\")
    bool isDriveRoot = (dest.length() == 3 && dest[1] == ':' && dest[2] == '\\');
    if (!isDriveRoot) {
        BOOL result = CreateDirectoryA(dest.c_str(), NULL);
        if (!result) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                std::ofstream errorLog(logDir + "\\copy_error.log", std::ios::app);
                errorLog << getTimestamp() << "Failed to create directory: " << dest << " Error code: " << error << "\n";
                errorLog.close();
                eventManager.notifyLogUpdate("Error: Failed to create directory " + dest + " (Error " + std::to_string(error) + ")\r\n");
                return false;
            } else {
                std::ofstream errorLog(logDir + "\\copy_error.log", std::ios::app);
                errorLog << getTimestamp() << "Directory already exists: " << dest << "\n";
                errorLog.close();
            }
        } else {
            std::ofstream errorLog(logDir + "\\copy_error.log", std::ios::app);
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
                if (!copyDirectoryWithProgress(srcItem, destItem, totalSize, copiedSoFar, excludeDirs, operation)) {
                    if (eventManager.isCancelRequested()) {
                        FindClose(hFind);
                        return false;
                    }
                    std::ofstream errorLog(logDir + "\\copy_error.log", std::ios::app);
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

                CopyProgressContext ctx = {&eventManager, totalSize, copiedSoFar, operation};
                BOOL copyResult = CopyFileExW(Utils::utf8_to_wstring(srcItem).c_str(), Utils::utf8_to_wstring(destItem).c_str(), CopyFileProgressRoutine, &ctx, NULL, 0);
                if (!copyResult) {
                    DWORD error = GetLastError();
                    std::ofstream errorLog2(logDir + "\\copy_error.log", std::ios::app);
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
                        std::ofstream errorLog2(logDir + "\\copy_error.log", std::ios::app);
                        errorLog2 << getTimestamp() << "Copied file appears invalid (not PE): " << destItem << "\n";
                        errorLog2.close();
                        eventManager.notifyLogUpdate("Error: Copied file appears invalid (not PE): " + destItem + "\r\n");
                        FindClose(hFind);
                        return false;
                    }
                }

                copiedSoFar += fileSize;
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return true;
}

const char* FileCopyManager::getTimestamp() {
    static char buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return buffer;
}

bool FileCopyManager::copyFileUtf8(const std::string& src, const std::string& dst) {
    std::wstring wsrc = Utils::utf8_to_wstring(src);
    std::wstring wdst = Utils::utf8_to_wstring(dst);
    return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE);
}

bool FileCopyManager::isValidPE(const std::string& path) {
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

uint16_t FileCopyManager::getPEMachine(const std::string& path) {
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