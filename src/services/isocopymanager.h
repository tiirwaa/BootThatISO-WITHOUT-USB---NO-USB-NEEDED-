#ifndef ISOCOPYMANAGER_H
#define ISOCOPYMANAGER_H

#include <string>
#include <set>
#include <fstream>
#include <memory>
#include "../models/EventManager.h"

class ISOTypeDetector;
class EFIManager;
class ISOMounter;
class FileCopyManager;

class ISOCopyManager
{
private:
    ISOCopyManager();
    ~ISOCopyManager();
    ISOCopyManager(const ISOCopyManager&) = delete;
    ISOCopyManager& operator=(const ISOCopyManager&) = delete;

public:
    static ISOCopyManager& getInstance();

    bool extractISOContents(EventManager& eventManager, const std::string& isoPath, const std::string& destPath, const std::string& espPath, bool extractContent = true, bool extractBootWim = false, bool copyInstallWim = false);
    bool copyISOFile(EventManager& eventManager, const std::string& isoPath, const std::string& destPath);

    bool getIsWindowsISO() const;
    const char* getTimestamp();

private:
    std::unique_ptr<ISOTypeDetector> typeDetector;
    std::unique_ptr<EFIManager> efiManager;
    std::unique_ptr<ISOMounter> isoMounter;
    std::unique_ptr<FileCopyManager> fileCopyManager;
    bool isWindowsISODetected;

    std::string exec(const char* cmd);
    long long getDirectorySize(const std::string& path);
    void listDirectoryRecursive(std::ofstream& log, const std::string& path, int depth, int maxDepth);
};

#endif // ISOCOPYMANAGER_H
