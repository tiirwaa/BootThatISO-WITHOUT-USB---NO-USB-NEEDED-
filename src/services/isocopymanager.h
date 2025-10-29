#ifndef ISOCOPYMANAGER_H
#define ISOCOPYMANAGER_H

#include <string>
#include <set>
#include "../models/EventManager.h"

class ISOCopyManager
{
private:
    ISOCopyManager();
    ~ISOCopyManager();
    ISOCopyManager(const ISOCopyManager&) = delete;
    ISOCopyManager& operator=(const ISOCopyManager&) = delete;

public:
    static ISOCopyManager& getInstance();

    bool extractISOContents(EventManager& eventManager, const std::string& isoPath, const std::string& destPath, const std::string& espPath, bool extractContent = true);
    bool copyISOFile(EventManager& eventManager, const std::string& isoPath, const std::string& destPath);

    bool isWindowsISO;

private:
    std::string exec(const char* cmd);
    long long getDirectorySize(const std::string& path);
    bool copyDirectoryWithProgress(const std::string& source, const std::string& dest, EventManager& eventManager, long long totalSize, long long& copiedSoFar, const std::set<std::string>& excludeDirs = {});
};

#endif // ISOCOPYMANAGER_H