#include "ProcessController.h"
#include <memory>

ProcessController::ProcessController(EventManager& eventManager)
    : eventManager(eventManager), workerThread(nullptr)
{
    partitionManager = &PartitionManager::getInstance();
    isoCopyManager = &ISOCopyManager::getInstance();
    bcdManager = &BCDManager::getInstance();
}

ProcessController::~ProcessController()
{
    if (workerThread && workerThread->joinable()) {
        workerThread->join();
        delete workerThread;
    }
}

void ProcessController::startProcess(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootMode)
{
    eventManager.notifyProgressUpdate(0);

    if (isoPath.empty()) {
        eventManager.notifyLogUpdate("Por favor, seleccione un archivo ISO primero.\r\n");
        return;
    }

    bool partitionExists = partitionManager->partitionExists();
    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            eventManager.notifyLogUpdate("No hay suficiente espacio disponible. Se requieren al menos 10 GB.\r\n");
            return;
        }

        // Aquí se pueden agregar confirmaciones si es necesario, pero por simplicidad, asumir que se confirma.
    }

    // Iniciar hilo
    workerThread = new std::thread(&ProcessController::processInThread, this, isoPath, selectedFormat, selectedBootMode);
}

void ProcessController::processInThread(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootMode)
{
    bool partitionExists = partitionManager->partitionExists();
    if (partitionExists) {
        eventManager.notifyLogUpdate("Las particiones existen. Reformateando...\r\n");
        eventManager.notifyProgressUpdate(20);
        if (!partitionManager->reformatPartition(selectedFormat)) {
            eventManager.notifyLogUpdate("Error al reformatear la partición.\r\n");
            eventManager.notifyError("Error al reformatear la partición.");
            eventManager.notifyButtonEnable();
            return;
        }
        eventManager.notifyLogUpdate("Partición reformateada.\r\n");
        eventManager.notifyProgressUpdate(30);
    } else {
        eventManager.notifyLogUpdate("Espacio validado.\r\n");
        eventManager.notifyProgressUpdate(10);

        eventManager.notifyLogUpdate("Confirmaciones completadas. Creando partición...\r\n");
        eventManager.notifyProgressUpdate(20);

        if (!partitionManager->createPartition(selectedFormat)) {
            eventManager.notifyLogUpdate("Error al crear la partición.\r\n");
            eventManager.notifyError("Error al crear la partición.");
            eventManager.notifyButtonEnable();
            return;
        }
        eventManager.notifyLogUpdate("Partición creada.\r\n");
        eventManager.notifyProgressUpdate(30);
    }

    // Verificar particiones
    std::string partitionDrive = partitionManager->getPartitionDriveLetter();
    if (partitionDrive.empty()) {
        eventManager.notifyLogUpdate("Error: No se puede acceder a la partición ISOBOOT.\r\n");
        eventManager.notifyError("Error: No se puede acceder a la partición ISOBOOT.");
        eventManager.notifyButtonEnable();
        return;
    }
    eventManager.notifyLogUpdate("Partición ISOBOOT encontrada en: " + partitionDrive + "\r\n");

    std::string espDrive = partitionManager->getEfiPartitionDriveLetter();
    if (espDrive.empty()) {
        eventManager.notifyLogUpdate("Error: No se puede acceder a la partición ISOEFI.\r\n");
        eventManager.notifyError("Error: No se puede acceder a la partición ISOEFI.");
        eventManager.notifyButtonEnable();
        return;
    }
    eventManager.notifyLogUpdate("Partición ISOEFI encontrada en: " + espDrive + "\r\n");

    // Copiar ISO
    if (copyISO(isoPath, partitionDrive, espDrive, selectedBootMode)) {
        configureBCD(partitionDrive, espDrive, selectedBootMode);
        eventManager.notifyLogUpdate("Proceso completado.\r\n");
        eventManager.notifyProgressUpdate(100);
        eventManager.notifyAskRestart();
    } else {
        eventManager.notifyLogUpdate("Proceso fallido debido a errores en la copia del ISO.\r\n");
        eventManager.notifyError("Proceso fallido debido a errores en la copia del ISO.");
    }
    eventManager.notifyButtonEnable();
}

bool ProcessController::copyISO(const std::string& isoPath, const std::string& destPath, const std::string& espPath, const std::string& mode)
{
    eventManager.notifyLogUpdate("Extrayendo archivos EFI del ISO...\r\n");
    eventManager.notifyProgressUpdate(40);

    std::string drive = destPath;
    std::string espDrive = espPath;

    if (mode == "RAMDISK") {
        eventManager.notifyLogUpdate("Modo RAMDISK: copiando solo EFI, sin extraer contenido...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, false)) {
            eventManager.notifyLogUpdate("EFI copiado exitosamente.\r\n");
            eventManager.notifyProgressUpdate(55);
        } else {
            eventManager.notifyLogUpdate("Error al copiar EFI.\r\n");
            return false;
        }
        eventManager.notifyLogUpdate("Copiando archivo ISO completo...\r\n");
        if (isoCopyManager->copyISOFile(eventManager, isoPath, drive)) {
            eventManager.notifyLogUpdate("Archivo ISO copiado exitosamente.\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error al copiar el archivo ISO.\r\n");
            return false;
        }
    } else {
        eventManager.notifyLogUpdate("Modo Extraido: extrayendo contenido y EFI...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, true)) {
            eventManager.notifyLogUpdate("Archivos extraídos exitosamente.\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error al extraer archivos del ISO.\r\n");
            return false;
        }
    }
    return true;
}

void ProcessController::configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, const std::string& mode)
{
    eventManager.notifyLogUpdate("Configurando BCD...\r\n");
    eventManager.notifyProgressUpdate(80);

    auto strategy = BootStrategyFactory::createStrategy(mode);
    if (!strategy) {
        eventManager.notifyLogUpdate("Modo de boot no válido.\r\n");
        return;
    }

    std::string error = bcdManager->configureBCD(driveLetter.substr(0, 2), espDriveLetter.substr(0, 2), *strategy);
    if (!error.empty()) {
        eventManager.notifyLogUpdate("Error al configurar BCD: " + error + "\r\n");
        eventManager.notifyError("Error al configurar BCD: " + error);
    } else {
        eventManager.notifyLogUpdate("BCD configurado exitosamente.\r\n");
        eventManager.notifyProgressUpdate(100);
    }
}