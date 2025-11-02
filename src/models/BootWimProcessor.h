#pragma once
#include <string>
#include <memory>
#include "../models/EventManager.h"
#include "../models/FileCopyManager.h"

class ISOReader;

class BootWimProcessor {
public:
    BootWimProcessor(EventManager &eventManager, FileCopyManager &fileCopyManager);
    ~BootWimProcessor();

    bool processBootWim(const std::string &sourcePath, const std::string &destPath, const std::string &espPath,
                        bool integratePrograms, const std::string &programsSrc, long long &copiedSoFar,
                        bool extractBootWim, bool copyInstallWim, std::ofstream &logFile);

private:
    EventManager    &eventManager_;
    FileCopyManager &fileCopyManager_;
    std::unique_ptr<ISOReader> isoReader_;

    bool mountAndProcessWim(const std::string &bootWimDest, const std::string &destPath, const std::string &sourcePath,
                            bool integratePrograms, const std::string &programsSrc, long long &copiedSoFar,
                            std::ofstream &logFile);
    bool extractAdditionalBootFiles(const std::string &sourcePath, const std::string &espPath,
                                    const std::string &destPath, long long &copiedSoFar, long long isoSize,
                                    std::ofstream &logFile);
    bool integrateSystemDriversIntoMountedImage(const std::string &mountDir, std::ofstream &logFile);
};
