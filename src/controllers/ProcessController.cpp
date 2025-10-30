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

void ProcessController::requestCancel()
{
    eventManager.requestCancel();
    // If a worker thread exists, wait for it to finish cleaning up
    if (workerThread && workerThread->joinable()) {
        workerThread->join();
        delete workerThread;
        workerThread = nullptr;
    }
}

void ProcessController::startProcess(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootMode)
{
    eventManager.notifyProgressUpdate(0);

    if (isoPath.empty()) {
        eventManager.notifyLogUpdate("Error: No se ha seleccionado un archivo ISO.\r\n");
        return;
    }

    eventManager.notifyLogUpdate("Iniciando proceso con ISO: " + isoPath + ", formato: " + selectedFormat + ", modo: " + selectedBootMode + "\r\n");

    bool partitionExists = partitionManager->partitionExists();
    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            eventManager.notifyLogUpdate("Error: " + validation.errorMessage + "\r\n");
            return;
        }

        // Aquí se pueden agregar confirmaciones si es necesario, pero por simplicidad, asumir que se confirma.
    }

    // Iniciar hilo
    workerThread = new std::thread(&ProcessController::processInThread, this, isoPath, selectedFormat, selectedBootMode);
}

void ProcessController::processInThread(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootMode)
{
    eventManager.notifyLogUpdate("Verificando estado de particiones...\r\n");
    bool partitionExists = partitionManager->partitionExists();
    if (partitionExists) {
        eventManager.notifyLogUpdate("Particiones existentes detectadas. Iniciando reformateo...\r\n");
        eventManager.notifyProgressUpdate(20);
        if (!partitionManager->reformatPartition(selectedFormat)) {
            eventManager.notifyLogUpdate("Error: Falló el reformateo de la partición.\r\n");
            eventManager.notifyError("Error al reformatear la partición.");
            eventManager.notifyButtonEnable();
            return;
        }
        eventManager.notifyLogUpdate("Partición reformateada exitosamente.\r\n");
        eventManager.notifyProgressUpdate(30);
    } else {
        eventManager.notifyLogUpdate("Validando espacio disponible en disco...\r\n");
        eventManager.notifyProgressUpdate(10);

        eventManager.notifyLogUpdate("Creando nuevas particiones...\r\n");
        eventManager.notifyProgressUpdate(20);

        if (!partitionManager->createPartition(selectedFormat)) {
            eventManager.notifyLogUpdate("Error: Falló la creación de la partición.\r\n");
            eventManager.notifyError("Error al crear la partición.");
            eventManager.notifyButtonEnable();
            return;
        }
        eventManager.notifyLogUpdate("Partición creada exitosamente.\r\n");
        eventManager.notifyProgressUpdate(30);
    }

    eventManager.notifyLogUpdate("Verificando acceso a particiones...\r\n");
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

    eventManager.notifyLogUpdate("Iniciando copia de contenido del ISO...\r\n");
    // Copiar ISO
    if (copyISO(isoPath, partitionDrive, espDrive, selectedBootMode)) {
        eventManager.notifyLogUpdate("Contenido del ISO copiado. Configurando BCD...\r\n");
        // Only configure BCD for Windows ISOs; non-Windows EFI boot directly from ESP
        if (ISOCopyManager::getInstance().isWindowsISO) {
            configureBCD(partitionDrive, espDrive, selectedBootMode);
        } else {
            eventManager.notifyLogUpdate("ISO no-Windows detectado: omitiendo configuración BCD, usando arranque EFI directo desde ESP.\r\n");
            eventManager.notifyProgressUpdate(100);
        }
        eventManager.notifyLogUpdate("Proceso completado exitosamente.\r\n");
        eventManager.notifyProgressUpdate(100);
        eventManager.notifyAskRestart();
    } else {
        eventManager.notifyLogUpdate("Error: Falló la copia del contenido del ISO.\r\n");
        eventManager.notifyError("Proceso fallido debido a errores en la copia del ISO.");
    }
    eventManager.notifyButtonEnable();
}

bool ProcessController::copyISO(const std::string& isoPath, const std::string& destPath, const std::string& espPath, const std::string& mode)
{
    eventManager.notifyLogUpdate("Montando imagen ISO...\r\n");
    eventManager.notifyProgressUpdate(40);

    std::string drive = destPath;
    std::string espDrive = espPath;

    if (mode == "Boot desde Memoria") {
        eventManager.notifyLogUpdate("Modo Boot desde Memoria seleccionado: extrayendo solo archivos EFI...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, false)) {
            eventManager.notifyLogUpdate("Archivos EFI extraídos exitosamente.\r\n");
            eventManager.notifyProgressUpdate(55);
        } else {
            eventManager.notifyLogUpdate("Error: Falló la extracción de archivos EFI.\r\n");
            return false;
        }
        eventManager.notifyLogUpdate("Copiando archivo ISO completo para modo Boot desde Memoria...\r\n");
        if (isoCopyManager->copyISOFile(eventManager, isoPath, drive)) {
            eventManager.notifyLogUpdate("Archivo ISO copiado exitosamente.\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error: Falló la copia del archivo ISO.\r\n");
            return false;
        }
    } else {
        eventManager.notifyLogUpdate("Modo " + mode + " seleccionado: extrayendo contenido completo del ISO...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, true)) {
            eventManager.notifyLogUpdate("Contenido del ISO extraído exitosamente.\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error: Falló la extracción del contenido del ISO.\r\n");
            return false;
        }
    }
    return true;
}

void ProcessController::configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, const std::string& mode)
{
    eventManager.notifyLogUpdate("Configurando Boot Configuration Data (BCD)...\r\n");
    eventManager.notifyProgressUpdate(80);

    auto strategy = BootStrategyFactory::createStrategy(mode);
    if (!strategy) {
        eventManager.notifyLogUpdate("Error: Modo de boot '" + mode + "' no válido.\r\n");
        return;
    }

    eventManager.notifyLogUpdate("Aplicando estrategia de configuración BCD para modo " + mode + "...\r\n");
    std::string error = bcdManager->configureBCD(driveLetter.substr(0, 2), espDriveLetter.substr(0, 2), *strategy);
    if (!error.empty()) {
        eventManager.notifyLogUpdate("Error en configuración BCD: " + error + "\r\n");
        eventManager.notifyError("Error al configurar BCD: " + error);
    } else {
        eventManager.notifyLogUpdate("BCD configurado exitosamente para arranque EFI.\r\n");
        eventManager.notifyProgressUpdate(100);
    }
}