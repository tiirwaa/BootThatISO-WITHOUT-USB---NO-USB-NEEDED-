#include "PartitionReformatter.h"
#include "EventManager.h"
#include "VolumeDetector.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationHelpers.h"
#include "../utils/constants.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>

namespace {
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

PartitionReformatter::PartitionReformatter(EventManager *eventManager) : eventManager_(eventManager) {}

PartitionReformatter::~PartitionReformatter() = default;

bool PartitionReformatter::reformatPartition(const std::string &format) {
    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.reformatter.startingData", "Iniciando reformateo de partición...\r\n"));

    std::string fsFormat;
    if (format == "EXFAT") {
        fsFormat = "exfat";
    } else if (format == "NTFS") {
        fsFormat = "ntfs";
    } else {
        fsFormat = "fat32";
    }

    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.reformatter.searchingVolume", "Buscando volumen para reformatear...\r\n"));

    // First, find the volume number by running diskpart list volume
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "listvol", 0, tempFile);

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        return false;
    }
    scriptFile << "list volume\n";
    scriptFile << "exit\n";
    scriptFile.close();

    STARTUPINFOA        si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(tempFile);
        return false;
    }

    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(tempFile);
        return false;
    }

    CloseHandle(hWrite);

    std::string output;
    char        buffer[1024];
    DWORD       bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 30000); // 30 seconds

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(tempFile);

    if (exitCode != 0) {
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::ofstream logFile((logDir + "\\" + REFORMAT_LOG_FILE).c_str());
        if (logFile) {
            logFile << "\xef\xbb\xbf"; // UTF-8 BOM
            logFile << "Diskpart list volume failed with exit code: " << exitCode << "\n";
            logFile << "Output:\n" << Utils::ansi_to_utf8(output) << "\n";
            logFile.close();
        }
        return false;
    }

    // Parse output to find volume number for BOOTTHATISO
    std::istringstream iss(output);
    std::string        line;
    int                volumeNumber = -1;
    while (std::getline(iss, line)) {
        size_t volPos = line.find("Volumen");
        if (volPos == std::string::npos) {
            volPos = line.find("Volume");
        }
        if (volPos != std::string::npos) {
            // Parse volume number and label
            std::string numStr   = line.substr(volPos + 8, 3);
            size_t      spacePos = numStr.find(' ');
            if (spacePos != std::string::npos) {
                numStr = numStr.substr(0, spacePos);
            }
            int volNum = std::atoi(numStr.c_str());

            std::string label = line.substr(volPos + 15, 13);
            // Trim leading and trailing spaces
            size_t start = label.find_first_not_of(" \t");
            size_t end   = label.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                label = label.substr(start, end - start + 1);
            } else {
                label = "";
            }

            if (label == VOLUME_LABEL) {
                volumeNumber = volNum;
                break;
            }
        }
    }

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + REFORMAT_LOG_FILE).c_str());
    if (logFile) {
        logFile << "\xef\xbb\xbf"; // UTF-8 BOM
        logFile << "Diskpart list volume output:\n" << Utils::ansi_to_utf8(output) << "\n";
        if (volumeNumber == -1) {
            logFile << "Volume with " << VOLUME_LABEL << " not found in output.\n";
        } else {
            logFile << "Found volume number: " << volumeNumber << "\n";
        }
        logFile.close();
    }

    if (volumeNumber == -1) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se encontró el volumen con etiqueta " +
                                           std::string(VOLUME_LABEL) + ".\r\n");
        return false;
    }

    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedFormatUtf8("log.reformatter.volumeFound", {Utils::utf8_to_wstring(std::to_string(volumeNumber))},
                                "Volumen encontrado (número {0}). Creando script de formateo...\r\n"));

    // Now, create script to select and format
    GetTempFileNameA(tempPath, "format", 0, tempFile);
    std::ostringstream formatScript;
    formatScript << "select volume " << volumeNumber << "\n";
    formatScript << "format fs=" << fsFormat << " quick label=\"" << VOLUME_LABEL << "\"\n";
    formatScript << "exit\n";
    scriptFile.open(tempFile);
    scriptFile << formatScript.str();
    scriptFile.close();

    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.reformatter.executing", "Ejecutando formateo de particion...\r\n"));

    std::string formatOutput;
    DWORD       formatExitCode = 0;
    std::string command        = "diskpart /s \"" + std::string(tempFile) + "\"";
    bool        ranDiskpart    = executeCommandHidden(command, 300000, formatOutput, formatExitCode);

    DeleteFileA(tempFile);

    std::ofstream formatLog((logDir + "\\" + REFORMAT_LOG_FILE).c_str(), std::ios::app);
    if (formatLog) {
        formatLog << "Diskpart format script:\n" << formatScript.str();
        formatLog << "Diskpart format output:\n" << Utils::ansi_to_utf8(formatOutput) << "\n";
        formatLog.close();
    }

    if (!ranDiskpart) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo ejecutar diskpart para formateo.\r\n");
        return false;
    }

    exitCode = formatExitCode;

    std::ofstream logFile2((logDir + "\\" + REFORMAT_EXIT_LOG_FILE).c_str(), std::ios::app);
    if (logFile2) {
        logFile2 << "Format command exit code: " << exitCode << "\n";
        logFile2.close();
    }

    if (exitCode == 0) {
        Sleep(5000);
        Utils::exec("mountvol /r");
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.reformatter.success", "Particion reformateada exitosamente.\r\n"));
        return true;
    }

    if (exitCode == DISKPART_DEVICE_IN_USE) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                "Aviso: Diskpart indico que la particion esta en uso. Intentando formateo alternativo...\r\n");
        std::string psOutput;
        DWORD       psExitCode  = 0;
        bool        ranFallback = formatVolumeWithPowerShell(VOLUME_LABEL, fsFormat, psOutput, psExitCode);

        std::ofstream fallLog((logDir + "\\" + REFORMAT_LOG_FILE).c_str(), std::ios::app);
        if (fallLog) {
            fallLog << "PowerShell fallback exit code: " << psExitCode << "\n";
            fallLog << "PowerShell fallback output:\n" << Utils::ansi_to_utf8(psOutput) << "\n";
            fallLog.close();
        }
        std::ofstream fallExit((logDir + "\\" + REFORMAT_EXIT_LOG_FILE).c_str(), std::ios::app);
        if (fallExit) {
            fallExit << "PowerShell fallback exit code: " << psExitCode << "\n";
            fallExit.close();
        }

        if (ranFallback && psExitCode == 0) {
            Sleep(5000);
            Utils::exec("mountvol /r");
            if (eventManager_)
                eventManager_->notifyLogUpdate("Particion reformateada exitosamente (metodo alternativo).\r\n");
            return true;
        }

        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: El formateo alternativo tambien fallo.\r\n");
        return false;
    }

    if (eventManager_)
        eventManager_->notifyLogUpdate("Error: Fallo el formateo de la particion (codigo " + std::to_string(exitCode) +
                                       ").\r\n");
    return false;
}

bool PartitionReformatter::reformatEfiPartition() {
    // Check if Windows is using the ISOEFI partition
    VolumeDetector volumeDetector(eventManager_);
    if (volumeDetector.isWindowsUsingEfiPartition()) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.reformatter.windowsUsingEfi",
                                                           "ADVERTENCIA: Windows está usando la partición ISOEFI. No "
                                                           "se formateará para preservar la instalación de Windows. "
                                                           "Solo se limpiarán los archivos de BootThatISO.") +
                                           "\r\n");

        // Log this detection
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::ofstream logFile((logDir + "\\" + REFORMAT_LOG_FILE).c_str(), std::ios::app);
        if (logFile) {
            logFile << "Windows installation detected in ISOEFI - skipping format, will clean BootThatISO files only\n";
            logFile.close();
        }

        // Instead of formatting, just clean BootThatISO-specific files
        return cleanBootThatISOFiles();
    }

    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.reformatter.starting", "Iniciando reformateo de partición EFI...") + "\r\n");

    // First, find the volume number by running diskpart list volume
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "listvol_efi", 0, tempFile);

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        return false;
    }
    scriptFile << "list volume\n";
    scriptFile << "exit\n";
    scriptFile.close();

    STARTUPINFOA        si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(tempFile);
        return false;
    }

    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput  = hWrite;
    si.hStdError   = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(tempFile);
        return false;
    }

    CloseHandle(hWrite);

    std::string output;
    char        buffer[1024];
    DWORD       bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 30000); // 30 seconds

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(tempFile);

    if (exitCode != 0) {
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::ofstream logFile((logDir + "\\" + REFORMAT_LOG_FILE).c_str(), std::ios::app);
        if (logFile) {
            logFile << "Diskpart list volume for EFI failed with exit code: " << exitCode << "\n";
            logFile << "Output:\n" << Utils::ansi_to_utf8(output) << "\n";
            logFile.close();
        }
        return false;
    }

    // Parse output to find volume number for EFI_VOLUME_LABEL
    std::istringstream iss(output);
    std::string        line;
    int                volumeNumber = -1;
    while (std::getline(iss, line)) {
        size_t volPos = line.find("Volumen");
        if (volPos == std::string::npos) {
            volPos = line.find("Volume");
        }
        if (volPos != std::string::npos) {
            // Parse volume number and label
            std::string numStr   = line.substr(volPos + 8, 3);
            size_t      spacePos = numStr.find(' ');
            if (spacePos != std::string::npos) {
                numStr = numStr.substr(0, spacePos);
            }
            int volNum = std::atoi(numStr.c_str());

            std::string label = line.substr(volPos + 15, 13);
            // Trim leading and trailing spaces
            size_t start = label.find_first_not_of(" \t");
            size_t end   = label.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                label = label.substr(start, end - start + 1);
            } else {
                label = "";
            }

            if (label == EFI_VOLUME_LABEL) {
                volumeNumber = volNum;
                break;
            }
        }
    }

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + REFORMAT_LOG_FILE).c_str(), std::ios::app);
    if (logFile) {
        logFile << "Diskpart list volume for EFI output:\n" << Utils::ansi_to_utf8(output) << "\n";
        if (volumeNumber == -1) {
            logFile << "Volume with " << EFI_VOLUME_LABEL << " not found in output.\n";
        } else {
            logFile << "Found EFI volume number: " << volumeNumber << "\n";
        }
        logFile.close();
    }

    if (volumeNumber == -1) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se encontró el volumen EFI con etiqueta " +
                                           std::string(EFI_VOLUME_LABEL) + ".\r\n");
        return false;
    }

    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedFormatUtf8("log.reformat.efi_found", {Utils::utf8_to_wstring(std::to_string(volumeNumber))},
                                "Volumen EFI encontrado (número {0}). Creando script de formateo...\r\n"));

    // Now, create script to select and format EFI
    const std::string fsFormat = "fat32";
    GetTempFileNameA(tempPath, "format_efi", 0, tempFile);
    std::ostringstream formatScript;
    formatScript << "select volume " << volumeNumber << "\n";
    formatScript << "format fs=" << fsFormat << " quick label=\"" << EFI_VOLUME_LABEL << "\"\n";
    // Reapply GPT attributes after formatting: EFI system partition (0x8000000000000000) - not hidden
    formatScript << "gpt attributes=0x8000000000000000\n";
    formatScript << "exit\n";
    scriptFile.open(tempFile);
    scriptFile << formatScript.str();
    scriptFile.close();

    // Execute the format script
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = NULL;
    si.hStdError   = NULL;

    cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        DeleteFileA(tempFile);
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo ejecutar diskpart para formateo EFI.\r\n");
        return false;
    }

    if (eventManager_)
        eventManager_->notifyLogUpdate(
            LocalizedOrUtf8("log.reformatter.executing", "Ejecutando formateo de partición EFI...") + "\r\n");

    WaitForSingleObject(pi.hProcess, 300000); // 5 minutes

    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(tempFile);

    // Wait a bit for the system to recognize the changes
    Sleep(5000);

    // Refresh volume information
    Utils::exec("mountvol /r");

    std::ofstream logFile2((logDir + "\\" + REFORMAT_EXIT_LOG_FILE).c_str(), std::ios::app);
    if (logFile2) {
        logFile2 << "EFI Format command exit code: " << exitCode << "\n";
        logFile2.close();
    }

    if (exitCode == 0) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                LocalizedOrUtf8("log.reformatter.success", "Partición EFI reformateada exitosamente.") + "\r\n");
        return true;
    } else {
        if (eventManager_) {
            std::string message =
                LocalizedOrUtf8("log.reformatter.failed", "Error: Falló el formateo de la partición EFI (código {0}).");
            size_t pos = message.find("{0}");
            if (pos != std::string::npos) {
                message.replace(pos, 3, std::to_string(exitCode));
            }
            eventManager_->notifyLogUpdate(message + "\r\n");
        }
        return false;
    }
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

bool PartitionReformatter::cleanBootThatISOFiles() {
    if (eventManager_)
        eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.reformatter.cleaningFiles",
                                                       "Limpiando archivos de BootThatISO de la partici�n EFI...") +
                                       "\r\n");

    VolumeDetector volumeDetector(eventManager_);
    std::string    efiDrive = volumeDetector.getEfiPartitionDriveLetter();

    if (efiDrive.empty()) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo encontrar la partici�n ISOEFI.\r\n");
        return false;
    }

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + REFORMAT_LOG_FILE).c_str(), std::ios::app);

    if (logFile) {
        logFile << "Cleaning BootThatISO files from: " << efiDrive << "\n";
    }

    // List of BootThatISO-specific files and directories to remove
    std::vector<std::string> filesToRemove = {
        efiDrive + "BOOTTHATISO_TEMP_PARTITION.txt",
        efiDrive + "EFI\\BOOT\\grub.cfg",    // GRUB config if present
        efiDrive + "EFI\\BOOT\\grubx64.efi", // GRUB bootloader
        efiDrive + "boot.sdi",               // RAMDisk files
        efiDrive + "sources\\boot.wim"       // Boot WIM if copied
    };

    // Directories to try removing (only if empty after file removal)
    std::vector<std::string> dirsToClean = {efiDrive + "sources", efiDrive + "EFI\\BOOT"};

    int filesRemoved = 0;
    for (const auto &filePath : filesToRemove) {
        DWORD attrs = GetFileAttributesA(filePath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            if (DeleteFileA(filePath.c_str())) {
                filesRemoved++;
                if (logFile) {
                    logFile << "Removed: " << filePath << "\n";
                }
                if (eventManager_)
                    eventManager_->notifyLogUpdate("Eliminado: " + filePath + "\r\n");
            } else {
                if (logFile) {
                    logFile << "Failed to remove: " << filePath << " (error " << GetLastError() << ")\n";
                }
            }
        }
    }

    // Try to remove directories if they're empty
    for (const auto &dirPath : dirsToClean) {
        DWORD attrs = GetFileAttributesA(dirPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            if (RemoveDirectoryA(dirPath.c_str())) {
                if (logFile) {
                    logFile << "Removed empty directory: " << dirPath << "\n";
                }
            } else {
                // Directory not empty or error - this is okay, Windows files might be there
                if (logFile) {
                    logFile << "Directory not removed (not empty or in use): " << dirPath << "\n";
                }
            }
        }
    }

    if (logFile) {
        logFile << "Cleanup complete. Files removed: " << filesRemoved << "\n";
        logFile.close();
    }

    if (eventManager_) {
        if (filesRemoved > 0) {
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.reformatter.cleanupSuccess",
                                                           "Limpieza completada. Archivos de BootThatISO eliminados.") +
                                           "\r\n");
        } else {
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.reformatter.noFilesToClean",
                                                           "No se encontraron archivos de BootThatISO para limpiar.") +
                                           "\r\n");
        }
    }

    return true;
}
