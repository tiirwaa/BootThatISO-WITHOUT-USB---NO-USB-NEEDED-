#include "PartitionCreator.h"
#include "EventManager.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>

PartitionCreator::PartitionCreator(EventManager *eventManager) : eventManager(eventManager) {}

bool PartitionCreator::performDiskpartOperations(const std::string &format) {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    if (eventManager)
        eventManager->notifyLogUpdate("Creando script de diskpart para particiones...\r\n");
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "diskpart", 0, tempFile);

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        if (eventManager)
            eventManager->notifyLogUpdate("Error: No se pudo crear el archivo de script de diskpart.\r\n");
        return false;
    }

    std::string fsFormat;
    if (format == "EXFAT") {
        fsFormat = "exfat";
    } else if (format == "NTFS") {
        fsFormat = "ntfs";
    } else {
        fsFormat = "fat32";
    }

    scriptFile << "select disk 0\n";
    scriptFile << "select volume C\n";
    scriptFile << "shrink desired=12000 minimum=12000\n";
    scriptFile << "create partition primary size=10000\n";
    scriptFile << "format fs=" << fsFormat << " quick label=\"" << VOLUME_LABEL << "\"\n";
    scriptFile << "create partition efi size=500\n";
    scriptFile << "format fs=fat32 quick label=\"" << EFI_VOLUME_LABEL << "\"\n";
    scriptFile << "exit\n";
    scriptFile.close();

    if (eventManager)
        eventManager->notifyLogUpdate("Ejecutando diskpart para crear particiones...\r\n");

    // Execute diskpart with the script and capture output
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
    if (!CreateProcessA(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(tempFile);
        return false;
    }

    CloseHandle(hWrite);

    // Read the output
    std::string output;
    char        buffer[1024];
    DWORD       bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    CloseHandle(hRead);

    // Wait for the process to finish
    WaitForSingleObject(pi.hProcess, 300000); // 5 minutes

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Wait a bit for the system to recognize the new partition
    Sleep(10000); // Increased to 10 seconds

    // Refresh volume information
    Utils::exec("mountvol /r");

    if (eventManager) {
        if (exitCode == 0) {
            eventManager->notifyLogUpdate("Diskpart ejecutado exitosamente. Verificando particiones...\r\n");
        } else {
            eventManager->notifyLogUpdate("Error: Diskpart falló con código de salida " + std::to_string(exitCode) +
                                          ".\r\n");
        }
    }

    // Write to log.txt
    std::ofstream logFile((logDir + "\\" + DISKPART_LOG_FILE).c_str());
    if (logFile) {
        logFile << "\xef\xbb\xbf"; // UTF-8 BOM
        logFile << "Diskpart script executed.\n";
        logFile << "Script content:\n";
        logFile << "select disk 0\n";
        logFile << "select volume C\n";
        logFile << "shrink desired=12000 minimum=12000\n";
        logFile << "create partition primary size=10000\n";
        logFile << "format fs=" << fsFormat << " quick label=\"" << VOLUME_LABEL << "\"\n";
        logFile << "create partition efi size=500\n";
        logFile << "format fs=fat32 quick label=\"" << EFI_VOLUME_LABEL << "\"\n";
        logFile << "exit\n";
        logFile << "\nExit code: " << exitCode << "\n";
        logFile << "\nDiskpart output:\n" << Utils::ansi_to_utf8(output) << "\n";
        logFile.close();
    }

    DeleteFileA(tempFile);

    if (exitCode == 0) {
        if (eventManager)
            eventManager->notifyLogUpdate("Particiones creadas exitosamente.\r\n");
        return true;
    } else {
        if (eventManager)
            eventManager->notifyLogUpdate("Error: Falló la creación de particiones.\r\n");
        return false;
    }
}

bool PartitionCreator::verifyPartitionsCreated() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    // Quick check if partition is now detectable
    bool   partitionFound = false;
    char   volNameCheck[MAX_PATH];
    HANDLE hVolCheck = FindFirstVolumeA(volNameCheck, sizeof(volNameCheck));
    if (hVolCheck != INVALID_HANDLE_VALUE) {
        do {
            size_t len = strlen(volNameCheck);
            if (len > 0 && volNameCheck[len - 1] == '\\') {
                volNameCheck[len - 1] = '\0';
            }

            char        volLabel[MAX_PATH] = {0};
            char        fsName[MAX_PATH]   = {0};
            DWORD       serial, maxComp, flags;
            std::string volPath = std::string(volNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volLabel, sizeof(volLabel), &serial, &maxComp, &flags, fsName,
                                      sizeof(fsName))) {
                if (_stricmp(volLabel, VOLUME_LABEL) == 0) {
                    partitionFound = true;
                    break;
                }
            }
        } while (FindNextVolumeA(hVolCheck, volNameCheck, sizeof(volNameCheck)));
        FindVolumeClose(hVolCheck);
    }

    // Log partition detection result
    std::ofstream logFile((logDir + "\\" + DISKPART_LOG_FILE).c_str(), std::ios::app);
    if (logFile) {
        logFile << "\nPartition detectable after creation: " << (partitionFound ? "YES" : "NO") << "\n";
        logFile.close();
    }

    return partitionFound;
}
