#include "ProcessController.h"
#include <memory>
#include <windows.h>
#include <fstream>
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"
#include "models/HashInfo.h"
#include "../utils/AppKeys.h"
#include "../utils/Logger.h"

ProcessController::ProcessController(EventManager &eventManager) : eventManager(eventManager) {
    partitionManager = &PartitionManager::getInstance();
    partitionManager->setEventManager(&eventManager);
    isoCopyManager = &ISOCopyManager::getInstance();
    bcdManager     = &BCDManager::getInstance();
    bcdManager->setEventManager(&eventManager);
    processService = std::make_unique<ProcessService>(partitionManager, isoCopyManager, bcdManager, eventManager);
}

ProcessController::~ProcessController() {
    if (workerThread.joinable()) {
        workerThread.join();
    }
    if (recoveryThread.joinable()) {
        recoveryThread.join();
    }
}

void ProcessController::requestCancel() {
    eventManager.requestCancel();
    if (recoveryInProgress.load()) {
        eventManager.notifyLogUpdate("La recuperacion de espacio esta en curso y no admite cancelacion.\r\n");
    }
}

void ProcessController::startProcess(const std::string &isoPath, const std::string &selectedFormat,
                                     const std::string &selectedBootModeKey, const std::string &selectedBootModeLabel,
                                     bool skipIntegrityCheck, bool synchronous) {
    Logger::instance().resetProcessLogs();
    const std::string logDir = Logger::instance().logDirectory();

    // Log to file
    std::ofstream logFile((logDir + "\\start_process.log").c_str());
    logFile << "startProcess called with synchronous: " << synchronous << "\n";
    logFile.close();

    eventManager.notifyProgressUpdate(0);

    // Trim the isoPath to remove leading/trailing whitespace, quotes and common invisible characters
    std::string trimmedIsoPath = isoPath;
    auto        isTrimChar     = [](char c) -> bool {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::isspace(uc) || uc <= 32 || uc == 0xA0 || c == '"' || c == '\'' || c == '\0';
    };
    while (!trimmedIsoPath.empty() && isTrimChar(trimmedIsoPath.front()))
        trimmedIsoPath.erase(trimmedIsoPath.begin());
    while (!trimmedIsoPath.empty() && isTrimChar(trimmedIsoPath.back()))
        trimmedIsoPath.pop_back();

    eventManager.notifyLogUpdate("ISO path after trimming: '" + trimmedIsoPath + "'\r\n");

    if (trimmedIsoPath.empty()) {
        eventManager.notifyLogUpdate("Error: No se ha seleccionado un archivo ISO.\r\n");
        return;
    }

    eventManager.notifyLogUpdate("Iniciando proceso con ISO: " + trimmedIsoPath + ", formato: " + selectedFormat +
                                 ", modo: " + selectedBootModeLabel + "\r\n");

    bool partitionExists = partitionManager->partitionExists();
    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            eventManager.notifyLogUpdate("Error: " + validation.errorMessage + "\r\n");
            return;
        }

        // Aqui se pueden agregar confirmaciones si es necesario, pero por simplicidad, asumir que se confirma.
    }

    if (synchronous) {
        processInThread(trimmedIsoPath, selectedFormat, selectedBootModeKey, selectedBootModeLabel, skipIntegrityCheck);
    } else {
        if (workerThread.joinable()) {
            workerThread.join();
        }
        workerThread = std::thread(&ProcessController::processInThread, this, trimmedIsoPath, selectedFormat,
                                   selectedBootModeKey, selectedBootModeLabel, skipIntegrityCheck);
    }
}

void ProcessController::processInThread(const std::string &isoPath, const std::string &selectedFormat,
                                        const std::string &selectedBootModeKey,
                                        const std::string &selectedBootModeLabel, bool skipIntegrityCheck) {
    eventManager.notifyLogUpdate("Verificando estado de particiones...\r\n");
    eventManager.notifyProgressUpdate(10);

    auto prepareResult = processService->validateAndPrepare(isoPath, selectedFormat, skipIntegrityCheck);
    if (!prepareResult.success) {
        eventManager.notifyLogUpdate("Error: " + prepareResult.errorMessage + "\r\n");
        eventManager.notifyError("Error: " + prepareResult.errorMessage);
        eventManager.notifyButtonEnable();
        return;
    }
    eventManager.notifyProgressUpdate(30);

    eventManager.notifyLogUpdate("Iniciando preparación de archivos del ISO...\r\n");
    auto copyResult =
        processService->copyIsoContent(isoPath, selectedFormat, selectedBootModeKey, selectedBootModeLabel);
    if (!copyResult.success) {
        eventManager.notifyLogUpdate("Error: " + copyResult.errorMessage + "\r\n");
        eventManager.notifyError("Error: " + copyResult.errorMessage);
        eventManager.notifyButtonEnable();
        return;
    }
    eventManager.notifyProgressUpdate(70);

    eventManager.notifyLogUpdate("Configurando arranque...\r\n");
    auto configResult = processService->configureBoot(selectedBootModeKey);
    if (!configResult.success) {
        eventManager.notifyLogUpdate("Error: " + configResult.errorMessage + "\r\n");
        eventManager.notifyError("Error: " + configResult.errorMessage);
        eventManager.notifyButtonEnable();
        return;
    }

    eventManager.notifyLogUpdate("Proceso completado exitosamente.\r\n");
    eventManager.notifyProgressUpdate(100);
    eventManager.notifyAskRestart();
    eventManager.notifyButtonEnable();
}

bool ProcessController::recoverSpace() {
    if (recoveryInProgress.load()) {
        eventManager.notifyLogUpdate("La recuperacion de espacio ya se esta ejecutando.\r\n");
        return false;
    }

    if (recoveryThread.joinable()) {
        recoveryThread.join();
    }

    eventManager.notifyLogUpdate("Iniciando recuperacion de espacio en segundo plano.\r\n");
    recoveryInProgress.store(true);
    recoveryThread = std::thread(&ProcessController::recoverSpaceInThread, this);
    return true;
}

void ProcessController::recoverSpaceInThread() {
    eventManager.notifyLogUpdate("Recuperando espacio. Esto puede tardar varios minutos...\r\n");
    bool success = partitionManager->recoverSpace();
    if (success) {
        // Remove BCD entries
        bcdManager->restoreBCD();
        eventManager.notifyLogUpdate("Entradas BCD ISOBOOT eliminadas.\r\n");
    }
    recoveryInProgress.store(false);
    eventManager.notifyRecoverComplete(success);
}
