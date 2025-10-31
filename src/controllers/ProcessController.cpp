#include "ProcessController.h"
#include <memory>

ProcessController::ProcessController(EventManager& eventManager)
    : eventManager(eventManager), workerThread(nullptr)
{
    partitionManager = &PartitionManager::getInstance();
    partitionManager->setEventManager(&eventManager);
    isoCopyManager = &ISOCopyManager::getInstance();
    bcdManager = &BCDManager::getInstance();
    bcdManager->setEventManager(&eventManager);
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

void ProcessController::startProcess(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootMode, bool skipIntegrityCheck)
{
    eventManager.notifyProgressUpdate(0);

    // Trim the isoPath to remove leading/trailing whitespace, quotes and common invisible characters
    std::string trimmedIsoPath = isoPath;
    auto isTrimChar = [](char c)->bool {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::isspace(uc) || uc <= 32 || uc == 0xA0 || c == '"' || c == '\'' || c == '\0';
    };
    while (!trimmedIsoPath.empty() && isTrimChar(trimmedIsoPath.front())) trimmedIsoPath.erase(trimmedIsoPath.begin());
    while (!trimmedIsoPath.empty() && isTrimChar(trimmedIsoPath.back())) trimmedIsoPath.pop_back();

    eventManager.notifyLogUpdate("ISO path after trimming: '" + trimmedIsoPath + "'\r\n");

    if (trimmedIsoPath.empty()) {
        eventManager.notifyLogUpdate("Error: No se ha seleccionado un archivo ISO.\r\n");
        return;
    }

    eventManager.notifyLogUpdate("Iniciando proceso con ISO: " + trimmedIsoPath + ", formato: " + selectedFormat + ", modo: " + selectedBootMode + "\r\n");

    bool partitionExists = partitionManager->partitionExists();
    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            eventManager.notifyLogUpdate("Error: " + validation.errorMessage + "\r\n");
            return;
        }

        // Aqui se pueden agregar confirmaciones si es necesario, pero por simplicidad, asumir que se confirma.
    }

    // Iniciar hilo
    workerThread = new std::thread(&ProcessController::processInThread, this, trimmedIsoPath, selectedFormat, selectedBootMode, skipIntegrityCheck);
}

void ProcessController::processInThread(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootMode, bool skipIntegrityCheck)
{
    eventManager.notifyLogUpdate("Verificando estado de particiones...\r\n");
    bool partitionExists = partitionManager->partitionExists();

    if (partitionExists) {
        eventManager.notifyLogUpdate("Particiones existentes detectadas. Iniciando reformateo...\r\n");
        eventManager.notifyProgressUpdate(20);
        if (!partitionManager->reformatPartition(selectedFormat)) {
            eventManager.notifyLogUpdate("Error: Fallo el reformateo de la particion.\r\n");
            eventManager.notifyError("Error al reformatear la particion.");
            eventManager.notifyButtonEnable();
            return;
        }
        eventManager.notifyLogUpdate("Particion reformateada exitosamente.\r\n");
        eventManager.notifyProgressUpdate(30);
    } else {
        eventManager.notifyLogUpdate("Validando espacio disponible en disco...\r\n");
        eventManager.notifyProgressUpdate(10);

        eventManager.notifyLogUpdate("Creando nuevas particiones...\r\n");
        eventManager.notifyProgressUpdate(20);

        if (!partitionManager->createPartition(selectedFormat, skipIntegrityCheck)) {
            eventManager.notifyLogUpdate("Error: Fallo la creacion de la particion.\r\n");
            eventManager.notifyError("Error al crear la particion.");
            eventManager.notifyButtonEnable();
            return;
        }
        eventManager.notifyLogUpdate("Particion creada exitosamente.\r\n");
        eventManager.notifyProgressUpdate(30);
    }

    eventManager.notifyLogUpdate("Verificando acceso a particiones...\r\n");
    // Verificar particiones
    std::string partitionDrive = partitionManager->getPartitionDriveLetter();
    if (partitionDrive.empty()) {
        eventManager.notifyLogUpdate("Error: No se puede acceder a la particion ISOBOOT.\r\n");
        eventManager.notifyError("Error: No se puede acceder a la particion ISOBOOT.");
        eventManager.notifyButtonEnable();
        return;
    }
    eventManager.notifyLogUpdate("Particion ISOBOOT encontrada en: " + partitionDrive + "\r\n");

    std::string espDrive = partitionManager->getEfiPartitionDriveLetter();
    if (espDrive.empty()) {
        eventManager.notifyLogUpdate("Error: No se puede acceder a la particion ISOEFI.\r\n");
        eventManager.notifyError("Error: No se puede acceder a la particion ISOEFI.");
        eventManager.notifyButtonEnable();
        return;
    }
    eventManager.notifyLogUpdate("Particion ISOEFI encontrada en: " + espDrive + "\r\n");

    // Always reformat EFI partition
    if (!partitionManager->reformatEfiPartition()) {
        eventManager.notifyLogUpdate("Error: Fallo el reformateo de la particion EFI.\r\n");
        eventManager.notifyError("Error al reformatear la particion EFI.");
        eventManager.notifyButtonEnable();
        return;
    }

    eventManager.notifyLogUpdate("Iniciando preparacion de archivos del ISO...\r\n");
    if (copyISO(isoPath, partitionDrive, espDrive, selectedBootMode)) {
        eventManager.notifyLogUpdate("Archivos preparados. Configurando BCD...\r\n");
        // Only configure BCD for Windows ISOs; non-Windows EFI boot directly from ESP
        if (isoCopyManager->getIsWindowsISO()) {
            configureBCD(partitionDrive, espDrive, selectedBootMode);
        } else {
            eventManager.notifyLogUpdate("ISO no-Windows detectado: omitiendo configuracion BCD, usando arranque EFI directo desde ESP.\r\n");
            eventManager.notifyProgressUpdate(100);
        }
        eventManager.notifyLogUpdate("Proceso completado exitosamente.\r\n");
        eventManager.notifyProgressUpdate(100);
        eventManager.notifyAskRestart();
    } else {
        eventManager.notifyLogUpdate("Error: Fallo la preparacion del contenido del ISO.\r\n");
        eventManager.notifyError("Proceso fallido debido a errores durante la preparacion del ISO.");
    }
    eventManager.notifyButtonEnable();
}

bool ProcessController::recoverSpace()
{
    return partitionManager->recoverSpace();
}

bool ProcessController::copyISO(const std::string& isoPath, const std::string& destPath, const std::string& espPath, const std::string& mode)
{
    eventManager.notifyLogUpdate("Montando imagen ISO...\r\n");
    eventManager.notifyProgressUpdate(40);

    std::string drive = destPath;
    std::string espDrive = espPath;

    if (mode == "Boot desde Memoria") {
        eventManager.notifyLogUpdate("Modo Boot desde Memoria seleccionado: extrayendo EFI y recursos para RAMDisk...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, false, true, true)) {
            eventManager.notifyLogUpdate("EFI y recursos de arranque preparados exitosamente (boot.wim/boot.sdi).\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error: Fallo la preparacion de archivos para modo Boot desde Memoria.\r\n");
            return false;
        }
    } else {
        eventManager.notifyLogUpdate("Modo " + mode + " seleccionado: extrayendo contenido completo del ISO...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, true, false, false)) {
            eventManager.notifyLogUpdate("Contenido del ISO extraido exitosamente.\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error: Fallo la extraccion del contenido del ISO.\r\n");
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
        eventManager.notifyLogUpdate("Error: Modo de boot '" + mode + "' no vAlido.\r\n");
        return;
    }

    eventManager.notifyLogUpdate("Aplicando estrategia de configuracion BCD para modo " + mode + "...\r\n");
    std::string error = bcdManager->configureBCD(driveLetter.substr(0, 2), espDriveLetter.substr(0, 2), *strategy);
    if (!error.empty()) {
        eventManager.notifyLogUpdate("Error en configuracion BCD: " + error + "\r\n");
        eventManager.notifyError("Error al configurar BCD: " + error);
    } else {
        eventManager.notifyLogUpdate("BCD configurado exitosamente para arranque EFI.\r\n");
        eventManager.notifyProgressUpdate(100);
    }
}
