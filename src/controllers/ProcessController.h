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
#include "ProcessService.h"

class ProcessController {
public:
    ProcessController(EventManager &eventManager);
    ~ProcessController();

    void startProcess(const std::string &isoPath, const std::string &selectedFormat,
                      const std::string &selectedBootModeKey, const std::string &selectedBootModeLabel,
                      bool skipIntegrityCheck = false, bool synchronous = false);
    // Request cancellation of the running process and wait for cleanup
    void requestCancel();
    bool recoverSpace();

private:
    void processInThread(const std::string &isoPath, const std::string &selectedFormat,
                         const std::string &selectedBootModeKey, const std::string &selectedBootModeLabel,
                         bool skipIntegrityCheck);
    void recoverSpaceInThread();

    PartitionManager               *partitionManager;
    ISOCopyManager                 *isoCopyManager;
    BCDManager                     *bcdManager;
    EventManager                   &eventManager;
    std::thread                     workerThread;
    std::thread                     recoveryThread;
    std::atomic<bool>               recoveryInProgress{false};
    std::unique_ptr<ProcessService> processService;
};

#endif // PROCESSCONTROLLER_H
