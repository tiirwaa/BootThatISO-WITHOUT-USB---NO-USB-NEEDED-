#pragma once

#include <string>
#include "../models/EventManager.h"

class DiskpartExecutor {
public:
    explicit DiskpartExecutor(EventManager *eventManager);
    ~DiskpartExecutor();

    bool performDiskpartOperations(const std::string &format);
    bool verifyPartitionsCreated();
    bool isDiskGpt();

private:
    EventManager *eventManager_;
};