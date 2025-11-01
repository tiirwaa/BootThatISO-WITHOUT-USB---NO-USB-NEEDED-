#ifndef PROCESSSERVICE_H
#define PROCESSSERVICE_H

#include <string>
#include <memory>
#include "../services/partitionmanager.h"
#include "../services/isocopymanager.h"
#include "../services/bcdmanager.h"
#include "../models/BootStrategyFactory.h"
#include "../models/EventManager.h"

class ProcessService {
public:
    ProcessService(PartitionManager *pm, ISOCopyManager *icm, BCDManager *bcm, EventManager &em);

    struct ProcessResult {
        bool        success;
        std::string errorMessage;
    };

    ProcessResult validateAndPrepare(const std::string &isoPath, const std::string &format, bool skipIntegrityCheck);
    ProcessResult copyIsoContent(const std::string &isoPath, const std::string &format, const std::string &modeKey,
                                 const std::string &modeLabel);
    ProcessResult configureBoot(const std::string &modeKey);

private:
    PartitionManager *partitionManager;
    ISOCopyManager   *isoCopyManager;
    BCDManager       *bcdManager;
    EventManager     &eventManager;

    std::string partitionDrive;
    std::string espDrive;

    bool copyISO(const std::string &isoPath, const std::string &destPath, const std::string &espPath,
                 const std::string &modeKey, const std::string &modeLabel, const std::string &format);
};

#endif // PROCESSSERVICE_H