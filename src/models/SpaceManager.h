#pragma once

#include <string>
#include "../models/EventManager.h"
#include "../models/SpaceValidationResult.h"

class SpaceManager {
public:
    explicit SpaceManager(EventManager *eventManager);
    ~SpaceManager();

    SpaceValidationResult validateAvailableSpace();
    long long             getAvailableSpaceGB(const std::string &driveRoot = "");
    bool                  performSpaceRecovery();
    bool                  recoverSpace();

private:
    EventManager *eventManager_;
    std::string   monitoredDrive_;
};