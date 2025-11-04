#pragma once

#include <string>
#include "../models/EventManager.h"

class PartitionReformatter {
public:
    explicit PartitionReformatter(EventManager *eventManager);
    ~PartitionReformatter();

    bool reformatPartition(const std::string &format);
    bool reformatEfiPartition();
    bool cleanBootThatISOFiles(); // Clean only BootThatISO files from EFI without formatting

private:
    EventManager *eventManager_;
};