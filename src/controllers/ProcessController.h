#ifndef PROCESSCONTROLLER_H
#define PROCESSCONTROLLER_H

#include <string>
#include <thread>
#include <atomic>
#include "../services/partitionmanager.h"
#include "../services/isocopymanager.h"
#include "../services/bcdmanager.h"
#include "../models/EventManager.h"
#include "../models/BootStrategyFactory.h"

class ProcessController {
public:
    ProcessController(EventManager& eventManager);
    ~ProcessController();

    void startProcess(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootModeKey, const std::string& selectedBootModeLabel, bool skipIntegrityCheck = false, bool synchronous = false);
    // Request cancellation of the running process and wait for cleanup
    void requestCancel();
    bool recoverSpace();

private:
    void processInThread(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootModeKey, const std::string& selectedBootModeLabel, bool skipIntegrityCheck);
    void recoverSpaceInThread();
    bool copyISO(const std::string& isoPath, const std::string& destPath, const std::string& espPath, const std::string& modeKey, const std::string& modeLabel, const std::string& format);
    void configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, const std::string& modeKey);

    PartitionManager* partitionManager;
    ISOCopyManager* isoCopyManager;
    BCDManager* bcdManager;
    EventManager& eventManager;
    std::thread workerThread;
    std::thread recoveryThread;
    std::atomic<bool> recoveryInProgress{false};
};

#endif // PROCESSCONTROLLER_H
