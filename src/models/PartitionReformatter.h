#pragma once

#include <string>
#include "../models/EventManager.h"

class PartitionReformatter {
public:
    explicit PartitionReformatter(EventManager *eventManager);
    ~PartitionReformatter();

    bool reformatPartition(const std::string &format);
    bool reformatEfiPartition();

private:
    EventManager *eventManager_;
};