#pragma once

#include <string>
#include <set>
#include "EventManager.h"

class FileCopyManager {
public:
    explicit FileCopyManager(EventManager& eventManager);
    ~FileCopyManager();

    bool copyDirectoryWithProgress(const std::string& source, const std::string& dest, long long totalSize, long long& copiedSoFar, const std::set<std::string>& excludeDirs, const std::string& operation);

private:
    EventManager& eventManager;

    // Utility functions
    const char* getTimestamp();
    bool copyFileUtf8(const std::string& src, const std::string& dst);
    bool isValidPE(const std::string& path);
    uint16_t getPEMachine(const std::string& path);
};