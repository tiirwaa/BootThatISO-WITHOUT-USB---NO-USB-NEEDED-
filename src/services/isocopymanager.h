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
class IniConfigurator;
class BootWimProcessor;
class ContentExtractor;
class HashVerifier;

class ISOCopyManager {
private:
    ISOCopyManager();
    ~ISOCopyManager();
    ISOCopyManager(const ISOCopyManager &)            = delete;
    ISOCopyManager &operator=(const ISOCopyManager &) = delete;

public:
    static ISOCopyManager &getInstance();

    bool extractISOContents(EventManager &eventManager, const std::string &isoPath, const std::string &destPath,
                            const std::string &espPath, bool extractContent = true, bool extractBootWim = false,
                            bool copyInstallWim = false, const std::string &mode = "", const std::string &format = "");
    bool copyISOFile(EventManager &eventManager, const std::string &isoPath, const std::string &destPath);

    bool               getIsWindowsISO() const;
    static const char *getTimestamp();

private:
    std::unique_ptr<ISOTypeDetector>  typeDetector;
    std::unique_ptr<EFIManager>       efiManager;
    std::unique_ptr<ISOMounter>       isoMounter;
    std::unique_ptr<FileCopyManager>  fileCopyManager;
    std::unique_ptr<IniConfigurator>  iniConfigurator;
    std::unique_ptr<BootWimProcessor> bootWimProcessor;
    std::unique_ptr<ContentExtractor> contentExtractor;
    std::unique_ptr<HashVerifier>     hashVerifier;
    bool                              isWindowsISODetected;

    std::string exec(const char *cmd, EventManager *eventManager = nullptr);
    long long   getDirectorySize(const std::string &path);
    void        listDirectoryRecursive(std::ofstream &log, const std::string &path, int depth, int maxDepth,
                                       EventManager &eventManager, long long &fileCount);
};

#endif // ISOCOPYMANAGER_H
