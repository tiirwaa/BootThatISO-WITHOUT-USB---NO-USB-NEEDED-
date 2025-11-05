#pragma once

#include <string>
#include <vector>
#include <utility>
#include "../models/EventManager.h"

class IRecoveryStep {
public:
    virtual ~IRecoveryStep() = default;
    virtual bool execute() = 0;
};

class VolumeEnumerator : public IRecoveryStep {
public:
    VolumeEnumerator(EventManager* eventManager, std::vector<std::pair<std::string, std::string>>& volumesToDelete);
    bool execute() override;

private:
    EventManager* eventManager_;
    std::vector<std::pair<std::string, std::string>>& volumesToDelete_;
};

class VolumeDeleter : public IRecoveryStep {
public:
    VolumeDeleter(EventManager* eventManager, const std::vector<std::pair<std::string, std::string>>& volumesToDelete);
    bool execute() override;

private:
    EventManager* eventManager_;
    const std::vector<std::pair<std::string, std::string>>& volumesToDelete_;
};

class PartitionResizer : public IRecoveryStep {
public:
    explicit PartitionResizer(EventManager* eventManager);
    bool execute() override;

private:
    EventManager* eventManager_;
};

class BCDCleaner : public IRecoveryStep {
public:
    explicit BCDCleaner(EventManager* eventManager);
    bool execute() override;

private:
    EventManager* eventManager_;
};