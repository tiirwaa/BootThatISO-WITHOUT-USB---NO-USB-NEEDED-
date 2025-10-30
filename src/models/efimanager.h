#pragma once

#include <string>
#include <memory>
#include "EventManager.h"

class FileCopyManager;

class EFIManager {
public:
    explicit EFIManager(EventManager& eventManager, FileCopyManager& fileCopyManager);
    ~EFIManager();

    bool extractEFI(const std::string& sourcePath, const std::string& espPath, bool isWindowsISO, long long& copiedSoFar, long long isoSize);

private:
    EventManager& eventManager;
    FileCopyManager& fileCopyManager;

    // Helper methods
    bool extractEFIDirectory(const std::string& sourcePath, const std::string& espPath, long long& copiedSoFar, long long isoSize);
    bool extractBootFilesFromWIM(const std::string& sourcePath, const std::string& espPath, long long& copiedSoFar, long long isoSize);
    bool copyBootmgrForNonWindows(const std::string& sourcePath, const std::string& espPath);
    bool validateAndFixEFIFiles(const std::string& efiDestPath, std::ofstream& logFile);
    bool ensureBootFileExists(const std::string& espPath);
    bool ensureSecureBootCompatibleBootloader(const std::string& espPath);

    // Utility functions
    std::string exec(const char* cmd);
    const char* getTimestamp();
    bool isValidPE(const std::string& path);
    uint16_t getPEMachine(const std::string& path);
    bool copyFileUtf8(const std::string& src, const std::string& dst);
    void ensureTempDirectoryClean(const std::string& tempDir);
    bool isDirectoryEmpty(const std::string& dirPath);
};