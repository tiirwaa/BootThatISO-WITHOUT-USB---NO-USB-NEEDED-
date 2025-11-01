#pragma once
#include <string>
#include <memory>
#include "../models/EventManager.h"
#include "../models/FileCopyManager.h"

class ContentExtractor {
public:
    ContentExtractor(EventManager& eventManager, FileCopyManager& fileCopyManager);
    ~ContentExtractor();

    bool extractContent(const std::string& sourcePath, const std::string& destPath, long long isoSize, long long& copiedSoFar,
                        bool extractContent, bool isWindowsISO, const std::string& mode, std::ofstream& logFile);

private:
    EventManager& eventManager_;
    FileCopyManager& fileCopyManager_;
};