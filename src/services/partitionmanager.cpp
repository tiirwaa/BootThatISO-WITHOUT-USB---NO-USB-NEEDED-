#include <mutex>
#include <string>
#include <fstream>
#include <windows.h>
#include "partitionmanager.h"
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"

namespace {
std::string normalizeDriveRoot(const std::string &drive) {
    if (drive.empty()) {
        return "";
    }
    std::string normalized = drive;
    if (normalized.size() >= 2 && normalized[1] == ':') {
        normalized = normalized.substr(0, 2);
    }
    if (normalized.size() == 1 && (normalized[0] == '\\' || normalized[0] == '/')) {
        return normalized;
    }
    if (!normalized.empty() && normalized.back() != '\\' && normalized.back() != '/') {
        normalized += "\\";
    }
    return normalized;
}

std::string detectSystemDrive() {
    char  buffer[MAX_PATH] = {0};
    DWORD length           = GetEnvironmentVariableA("SystemDrive", buffer, MAX_PATH);
    if (length >= 2 && buffer[1] == ':') {
        return normalizeDriveRoot(std::string(buffer, length));
    }
    char windowsDir[MAX_PATH] = {0};
    UINT written              = GetWindowsDirectoryA(windowsDir, MAX_PATH);
    if (written >= 2 && windowsDir[1] == ':') {
        return normalizeDriveRoot(std::string(windowsDir, windowsDir + 2));
    }
    return "C:\\";
}

constexpr DWORD DISKPART_DEVICE_IN_USE = 0x80042413;

bool executeCommandHidden(const std::string &command, DWORD timeoutMs, std::string &output, DWORD &exitCode) {
    STARTUPINFOA        si = {sizeof(si)};
    PROCESS_INFORMATION pi{};
    SECURITY_ATTRIBUTES sa{};
    sa.nLength              = sizeof(sa);
    sa.bInheritHandle       = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hRead  = NULL;
    HANDLE hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return false;
    }

    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(NULL, const_cast<char *>(command.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }

    CloseHandle(hWrite);

    output.clear();
    char  buffer[1024];
    DWORD bytesRead = 0;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output.append(buffer, bytesRead);
    }

    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, timeoutMs);
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

bool formatVolumeWithPowerShell(const std::string &volumeLabel, const std::string &fsFormat, std::string &output,
                                DWORD &exitCode) {
    char tempPath[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempPath)) {
        return false;
    }
    char tempFile[MAX_PATH];
    if (!GetTempFileNameA(tempPath, "fmtps", 0, tempFile)) {
        return false;
    }

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        DeleteFileA(tempFile);
        return false;
    }

    scriptFile << "$ErrorActionPreference = 'Stop'\n";
    scriptFile << "$volume = Get-Volume | Where-Object { $_.FileSystemLabel -eq '" << volumeLabel << "' }\n";
    scriptFile << "if (-not $volume) {\n";
    scriptFile << "    Write-Output 'VolumeNotFound'\n";
    scriptFile << "    exit 3\n";
    scriptFile << "}\n";
    scriptFile << "try {\n";
    scriptFile << "    Format-Volume -InputObject $volume -FileSystem '" << fsFormat << "' -NewFileSystemLabel '"
               << volumeLabel << "' -Confirm:$false -Force -ErrorAction Stop | Out-Null\n";
    scriptFile << "    exit 0\n";
    scriptFile << "} catch {\n";
    scriptFile << "    Write-Output $_.Exception.Message\n";
    scriptFile << "    exit 4\n";
    scriptFile << "}\n";
    scriptFile.close();

    std::string command = "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + std::string(tempFile) + "\"";
    bool        ran     = executeCommandHidden(command, 300000, output, exitCode);

    DeleteFileA(tempFile);
    return ran;
}
} // namespace
// Helper to append and flush to general_log.log
void logToGeneral(const std::string &msg) {
    static std::mutex           logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::string                 logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + GENERAL_LOG_FILE).c_str(), std::ios::app);
    if (logFile) {
        logFile << msg << std::flush;
        logFile.close();
    }
}

#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cctype>

PartitionManager &PartitionManager::getInstance() {
    static PartitionManager instance;
    return instance;
}

PartitionManager::PartitionManager()
    : eventManager(nullptr), monitoredDrive(detectSystemDrive()), diskIntegrityChecker(nullptr),
      volumeDetector(nullptr), spaceManager(nullptr), diskpartExecutor(nullptr), partitionReformatter(nullptr),
      partitionCreator(nullptr) {}
void PartitionManager::setEventManager(EventManager *em) {
    eventManager = em;
    // Initialize components with the event manager
    diskIntegrityChecker = std::make_unique<DiskIntegrityChecker>(em);
    volumeDetector       = std::make_unique<VolumeDetector>(em);
    spaceManager         = std::make_unique<SpaceManager>(em);
    diskpartExecutor     = std::make_unique<DiskpartExecutor>(em);
    partitionReformatter = std::make_unique<PartitionReformatter>(em);
    partitionCreator     = std::make_unique<PartitionCreator>(em);
}
PartitionManager::~PartitionManager() {}

SpaceValidationResult PartitionManager::validateAvailableSpace() {
    return spaceManager->validateAvailableSpace();
}

long long PartitionManager::getAvailableSpaceGB(const std::string &driveRoot) {
    return spaceManager->getAvailableSpaceGB(driveRoot);
}

bool PartitionManager::createPartition(const std::string &format, bool skipIntegrityCheck) {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);

    // Step 0: Check if EFI partition exists with wrong size (backward compatibility)
    if (efiPartitionExists()) {
        int currentEfiSizeMB = getEfiPartitionSizeMB();
        if (eventManager)
            eventManager->notifyLogUpdate("Partición EFI detectada con tamaño: " + std::to_string(currentEfiSizeMB) +
                                          " MB\r\n");

        // CRITICAL: Check if Windows is using this ISOEFI partition
        bool windowsIsUsingEfi = volumeDetector->isWindowsUsingEfiPartition();

        if (windowsIsUsingEfi) {
            if (eventManager) {
                eventManager->notifyLogUpdate("\r\n");
                eventManager->notifyLogUpdate("========================================\r\n");
                eventManager->notifyLogUpdate("ADVERTENCIA CRÍTICA:\r\n");
                eventManager->notifyLogUpdate(
                    "Windows está usando la partición ISOEFI como partición EFI del sistema.\r\n");
                eventManager->notifyLogUpdate(
                    "NO se puede eliminar ni modificar esta partición sin hacer el sistema no booteable.\r\n");
                eventManager->notifyLogUpdate("========================================\r\n");
                eventManager->notifyLogUpdate("\r\n");
                eventManager->notifyLogUpdate("Solución: La partición ISOEFI será reutilizada sin modificaciones.\r\n");
                eventManager->notifyLogUpdate(
                    "NOTA: Esto significa que cualquier contenido previo en ISOEFI se mantendrá.\r\n");
                eventManager->notifyLogUpdate("\r\n");
            }

            // Mark that we should NOT delete/recreate the EFI partition
            // We'll just reuse it as-is
        } else {
            // Windows is not using this partition, safe to modify
            if (currentEfiSizeMB > 0 && currentEfiSizeMB != REQUIRED_EFI_SIZE_MB) {
                if (eventManager)
                    eventManager->notifyLogUpdate("ADVERTENCIA: Partición EFI con tamaño incorrecto (" +
                                                  std::to_string(currentEfiSizeMB) + " MB, se requieren " +
                                                  std::to_string(REQUIRED_EFI_SIZE_MB) + " MB)\r\n");
                if (eventManager)
                    eventManager->notifyLogUpdate("Windows NO está usando esta partición. Eliminando AMBAS particiones "
                                                  "para recrearlas con el nuevo tamaño...\r\n");

                // Delete both partitions to recreate with correct size
                if (!spaceManager->recoverSpace()) {
                    if (eventManager)
                        eventManager->notifyLogUpdate("Error: No se pudieron eliminar las particiones antiguas.\r\n");
                    return false;
                }

                if (eventManager)
                    eventManager->notifyLogUpdate("Particiones antiguas eliminadas exitosamente.\r\n");
            } else if (currentEfiSizeMB == REQUIRED_EFI_SIZE_MB) {
                if (eventManager)
                    eventManager->notifyLogUpdate("Partición EFI tiene el tamaño correcto (" +
                                                  std::to_string(REQUIRED_EFI_SIZE_MB) + " MB).\r\n");
            }
        }
    }

    // Step 1: Perform disk integrity check (unless skipped)
    if (!skipIntegrityCheck) {
        if (!diskIntegrityChecker->performDiskIntegrityCheck()) {
            return false;
        }
    }

    // Step 2: Verify disk is GPT
    if (!diskpartExecutor->isDiskGpt()) {
        if (eventManager)
            eventManager->notifyLogUpdate(
                "Error: El disco no es GPT. La aplicación requiere un disco GPT para crear particiones EFI.\r\n");
        return false;
    } else {
        if (eventManager)
            eventManager->notifyLogUpdate(
                "Disco confirmado como GPT. Procediendo con la creación de particiones...\r\n");
    }

    // Step 3: Recover space for partitions (if not already done in Step 0)
    bool needsToCreatePartitions = false;

    if (!partitionExists() && !efiPartitionExists()) {
        // No partitions exist, need to create them
        if (eventManager)
            eventManager->notifyLogUpdate("No se detectaron particiones existentes. Creando nuevas particiones...\r\n");

        if (!spaceManager->recoverSpace()) {
            if (eventManager)
                eventManager->notifyLogUpdate("Error: Falló la recuperación de espacio.\r\n");
            return false;
        }
        needsToCreatePartitions = true;
    } else if (partitionExists() && efiPartitionExists()) {
        // Both partitions exist - check if they have correct sizes
        if (eventManager)
            eventManager->notifyLogUpdate("Particiones ISOBOOT e ISOEFI ya existen. Verificando integridad...\r\n");

        // Partitions already exist and have been validated in Step 0
        // No need to create them again
        needsToCreatePartitions = false;

        if (eventManager)
            eventManager->notifyLogUpdate("Usando particiones existentes.\r\n");
    } else {
        // Only one partition exists - this is an inconsistent state, recreate both
        if (eventManager)
            eventManager->notifyLogUpdate("Estado inconsistente: Solo una partición existe. Recreando ambas...\r\n");

        if (!spaceManager->recoverSpace()) {
            if (eventManager)
                eventManager->notifyLogUpdate("Error: Falló la recuperación de espacio.\r\n");
            return false;
        }
        needsToCreatePartitions = true;
    }

    // Step 4: Create partitions using diskpart (only if needed)
    if (needsToCreatePartitions) {
        if (!partitionCreator->performDiskpartOperations(format)) {
            return false;
        }

        // Step 5: Verify partitions were created successfully
        if (!partitionCreator->verifyPartitionsCreated()) {
            if (eventManager)
                eventManager->notifyLogUpdate(
                    "Advertencia: No se pudo verificar la creación de particiones, pero diskpart reportó éxito.\r\n");
        }
    } else {
        if (eventManager)
            eventManager->notifyLogUpdate("Saltando creación de particiones: las particiones ya existen.\r\n");
    }

    return true;
}

bool PartitionManager::partitionExists() {
    return volumeDetector->partitionExists();
}

bool PartitionManager::efiPartitionExists() {
    return volumeDetector->efiPartitionExists();
}

std::string PartitionManager::getPartitionDriveLetter() {
    return volumeDetector->getPartitionDriveLetter();
}

std::string PartitionManager::getEfiPartitionDriveLetter() {
    return volumeDetector->getEfiPartitionDriveLetter();
}

std::string PartitionManager::getPartitionFileSystem() {
    return volumeDetector->getPartitionFileSystem();
}

int PartitionManager::getEfiPartitionSizeMB() {
    return volumeDetector->getEfiPartitionSizeMB();
}

int PartitionManager::countEfiPartitions() {
    return volumeDetector->countEfiPartitions();
}

bool PartitionManager::reformatPartition(const std::string &format) {
    return partitionReformatter->reformatPartition(format);
}

bool PartitionManager::reformatEfiPartition() {
    return partitionReformatter->reformatEfiPartition();
}

bool PartitionManager::recoverSpace() {
    return spaceManager->recoverSpace();
}

bool PartitionManager::RestartComputer() {
    if (eventManager)
        eventManager->notifyLogUpdate("Intentando reiniciar el sistema...\r\n");

    // Enable shutdown privilege
    HANDLE           hToken;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        if (eventManager)
            eventManager->notifyLogUpdate("Error: No se pudo abrir el token del proceso.\r\n");
        return false;
    }
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount           = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0)) {
        if (eventManager)
            eventManager->notifyLogUpdate("Error: No se pudo ajustar los privilegios.\r\n");
        CloseHandle(hToken);
        return false;
    }
    if (GetLastError() != ERROR_SUCCESS) {
        if (eventManager)
            eventManager->notifyLogUpdate("Error: Falló la verificación de privilegios.\r\n");
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);

    // Attempt to restart the computer
    // Use InitiateShutdown to prevent Windows Update from installing during restart
    DWORD dwReason = SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_MAINTENANCE | SHTDN_REASON_FLAG_PLANNED;
    if (InitiateShutdownW(NULL, NULL, 0, SHUTDOWN_RESTART, dwReason) == ERROR_SUCCESS) {
        return true;
    } else {
        // Fallback to ExitWindowsEx if InitiateShutdown fails
        if (ExitWindowsEx(EWX_REBOOT | EWX_FORCE, dwReason)) {
            return true;
        } else {
            DWORD error = GetLastError();
            if (eventManager)
                eventManager->notifyLogUpdate(
                    "Error: No se pudo reiniciar el sistema. Código de error: " + std::to_string(error) + "\r\n");
            return false;
        }
    }
}
