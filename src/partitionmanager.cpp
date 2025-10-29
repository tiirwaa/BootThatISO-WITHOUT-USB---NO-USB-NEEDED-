#include "partitionmanager.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>

PartitionManager::PartitionManager()
{
}

PartitionManager::~PartitionManager()
{
}

SpaceValidationResult PartitionManager::validateAvailableSpace()
{
    long long availableGB = getAvailableSpaceGB();
    
    SpaceValidationResult result;
    result.availableGB = availableGB;
    result.isValid = availableGB >= 10;
    
    if (!result.isValid) {
        std::ostringstream oss;
        oss << "No hay suficiente espacio disponible. Se requieren al menos 10 GB, pero solo hay " << availableGB << " GB disponibles.";
        result.errorMessage = oss.str();
    }
    
    return result;
}

long long PartitionManager::getAvailableSpaceGB()
{
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExA("C:\\", &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return freeBytesAvailable.QuadPart / (1024LL * 1024 * 1024);
    }
    return 0;
}

bool PartitionManager::createPartition()
{
    // Create a temporary script file for diskpart
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "diskpart", 0, tempFile);

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        return false;
    }

    scriptFile << "select disk 0\n";
    scriptFile << "select volume C\n";
    scriptFile << "shrink desired=10240 minimum=10240\n";
    scriptFile << "create partition primary size=10240\n";
    scriptFile << "assign letter=Z\n";
    scriptFile << "format fs=ntfs quick label=\"EasyISOBoot\"\n";
    scriptFile << "exit\n";
    scriptFile.close();

    // Execute diskpart with the script
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::string cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DeleteFileA(tempFile);
        return false;
    }

    // Wait for the process to finish
    WaitForSingleObject(pi.hProcess, 300000); // 5 minutes

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Write to log.txt
    std::ofstream logFile("log.txt");
    if (logFile) {
        logFile << "Diskpart script executed.\n";
        logFile << "Script content:\n";
        logFile << "select disk 0\n";
        logFile << "select volume C\n";
        logFile << "shrink desired=10240 minimum=10240\n";
        logFile << "create partition primary size=10240\n";
        logFile << "assign letter=Z\n";
        logFile << "format fs=ntfs quick label=\"EasyISOBoot\"\n";
        logFile << "exit\n";
        logFile << "\nExit code: " << exitCode << "\n";
        logFile.close();
    }

    DeleteFileA(tempFile);

    return exitCode == 0;
}

bool PartitionManager::partitionExists()
{
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    char* drive = drives;
    while (*drive) {
        if (GetDriveTypeA(drive) == DRIVE_FIXED) {
            char volumeName[MAX_PATH];
            char fileSystem[MAX_PATH];
            DWORD serialNumber, maxComponentLen, fileSystemFlags;
            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen, &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                if (strcmp(volumeName, "EasyISOBoot") == 0) {
                    return true;
                }
            }
        }
        drive += strlen(drive) + 1;
    }
    return false;
}