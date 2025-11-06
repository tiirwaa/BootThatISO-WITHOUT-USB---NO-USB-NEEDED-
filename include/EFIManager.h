#pragma once

#include <string>
#include <vector>
#include <optional>
#include <windows.h>

class EventManager;

class EFIManager {
public:
    EFIManager();

    // Select the best EFI boot file based on strategy type
    std::optional<std::string> selectEFIBootFile(const std::string &espDriveLetter, const std::string &strategyType,
                                                 EventManager *eventManager = nullptr);

    // Get machine type from PE file (made public for BCDConfigurator)
    WORD getMachineType(const std::string &filePath);

    // Build candidate list based on strategy
    std::vector<std::string> getCandidates(const std::string &espDriveLetter, const std::string &strategyType);

    // Evaluate candidates and select best
    std::optional<std::string> evaluateCandidates(const std::vector<std::string> &candidates,
                                                  EventManager                   *eventManager);
};