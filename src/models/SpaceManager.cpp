#include "SpaceManager.h"
#include "VolumeDetector.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include "../utils/Utils.h"
#include "../utils/constants.h"

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
} // namespace

SpaceManager::SpaceManager(EventManager *eventManager)
    : eventManager_(eventManager), monitoredDrive_(detectSystemDrive()) {}

SpaceManager::~SpaceManager() {}

SpaceValidationResult SpaceManager::validateAvailableSpace() {
    long long availableGB = getAvailableSpaceGB();

    SpaceValidationResult result;
    result.availableGB = availableGB;
    result.isValid     = availableGB >= 10;

    // Log to file
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\space_validation.log").c_str());
    if (logFile) {
        logFile << "Available space: " << availableGB << " GB\n";
        logFile << "Is valid: " << (result.isValid ? "yes" : "no") << "\n";
        if (!result.isValid) {
            std::ostringstream oss;
            oss << "No hay suficiente espacio disponible. Se requieren al menos 10 GB, pero solo hay " << availableGB
                << " GB disponibles.";
            result.errorMessage = oss.str();
            logFile << "Error: " << result.errorMessage << "\n";
        }
        logFile.close();
    }

    return result;
}

long long SpaceManager::getAvailableSpaceGB(const std::string &driveRoot) {
    std::string target = driveRoot.empty() ? monitoredDrive_ : normalizeDriveRoot(driveRoot);
    if (target.empty()) {
        target          = detectSystemDrive();
        monitoredDrive_ = target;
    }

    ULARGE_INTEGER freeBytesAvailable{}, totalNumberOfBytes{}, totalNumberOfFreeBytes{};
    if (GetDiskFreeSpaceExA(target.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return static_cast<long long>(freeBytesAvailable.QuadPart / (1024LL * 1024 * 1024));
    }
    return 0;
}

bool SpaceManager::performSpaceRecovery() {
    // Always recover space to ensure clean state
    if (eventManager_)
        eventManager_->notifyLogUpdate("Recuperando espacio para particiones...\r\n");
    if (!recoverSpace()) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: Falló la recuperación de espacio.\r\n");
        return false;
    }
    return true;
}

bool SpaceManager::recoverSpace() {
    if (eventManager_)
        eventManager_->notifyLogUpdate("Iniciando recuperación de espacio...\r\n");

    // Check if Windows is using the ISOEFI partition
    VolumeDetector volumeDetector(eventManager_);
    bool           windowsUsingEfi = volumeDetector.isWindowsUsingEfiPartition();

    if (windowsUsingEfi) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("ADVERTENCIA: Windows está usando la partición ISOEFI. "
                                           "Solo se eliminará ISOBOOT, ISOEFI se preservará.\r\n");
    }

    // Create PowerShell script to recover space
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "recover_ps", 0, tempFile);
    std::string psFile = std::string(tempFile) + ".ps1";

    std::ofstream scriptFile(psFile);
    if (!scriptFile) {
        return false;
    }

    // Build the volume filter - if Windows is using EFI, only remove ISOBOOT
    if (windowsUsingEfi) {
        scriptFile << "$volumes = Get-Volume | Where-Object { $_.FileSystemLabel -eq '" << VOLUME_LABEL << "' }\n";
    } else {
        scriptFile << "$volumes = Get-Volume | Where-Object { $_.FileSystemLabel -eq '" << VOLUME_LABEL
                   << "' -or $_.FileSystemLabel -eq '" << EFI_VOLUME_LABEL << "' }\n";
    }

    scriptFile << "foreach ($vol in $volumes) {\n";
    scriptFile << "    $part = Get-Partition | Where-Object { $_.AccessPaths -contains $vol.Path }\n";
    scriptFile << "    if ($part) {\n";
    scriptFile << "        Remove-PartitionAccessPath -DiskNumber 0 -PartitionNumber $part.PartitionNumber -AccessPath "
                  "$vol.Path -Confirm:$false\n";
    scriptFile << "        Remove-Partition -DiskNumber 0 -PartitionNumber $part.PartitionNumber -Confirm:$false\n";
    scriptFile << "    }\n";
    scriptFile << "}\n";
    scriptFile << "$systemPartition = Get-Partition | Where-Object { $_.DriveLetter -eq 'C' }\n";
    scriptFile << "if ($systemPartition) {\n";
    scriptFile << "    $supportedSize = Get-PartitionSupportedSize -DiskNumber 0 -PartitionNumber "
                  "$systemPartition.PartitionNumber\n";
    scriptFile << "    if ($systemPartition.Size -lt $supportedSize.SizeMax) {\n";
    scriptFile << "        Resize-Partition -DiskNumber 0 -PartitionNumber $systemPartition.PartitionNumber -Size "
                  "$supportedSize.SizeMax -Confirm:$false\n";
    scriptFile << "    }\n";
    scriptFile << "}\n";
    scriptFile.close();

    // Log the script content
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logScriptFile((logDir + "\\recover_script_log.txt").c_str());
    if (logScriptFile) {
        std::ifstream readScript(psFile);
        std::string   scriptContent((std::istreambuf_iterator<char>(readScript)), std::istreambuf_iterator<char>());
        logScriptFile << scriptContent;
        logScriptFile.close();
    }
    if (eventManager_)
        eventManager_->notifyLogUpdate("Script de recuperación creado.\r\n");

    // Execute PowerShell script
    STARTUPINFOA        si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;
    HANDLE      hRead, hWrite;
    std::string output;
    char        buffer[1024];
    DWORD       bytesRead;
    DWORD       exitCode;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(psFile.c_str());
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo crear pipe para recuperación.\r\n");
        return false;
    }

    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "powershell -ExecutionPolicy Bypass -File \"" + psFile + "\"";
    if (!CreateProcessA(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(psFile.c_str());
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo ejecutar PowerShell para recuperación.\r\n");
        return false;
    }

    CloseHandle(hWrite);

    if (eventManager_) {
        eventManager_->notifyLogUpdate("Ejecutando script de PowerShell para recuperar espacio...\r\n");
    }

    output.clear();
    std::string pendingLine;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        std::string chunk(buffer, bytesRead);
        output += chunk;

        if (eventManager_) {
            pendingLine += chunk;
            std::size_t newlinePos;
            while ((newlinePos = pendingLine.find('\n')) != std::string::npos) {
                std::string line     = pendingLine.substr(0, newlinePos + 1);
                std::string utf8Line = Utils::ansi_to_utf8(line);
                if (!utf8Line.empty()) {
                    eventManager_->notifyLogUpdate(utf8Line);
                }
                pendingLine.erase(0, newlinePos + 1);
            }
        }
    }

    if (eventManager_ && !pendingLine.empty()) {
        std::string utf8Remainder = Utils::ansi_to_utf8(pendingLine);
        if (!utf8Remainder.empty()) {
            eventManager_->notifyLogUpdate(utf8Remainder);
        }
    }

    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 300000); // 5 minutes

    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Log the result
    std::ofstream logFile((logDir + "\\recover_log.txt").c_str());
    if (logFile) {
        logFile << "Recover script exit code: " << exitCode << "\n";
        logFile << "Output:\n" << Utils::ansi_to_utf8(output) << "\n";
        logFile.close();
    }

    // Clean up
    DeleteFileA(psFile.c_str());

    if (exitCode == 0) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Espacio recuperado exitosamente.\r\n");
        return true;
    } else {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: Falló la recuperación de espacio (código " +
                                           std::to_string(exitCode) + ").\r\n");
        return false;
    }
}