#include "SpaceManager.h"
#include "VolumeDetector.h"
#include "../services/bcdmanager.h"
#include "../utils/LocalizationHelpers.h"
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
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.partition.space_recovery_failed", "Error: Falló la recuperación de espacio.\r\n"));
        return false;
    }
    return true;
}

bool SpaceManager::recoverSpace() {
    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.space.starting_recovery", "Iniciando recuperación de espacio...\r\n"));

    // Check if Windows is using the ISOEFI partition
    VolumeDetector volumeDetector(eventManager_);
    bool           windowsUsingEfi = volumeDetector.isWindowsUsingEfiPartition();

    if (windowsUsingEfi) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.partition.windows_using_efi",
                                                           "ADVERTENCIA: Windows está usando la partición ISOEFI. "
                                                           "Solo se eliminará ISOBOOT, ISOEFI se preservará.\r\n"));
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

    // Build the volume filter and deletion logic
    if (windowsUsingEfi) {
        // Windows is using one ISOEFI as system partition
        // Delete all ISOBOOT partitions
        scriptFile << "$volumes = Get-Volume | Where-Object { $_.FileSystemLabel -eq '" << VOLUME_LABEL << "' }\n";
        scriptFile << "foreach ($vol in $volumes) {\n";
        scriptFile << "    $part = Get-Partition | Where-Object { $_.AccessPaths -contains $vol.Path }\n";
        scriptFile << "    if ($part) {\n";
        scriptFile << "        $accessPaths = $part.AccessPaths | Where-Object { $_ -match '^[A-Z]:\\\\$' }\n";
        scriptFile << "        foreach ($path in $accessPaths) {\n";
        scriptFile << "            try {\n";
        scriptFile << "                Remove-PartitionAccessPath -DiskNumber 0 -PartitionNumber "
                      "$part.PartitionNumber -AccessPath $path -Confirm:$false -ErrorAction SilentlyContinue\n";
        scriptFile << "            } catch { }\n";
        scriptFile << "        }\n";
        scriptFile << "        Remove-Partition -DiskNumber 0 -PartitionNumber $part.PartitionNumber -Confirm:$false\n";
        scriptFile << "    }\n";
        scriptFile << "}\n";

        // Delete EFI partitions that DON'T contain bootmgfw.efi (system EFI marker)
        scriptFile << "Write-Host \"Starting EFI partition detection...\"\n";
        scriptFile << "Write-Host \"Checking if running as admin...\"\n";
        scriptFile << "$isAdmin = "
                      "([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent())."
                      "IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)\n";
        scriptFile << "Write-Host \"Is admin: $isAdmin\"\n";
        scriptFile << "Write-Host \"Getting all partitions...\"\n";
        scriptFile << "$allPartitions = Get-Partition\n";
        scriptFile << "Write-Host \"Total partitions found: $($allPartitions.Count)\"\n";
        scriptFile << "foreach ($p in $allPartitions) { Write-Host \"Partition $($p.PartitionNumber): Type=$($p.Type), "
                      "Size=$($p.Size)\" }\n";
        scriptFile << "$efiPartitions = Get-Partition | Where-Object { $_.Type -eq 'System' }\n";
        scriptFile << "Write-Host \"Found $($efiPartitions.Count) EFI partitions to check\"\n";
        scriptFile << "foreach ($part in $efiPartitions) {\n";
        scriptFile << "    $isSystemEfi = $false\n";
        scriptFile << "    Write-Host \"Checking EFI partition: $($part.PartitionNumber) - Size: $($part.Size)\"\n";
        scriptFile << "    \n";
        scriptFile << "    # Try to find existing drive letter\n";
        scriptFile << "    $driveLetter = $null\n";
        scriptFile << "    if ($part.DriveLetter) {\n";
        scriptFile << "        $driveLetter = $part.DriveLetter\n";
        scriptFile << "        Write-Host \"Partition has drive letter: $driveLetter\"\n";
        scriptFile << "    } else {\n";
        scriptFile << "        # Assign temp drive letter\n";
        scriptFile << "        $availableLetters = 90..67 | ForEach-Object { [char]$_ } | Where-Object { -not "
                      "(Get-Volume | Where-Object { $_.DriveLetter -eq $_ }) }\n";
        scriptFile << "        if ($availableLetters) {\n";
        scriptFile << "            $driveLetter = $availableLetters[0]\n";
        scriptFile
            << "            Write-Host \"Assigning temp drive $driveLetter to partition $($part.PartitionNumber)\"\n";
        scriptFile << "            Add-PartitionAccessPath -DiskNumber 0 -PartitionNumber $part.PartitionNumber "
                      "-AccessPath ($driveLetter + ':') 2>&1 | Out-Null\n";
        scriptFile << "        }\n";
        scriptFile << "    }\n";
        scriptFile << "    \n";
        scriptFile << "    if ($driveLetter) {\n";
        scriptFile << "        $bootmgfwPath = $driveLetter + ':\\EFI\\Microsoft\\Boot\\bootmgfw.efi'\n";
        scriptFile << "        Write-Host \"Checking path: $bootmgfwPath\"\n";
        scriptFile << "        if (Test-Path $bootmgfwPath) {\n";
        scriptFile << "            Write-Host \"Found bootmgfw.efi - This is SYSTEM EFI partition\"\n";
        scriptFile << "            $isSystemEfi = $true\n";
        scriptFile << "        } else {\n";
        scriptFile << "            Write-Host \"bootmgfw.efi NOT found\"\n";
        scriptFile << "        }\n";
        scriptFile << "        \n";
        scriptFile << "        # Remove temp drive if assigned\n";
        scriptFile << "        if (-not $part.DriveLetter) {\n";
        scriptFile << "            Write-Host \"Removing temp drive $driveLetter\"\n";
        scriptFile << "            Remove-PartitionAccessPath -DiskNumber 0 -PartitionNumber $part.PartitionNumber "
                      "-AccessPath ($driveLetter + ':') 2>&1 | Out-Null\n";
        scriptFile << "        }\n";
        scriptFile << "    } else {\n";
        scriptFile << "        Write-Host \"Could not assign drive letter to partition\"\n";
        scriptFile << "    }\n";
        scriptFile << "    \n";
        scriptFile << "    if (-not $isSystemEfi) {\n";
        scriptFile << "        Write-Host \"Deleting non-system EFI partition $($part.PartitionNumber)\"\n";
        scriptFile << "        # Remove all access paths\n";
        scriptFile << "        foreach ($path in $part.AccessPaths) {\n";
        scriptFile << "            if ($path -match '^[A-Z]:\\\\$') {\n";
        scriptFile << "                Remove-PartitionAccessPath -DiskNumber 0 -PartitionNumber $part.PartitionNumber "
                      "-AccessPath $path -Confirm:$false -ErrorAction SilentlyContinue\n";
        scriptFile << "            }\n";
        scriptFile << "        }\n";
        scriptFile << "        Remove-Partition -DiskNumber 0 -PartitionNumber $part.PartitionNumber -Confirm:$false\n";
        scriptFile << "    } else {\n";
        scriptFile << "        Write-Host \"Preserving system EFI partition $($part.PartitionNumber)\"\n";
        scriptFile << "    }\n";
        scriptFile << "}\n";
    } else {
        // Windows is NOT using ISOEFI - safe to delete all ISOBOOT and ISOEFI partitions
        scriptFile << "$volumes = Get-Volume | Where-Object { $_.FileSystemLabel -eq '" << VOLUME_LABEL
                   << "' -or $_.FileSystemLabel -eq '" << EFI_VOLUME_LABEL << "' }\n";
        scriptFile << "Write-Host \"Found volumes to remove: $($volumes.Count)\"\n";
        scriptFile << "foreach ($vol in $volumes) {\n";
        scriptFile << "    Write-Host \"Processing volume: $($vol.FileSystemLabel) at $($vol.Path)\"\n";
        scriptFile << "    $part = Get-Partition | Where-Object { $_.AccessPaths -contains $vol.Path }\n";
        scriptFile << "    if ($part) {\n";
        scriptFile << "        Write-Host \"Found partition: $($part.PartitionNumber)\"\n";
        scriptFile << "        $accessPaths = $part.AccessPaths | Where-Object { $_ -match '^[A-Z]:\\\\$' }\n";
        scriptFile << "        Write-Host \"Access paths: $($accessPaths)\"\n";
        scriptFile << "        foreach ($path in $accessPaths) {\n";
        scriptFile << "            try {\n";
        scriptFile << "                Write-Host \"Removing access path: $path\"\n";
        scriptFile << "                Remove-PartitionAccessPath -DiskNumber 0 -PartitionNumber "
                      "$part.PartitionNumber -AccessPath $path -Confirm:$false -ErrorAction SilentlyContinue\n";
        scriptFile << "            } catch { Write-Host \"Error removing access path: $_\" }\n";
        scriptFile << "        }\n";
        scriptFile << "        Write-Host \"Removing partition: $($part.PartitionNumber)\"\n";
        scriptFile << "        try {\n";
        scriptFile
            << "            Remove-Partition -DiskNumber 0 -PartitionNumber $part.PartitionNumber -Confirm:$false\n";
        scriptFile << "            Write-Host \"Partition removed successfully\"\n";
        scriptFile << "        } catch { Write-Host \"Error removing partition: $_\" }\n";
        scriptFile << "    } else {\n";
        scriptFile << "        Write-Host \"No partition found for volume $($vol.FileSystemLabel)\"\n";
        scriptFile << "    }\n";
        scriptFile << "}\n";
    }
    scriptFile << "$systemPartition = Get-Partition | Where-Object { $_.DriveLetter -eq 'C' }\n";
    scriptFile << "if ($systemPartition) {\n";
    scriptFile << "    Write-Host \"Found C: partition: $($systemPartition.PartitionNumber), size: "
                  "$($systemPartition.Size)\"\n";
    scriptFile << "    $supportedSize = Get-PartitionSupportedSize -DiskNumber 0 -PartitionNumber "
                  "$systemPartition.PartitionNumber\n";
    scriptFile << "    Write-Host \"Supported max size: $($supportedSize.SizeMax)\"\n";
    scriptFile << "    if ($systemPartition.Size -lt $supportedSize.SizeMax) {\n";
    scriptFile << "        Write-Host \"Resizing C: to max size\"\n";
    scriptFile << "        try {\n";
    scriptFile << "            Resize-Partition -DiskNumber 0 -PartitionNumber $systemPartition.PartitionNumber -Size "
                  "$supportedSize.SizeMax -Confirm:$false\n";
    scriptFile << "            Write-Host \"Resize completed successfully\"\n";
    scriptFile << "        } catch { Write-Host \"Error resizing: $_\" }\n";
    scriptFile << "    } else {\n";
    scriptFile << "        Write-Host \"C: already at max size\"\n";
    scriptFile << "    }\n";
    scriptFile << "} else {\n";
    scriptFile << "    Write-Host \"C: partition not found\"\n";
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
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.space.script_created", "Script de recuperación creado.\r\n"));

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
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.partition.pipe_creation_failed",
                                                           "Error: No se pudo crear pipe para recuperación.\r\n"));
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
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.partition.powershell_execution_failed",
                                "Error: No se pudo ejecutar PowerShell para recuperación.\r\n"));
        return false;
    }

    CloseHandle(hWrite);

    if (eventManager_) {
        eventManager_->notifyLogUpdate(LocalizedOrUtf8(
            "log.space.executing_script", "Ejecutando script de PowerShell para recuperar espacio...\r\n"));
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

    // Clean BCD entries created by BootThatISO
    if (eventManager_)
        eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.space.cleaning_bcd", "Limpiando entradas BCD...\r\n"));

    BCDManager &bcdManager = BCDManager::getInstance();
    bcdManager.setEventManager(eventManager_);
    bcdManager.cleanBootThatISOEntries();

    if (exitCode == 0) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.space.recovery_successful", "Espacio recuperado exitosamente.\r\n"));
        return true;
    } else {
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedFormatUtf8("log.space.recovery_failed", {Utils::utf8_to_wstring(std::to_string(exitCode))},
                                    "Error: Falló la recuperación de espacio (código {0}).\r\n"));
        return false;
    }
}