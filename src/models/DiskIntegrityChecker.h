#pragma once

#include <string>
#include "../models/EventManager.h"

class DiskIntegrityChecker {
public:
    explicit DiskIntegrityChecker(EventManager* eventManager);
    ~DiskIntegrityChecker();

    bool performDiskIntegrityCheck();

private:
    EventManager* eventManager_;

    bool RestartComputer();
    void logToGeneral(const std::string& message);
};