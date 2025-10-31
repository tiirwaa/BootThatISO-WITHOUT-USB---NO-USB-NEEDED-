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

// Helper function to read hash info from file
static HashInfo readHashInfo(const std::string& path) {
    HashInfo info = {"", "", ""};
    std::ifstream file(path);
    if (file.is_open()) {
        std::getline(file, info.hash);
        std::getline(file, info.mode);
        std::getline(file, info.format);
    }
    return info;
}

ProcessController::ProcessController(EventManager& eventManager)
    : eventManager(eventManager)
{
    partitionManager = &PartitionManager::getInstance();
    partitionManager->setEventManager(&eventManager);
    isoCopyManager = &ISOCopyManager::getInstance();
    bcdManager = &BCDManager::getInstance();
    bcdManager->setEventManager(&eventManager);
}

ProcessController::~ProcessController()
{
    if (workerThread.joinable()) {
        workerThread.join();
    }
    if (recoveryThread.joinable()) {
        recoveryThread.join();
    }
}

void ProcessController::requestCancel()
{
    eventManager.requestCancel();
    if (recoveryInProgress.load()) {
        eventManager.notifyLogUpdate("La recuperacion de espacio esta en curso y no admite cancelacion.\r\n");
    }
}

void ProcessController::startProcess(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootModeKey, const std::string& selectedBootModeLabel, bool skipIntegrityCheck, bool synchronous)
{
    Logger::instance().resetProcessLogs();
    const std::string logDir = Logger::instance().logDirectory();

    // Log to file
    std::ofstream logFile((logDir + "\\start_process.log").c_str());
    logFile << "startProcess called with synchronous: " << synchronous << "\n";
    logFile.close();

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

    eventManager.notifyLogUpdate("Iniciando proceso con ISO: " + trimmedIsoPath + ", formato: " + selectedFormat + ", modo: " + selectedBootModeLabel + "\r\n");

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
        workerThread = std::thread(&ProcessController::processInThread, this, trimmedIsoPath, selectedFormat, selectedBootModeKey, selectedBootModeLabel, skipIntegrityCheck);
    }
}

void ProcessController::processInThread(const std::string& isoPath, const std::string& selectedFormat, const std::string& selectedBootModeKey, const std::string& selectedBootModeLabel, bool skipIntegrityCheck)
{
    eventManager.notifyLogUpdate("Verificando estado de particiones...\r\n");
    bool partitionExists = partitionManager->partitionExists();

    bool skipFormat = false;
    if (partitionExists) {
        std::string partitionDrive = partitionManager->getPartitionDriveLetter();
        if (!partitionDrive.empty()) {
            std::string hashFilePath = partitionDrive + "\\ISOBOOTHASH";
            std::string md5 = Utils::calculateMD5(isoPath);
            HashInfo existing = readHashInfo(hashFilePath);
            if (existing.hash == md5 && ((existing.mode == selectedBootModeKey) || (existing.mode == selectedBootModeLabel)) && existing.format == selectedFormat && !existing.hash.empty()) {
                eventManager.notifyLogUpdate("Hash, modo y formato coinciden. Omitiendo formateo.\r\n");
                skipFormat = true;
            } else {
                eventManager.notifyLogUpdate("Hash o configuración no coinciden. Procediendo con formateo.\r\n");
            }
        } else {
            eventManager.notifyLogUpdate("No se pudo acceder a la partición existente para verificar hash. Formateando.\r\n");
        }
        if (!skipFormat) {
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
            eventManager.notifyProgressUpdate(30);
        }
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

    // Reformat EFI partition only if not skipping
    if (!skipFormat) {
        if (!partitionManager->reformatEfiPartition()) {
            eventManager.notifyLogUpdate("Error: Fallo el reformateo de la particion EFI.\r\n");
            eventManager.notifyError("Error al reformatear la particion EFI.");
            eventManager.notifyButtonEnable();
            return;
        }
    }

    eventManager.notifyLogUpdate("Iniciando preparacion de archivos del ISO...\r\n");
    if (copyISO(isoPath, partitionDrive, espDrive, selectedBootModeKey, selectedBootModeLabel, selectedFormat)) {
        eventManager.notifyLogUpdate("Archivos preparados. Configurando BCD...\r\n");
        // Configure BCD for Windows ISOs or for RAM boot mode (even non-Windows can use ramdisk)
        if (isoCopyManager->getIsWindowsISO() || selectedBootModeKey == AppKeys::BootModeRam) {
            configureBCD(partitionDrive, espDrive, selectedBootModeKey);
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

bool ProcessController::copyISO(const std::string& isoPath, const std::string& destPath, const std::string& espPath, const std::string& modeKey, const std::string& modeLabel, const std::string& format)
{
    eventManager.notifyLogUpdate("Montando imagen ISO...\r\n");
    eventManager.notifyProgressUpdate(40);

    std::string drive = destPath;
    std::string espDrive = espPath;

    if (modeKey == AppKeys::BootModeRam) {
        eventManager.notifyLogUpdate("Modo " + modeLabel + " seleccionado: extrayendo EFI y recursos para RAMDisk...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, false, true, true, modeKey, format)) {
            eventManager.notifyLogUpdate("EFI y recursos de arranque preparados exitosamente (boot.wim/boot.sdi).\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error: Fallo la preparacion de archivos para modo " + modeLabel + ".\r\n");
            return false;
        }
    } else {
        eventManager.notifyLogUpdate("Modo " + modeLabel + " seleccionado: extrayendo contenido completo del ISO...\r\n");
        if (isoCopyManager->extractISOContents(eventManager, isoPath, drive, espDrive, true, true, true, modeKey, format)) {
            eventManager.notifyLogUpdate("Contenido del ISO extraido exitosamente.\r\n");
            eventManager.notifyProgressUpdate(70);
        } else {
            eventManager.notifyLogUpdate("Error: Fallo la extraccion del contenido del ISO.\r\n");
            return false;
        }
    }
    return true;
}



void ProcessController::configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, const std::string& modeKey)
{
    eventManager.notifyLogUpdate("Configurando Boot Configuration Data (BCD)...\r\n");
    eventManager.notifyProgressUpdate(80);

    auto strategy = BootStrategyFactory::createStrategy(modeKey);
    std::string modeLabel = LocalizationManager::getInstance().getUtf8String("bootMode." + modeKey);
    if (modeLabel.empty()) {
        modeLabel = modeKey;
    }

    if (!strategy) {
        eventManager.notifyLogUpdate("Error: Modo de boot '" + modeLabel + "' no valido.\r\n");
        return;
    }

    eventManager.notifyLogUpdate("Aplicando estrategia de configuracion BCD para modo " + modeLabel + "...\r\n");
    std::string error = bcdManager->configureBCD(driveLetter.substr(0, 2), espDriveLetter.substr(0, 2), *strategy);
    if (!error.empty()) {
        eventManager.notifyLogUpdate("Error en configuracion BCD: " + error + "\r\n");
        eventManager.notifyError("Error al configurar BCD: " + error);
    } else {
        eventManager.notifyLogUpdate("BCD configurado exitosamente para arranque EFI.\r\n");
        eventManager.notifyProgressUpdate(100);
    }
}



void ProcessController::recoverSpaceInThread()
{
    eventManager.notifyLogUpdate("Recuperando espacio. Esto puede tardar varios minutos...\r\n");
    bool success = partitionManager->recoverSpace();
    recoveryInProgress.store(false);
    eventManager.notifyRecoverComplete(success);
}



