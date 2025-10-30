#include "partitionmanager.h"
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdlib>

PartitionManager& PartitionManager::getInstance() {
    static PartitionManager instance;
    return instance;
}

PartitionManager::PartitionManager()
    : eventManager(nullptr)
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

bool PartitionManager::createPartition(const std::string& format)
{
    if (eventManager) eventManager->notifyLogUpdate("Creando script de diskpart para particiones...\r\n");

    // Create a temporary script file for diskpart
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "diskpart", 0, tempFile);

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo crear el archivo de script de diskpart.\r\n");
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
    scriptFile << "shrink desired=10500 minimum=10500\n";
    scriptFile << "create partition efi size=500\n";
    scriptFile << "format fs=fat32 quick label=\"" << EFI_VOLUME_LABEL << "\"\n";
    scriptFile << "create partition primary size=10000\n";
    scriptFile << "format fs=" << fsFormat << " quick label=\"" << VOLUME_LABEL << "\"\n";
    scriptFile << "exit\n";
    scriptFile.close();

    if (eventManager) eventManager->notifyLogUpdate("Ejecutando diskpart para crear particiones...\r\n");

    // Execute diskpart with the script and capture output
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(tempFile);
        return false;
    }

    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(tempFile);
        return false;
    }

    CloseHandle(hWrite);

    // Read the output
    std::string output;
    char buffer[1024];
    DWORD bytesRead;
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
    system("mountvol /r >nul 2>&1");

    if (eventManager) {
        if (exitCode == 0) {
            eventManager->notifyLogUpdate("Diskpart ejecutado exitosamente. Verificando particiones...\r\n");
        } else {
            eventManager->notifyLogUpdate("Error: Diskpart falló con código de salida " + std::to_string(exitCode) + ".\r\n");
        }
    }

    // Quick check if partition is now detectable
    bool partitionFound = false;
    char volNameCheck[MAX_PATH];
    HANDLE hVolCheck = FindFirstVolumeA(volNameCheck, sizeof(volNameCheck));
    if (hVolCheck != INVALID_HANDLE_VALUE) {
        do {
            size_t len = strlen(volNameCheck);
            if (len > 0 && volNameCheck[len - 1] == '\\') {
                volNameCheck[len - 1] = '\0';
            }
            
            char volLabel[MAX_PATH] = {0};
            char fsName[MAX_PATH] = {0};
            DWORD serial, maxComp, flags;
            std::string volPath = std::string(volNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volLabel, sizeof(volLabel), &serial, &maxComp, &flags, fsName, sizeof(fsName))) {
                if (_stricmp(volLabel, VOLUME_LABEL) == 0) {
                    partitionFound = true;
                    break;
                }
            }
        } while (FindNextVolumeA(hVolCheck, volNameCheck, sizeof(volNameCheck)));
        FindVolumeClose(hVolCheck);
    }

    // Write to log.txt
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\" + DISKPART_LOG_FILE).c_str());
    if (logFile) {
        logFile << "\xef\xbb\xbf"; // UTF-8 BOM
        logFile << "Diskpart script executed.\n";
        logFile << "Script content:\n";
        logFile << "select disk 0\n";
        logFile << "select volume C\n";
        logFile << "shrink desired=10500 minimum=10500\n";
        logFile << "create partition efi size=500\n";
        logFile << "format fs=fat32 quick label=\"" << EFI_VOLUME_LABEL << "\"\n";
        logFile << "create partition primary size=10000\n";
        logFile << "format fs=" << fsFormat << " quick label=\"" << VOLUME_LABEL << "\"\n";
        logFile << "exit\n";
        logFile << "\nExit code: " << exitCode << "\n";
        logFile << "\nDiskpart output:\n" << Utils::ansi_to_utf8(output) << "\n";
        logFile << "\nPartition detectable after creation: " << (partitionFound ? "YES" : "NO") << "\n";
        logFile.close();
    }

    DeleteFileA(tempFile);

    if (exitCode == 0) {
        if (eventManager) eventManager->notifyLogUpdate("Particiones creadas exitosamente.\r\n");
        return true;
    } else {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló la creación de particiones.\r\n");
        return false;
    }
}

bool PartitionManager::partitionExists()
{
    // First check drives with assigned letters
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    char* drive = drives;
    while (*drive) {
        if (GetDriveTypeA(drive) == DRIVE_FIXED) {
            char volumeName[MAX_PATH];
            char fileSystem[MAX_PATH];
            DWORD serialNumber, maxComponentLen, fileSystemFlags;
            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen, &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                if (_stricmp(volumeName, VOLUME_LABEL) == 0) {
                    return true;
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    // Also check unassigned volumes
    char volumeNameCheck[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeNameCheck, sizeof(volumeNameCheck));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeNameCheck);
            if (len > 0 && volumeNameCheck[len - 1] == '\\') {
                volumeNameCheck[len - 1] = '\0';
            }
            
            // Get volume information
            char volName[MAX_PATH] = {0};
            char fsName[MAX_PATH] = {0};
            DWORD serial, maxComp, flags;
            std::string volPath = std::string(volumeNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName, sizeof(fsName))) {
                if (_stricmp(volName, VOLUME_LABEL) == 0) {
                    FindVolumeClose(hVolume);
                    return true;
                }
            }
        } while (FindNextVolumeA(hVolume, volumeNameCheck, sizeof(volumeNameCheck)));
        FindVolumeClose(hVolume);
    }

    return false;
}

std::string PartitionManager::getPartitionDriveLetter()
{
    // First try to find existing drive letter
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\" + DEBUG_DRIVES_LOG_FILE).c_str());
    debugLog << "Searching for EASYISOBOOT partition...\n";

    char* drive = drives;
    while (*drive) {
        UINT driveType = GetDriveTypeA(drive);
        debugLog << "Checking drive: " << drive << " (type: " << driveType << ")\n";
        if (driveType == DRIVE_FIXED) {
            char volumeName[MAX_PATH] = {0};
            char fileSystem[MAX_PATH] = {0};
            DWORD serialNumber, maxComponentLen, fileSystemFlags;
            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen, &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                debugLog << "  Volume name: '" << volumeName << "', File system: '" << fileSystem << "'\n";
                if (_stricmp(volumeName, VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << VOLUME_LABEL << " partition at: " << drive << "\n";
                    debugLog.close();
                    return std::string(drive);
                }
            } else {
                debugLog << "  GetVolumeInformation failed for drive: " << drive << ", error: " << GetLastError() << "\n";
            }
        } else {
            debugLog << "  Drive type " << driveType << " (not DRIVE_FIXED)\n";
        }
        drive += strlen(drive) + 1;
    }

    // If not found, try to find unassigned volumes and assign a drive letter
    debugLog << "Partition not found with drive letter, searching for unassigned volumes...\n";
    
    char volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            debugLog << "Checking volume: " << volumeName << "\n";
            
            // Store volume name with trailing backslash for SetVolumeMountPointA
            std::string volumeNameWithSlash = volumeName;
            
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }
            
            // Get volume information
            char volName[MAX_PATH] = {0};
            char fsName[MAX_PATH] = {0};
            DWORD serial, maxComp, flags;
            std::string volPath = std::string(volumeName) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName, sizeof(fsName))) {
                debugLog << "  Volume label: '" << volName << "', FS: '" << fsName << "'\n";
                if (_stricmp(volName, VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << VOLUME_LABEL << " volume: " << volumeName << "\n";
                    
                    // Try to assign a drive letter to this volume
                    // Start from Z: and go backwards to avoid conflicts with common drive letters
                    for (char letter = 'Z'; letter >= 'D'; letter--) {
                        std::string driveLetter = std::string(1, letter) + ":";
                        
                        // Check if drive letter is available
                        UINT driveType = GetDriveTypeA(driveLetter.c_str());
                        debugLog << "  Checking drive letter " << driveLetter << ", type: " << driveType << "\n";
                        
                        if (driveType == DRIVE_NO_ROOT_DIR) {
                            debugLog << "  Trying to assign drive letter " << driveLetter << "\n";
                            
                            std::string mountPoint = driveLetter + "\\";
                            // Use volume name WITH trailing backslash for SetVolumeMountPointA
                            if (SetVolumeMountPointA(mountPoint.c_str(), volumeNameWithSlash.c_str())) {
                                debugLog << "  Successfully assigned drive letter " << driveLetter << "\n";
                                FindVolumeClose(hVolume);
                                debugLog.close();
                                return driveLetter + "\\";
                            } else {
                                DWORD error = GetLastError();
                                debugLog << "  Failed to assign drive letter " << driveLetter << ", error: " << error << "\n";
                                
                                // If the error is ERROR_DIR_NOT_EMPTY, the letter might be in use
                                // Try the next letter
                                if (error != ERROR_DIR_NOT_EMPTY) {
                                    debugLog << "  Non-recoverable error, stopping drive letter assignment\n";
                                    break;
                                }
                            }
                        } else {
                            debugLog << "  Drive letter " << driveLetter << " not available (type: " << driveType << ")\n";
                        }
                    }
                    
                    debugLog << "  Could not assign any drive letter\n";
                    FindVolumeClose(hVolume);
                    debugLog.close();
                    return "";
                }
            } else {
                debugLog << "  GetVolumeInformation failed for volume " << volumeName << ", error: " << GetLastError() << "\n";
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));
        FindVolumeClose(hVolume);
    } else {
        debugLog << "FindFirstVolumeA failed, error: " << GetLastError() << "\n";
    }
    
    debugLog << VOLUME_LABEL << " partition not found.\n";
    debugLog << "Available drives found:\n";
    
    // List all drives for debugging
    char allDrives[256];
    GetLogicalDriveStringsA(sizeof(allDrives), allDrives);
    char* d = allDrives;
    while (*d) {
        UINT dt = GetDriveTypeA(d);
        debugLog << "  " << d << " (type: " << dt << ")\n";
        d += strlen(d) + 1;
    }
    
    debugLog.close();
    return "";
}

std::string PartitionManager::getEfiPartitionDriveLetter()
{
    // Similar to getPartitionDriveLetter but for EFI_VOLUME_LABEL
    char drives[256];
    GetLogicalDriveStringsA(sizeof(drives), drives);

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream debugLog((logDir + "\\" + DEBUG_DRIVES_EFI_LOG_FILE).c_str());
    debugLog << "Searching for " << EFI_VOLUME_LABEL << " partition...\n";

    char* drive = drives;
    while (*drive) {
        UINT driveType = GetDriveTypeA(drive);
        debugLog << "Checking drive: " << drive << " (type: " << driveType << ")\n";
        if (driveType == DRIVE_FIXED) {
            char volumeName[MAX_PATH] = {0};
            char fileSystem[MAX_PATH] = {0};
            DWORD serialNumber, maxComponentLen, fileSystemFlags;
            if (GetVolumeInformationA(drive, volumeName, sizeof(volumeName), &serialNumber, &maxComponentLen, &fileSystemFlags, fileSystem, sizeof(fileSystem))) {
                debugLog << "  Volume name: '" << volumeName << "', File system: '" << fileSystem << "'\n";
                if (_stricmp(volumeName, EFI_VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << EFI_VOLUME_LABEL << " partition at: " << drive << "\n";
                    debugLog.close();
                    return std::string(drive);
                }
            } else {
                debugLog << "  GetVolumeInformation failed for drive: " << drive << ", error: " << GetLastError() << "\n";
            }
        } else {
            debugLog << "  Drive type " << driveType << " (not DRIVE_FIXED)\n";
        }
        drive += strlen(drive) + 1;
    }

    // If not found, try to find unassigned volumes and assign a drive letter
    debugLog << "Partition not found with drive letter, searching for unassigned volumes...\n";
    
    char volumeName[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeName, sizeof(volumeName));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            debugLog << "Checking volume: " << volumeName << "\n";
            
            // Store volume name with trailing backslash for SetVolumeMountPointA
            std::string volumeNameWithSlash = volumeName;
            
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len - 1] == '\\') {
                volumeName[len - 1] = '\0';
            }
            
            // Get volume information
            char volName[MAX_PATH] = {0};
            char fsName[MAX_PATH] = {0};
            DWORD serial, maxComp, flags;
            std::string volPath = std::string(volumeName) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName, sizeof(fsName))) {
                debugLog << "  Volume label: '" << volName << "', FS: '" << fsName << "'\n";
                if (_stricmp(volName, EFI_VOLUME_LABEL) == 0) {
                    debugLog << "  Found " << EFI_VOLUME_LABEL << " volume: " << volumeName << "\n";
                    
                    // Try to assign a drive letter to this volume
                    for (char letter = 'Z'; letter >= 'D'; letter--) {
                        std::string driveLetter = std::string(1, letter) + ":";
                        
                        // Check if drive letter is available
                        UINT driveType = GetDriveTypeA(driveLetter.c_str());
                        debugLog << "  Checking drive letter " << driveLetter << ", type: " << driveType << "\n";
                        
                        if (driveType == DRIVE_NO_ROOT_DIR) {
                            debugLog << "  Trying to assign drive letter " << driveLetter << "\n";
                            
                            std::string mountPoint = driveLetter + "\\";
                            if (SetVolumeMountPointA(mountPoint.c_str(), volumeNameWithSlash.c_str())) {
                                debugLog << "  Successfully assigned drive letter " << driveLetter << "\n";
                                // Grant full permissions to ensure writability
                                std::string permCmd = "icacls \"" + driveLetter + "\" /grant Everyone:F /T /C";
                                system(permCmd.c_str());
                                FindVolumeClose(hVolume);
                                debugLog.close();
                                return driveLetter + "\\";
                            } else {
                                DWORD error = GetLastError();
                                debugLog << "  Failed to assign drive letter " << driveLetter << ", error: " << error << "\n";
                                
                                if (error != ERROR_DIR_NOT_EMPTY) {
                                    debugLog << "  Non-recoverable error, stopping drive letter assignment\n";
                                    break;
                                }
                            }
                        } else {
                            debugLog << "  Drive letter " << driveLetter << " not available (type: " << driveType << ")\n";
                        }
                    }
                    
                    debugLog << "  Could not assign any drive letter\n";
                    FindVolumeClose(hVolume);
                    debugLog.close();
                    return "";
                }
            } else {
                debugLog << "  GetVolumeInformation failed for volume " << volumeName << ", error: " << GetLastError() << "\n";
            }
        } while (FindNextVolumeA(hVolume, volumeName, sizeof(volumeName)));
        FindVolumeClose(hVolume);
    } else {
        debugLog << "FindFirstVolumeA failed, error: " << GetLastError() << "\n";
    }
    
    debugLog << EFI_VOLUME_LABEL << " partition not found.\n";
    debugLog.close();
    return "";
}

std::string PartitionManager::getPartitionFileSystem()
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
                if (_stricmp(volumeName, VOLUME_LABEL) == 0) {
                    return std::string(fileSystem);
                }
            }
        }
        drive += strlen(drive) + 1;
    }

    // Also check unassigned volumes
    char volumeNameCheck[MAX_PATH];
    HANDLE hVolume = FindFirstVolumeA(volumeNameCheck, sizeof(volumeNameCheck));
    if (hVolume != INVALID_HANDLE_VALUE) {
        do {
            // Remove trailing backslash for GetVolumeInformationA
            size_t len = strlen(volumeNameCheck);
            if (len > 0 && volumeNameCheck[len - 1] == '\\') {
                volumeNameCheck[len - 1] = '\0';
            }
            
            // Get volume information
            char volName[MAX_PATH] = {0};
            char fsName[MAX_PATH] = {0};
            DWORD serial, maxComp, flags;
            std::string volPath = std::string(volumeNameCheck) + "\\";
            if (GetVolumeInformationA(volPath.c_str(), volName, sizeof(volName), &serial, &maxComp, &flags, fsName, sizeof(fsName))) {
                if (_stricmp(volName, VOLUME_LABEL) == 0) {
                    FindVolumeClose(hVolume);
                    return std::string(fsName);
                }
            }
        } while (FindNextVolumeA(hVolume, volumeNameCheck, sizeof(volumeNameCheck)));
        FindVolumeClose(hVolume);
    }

    return "";
}

bool PartitionManager::reformatPartition(const std::string& format)
{
    if (eventManager) eventManager->notifyLogUpdate("Iniciando reformateo de partición...\r\n");

    std::string fsFormat;
    if (format == "EXFAT") {
        fsFormat = "exfat";
    } else if (format == "NTFS") {
        fsFormat = "ntfs";
    } else {
        fsFormat = "fat32";
    }

    if (eventManager) eventManager->notifyLogUpdate("Buscando volumen para reformatear...\r\n");

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

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(tempFile);
        return false;
    }

    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(tempFile);
        return false;
    }

    CloseHandle(hWrite);

    std::string output;
    char buffer[1024];
    DWORD bytesRead;
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

    // Parse output to find volume number for EASYISOBOOT
    std::istringstream iss(output);
    std::string line;
    int volumeNumber = -1;
    while (std::getline(iss, line)) {
        size_t volPos = line.find("Volumen");
        if (volPos == std::string::npos) {
            volPos = line.find("Volume");
        }
        if (volPos != std::string::npos) {
            // Parse volume number and label
            std::string numStr = line.substr(volPos + 8, 3);
            size_t spacePos = numStr.find(' ');
            if (spacePos != std::string::npos) {
                numStr = numStr.substr(0, spacePos);
            }
            int volNum = std::atoi(numStr.c_str());
            
            std::string label = line.substr(volPos + 15, 13);
            // Trim leading and trailing spaces
            size_t start = label.find_first_not_of(" \t");
            size_t end = label.find_last_not_of(" \t");
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
        if (eventManager) eventManager->notifyLogUpdate("Error: No se encontró el volumen con etiqueta " + std::string(VOLUME_LABEL) + ".\r\n");
        return false;
    }

    if (eventManager) eventManager->notifyLogUpdate("Volumen encontrado (número " + std::to_string(volumeNumber) + "). Creando script de formateo...\r\n");

    // Now, create script to select and format
    GetTempFileNameA(tempPath, "format", 0, tempFile);
    scriptFile.open(tempFile);
    scriptFile << "select volume " << volumeNumber << "\n";
    scriptFile << "format fs=" << fsFormat << " quick label=\"" << VOLUME_LABEL << "\"\n";
    scriptFile << "exit\n";
    scriptFile.close();

    // Execute the format script
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = NULL;
    si.hStdError = NULL;

    cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DeleteFileA(tempFile);
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo ejecutar diskpart para formateo.\r\n");
        return false;
    }

    if (eventManager) eventManager->notifyLogUpdate("Ejecutando formateo de partición...\r\n");

    WaitForSingleObject(pi.hProcess, 300000); // 5 minutes

    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(tempFile);

    // Wait a bit for the system to recognize the changes
    Sleep(5000);

    // Refresh volume information
    system("mountvol /r >nul 2>&1");

    std::ofstream logFile2((logDir + "\\" + REFORMAT_EXIT_LOG_FILE).c_str(), std::ios::app);
    if (logFile2) {
        logFile2 << "Format command exit code: " << exitCode << "\n";
        logFile2.close();
    }

    if (exitCode == 0) {
        if (eventManager) eventManager->notifyLogUpdate("Partición reformateada exitosamente.\r\n");
        return true;
    } else {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló el formateo de la partición (código " + std::to_string(exitCode) + ").\r\n");
        return false;
    }
}

bool PartitionManager::reformatEfiPartition()
{
    if (eventManager) eventManager->notifyLogUpdate("Iniciando reformateo de partición EFI...\r\n");

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

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(tempFile);
        return false;
    }

    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(tempFile);
        return false;
    }

    CloseHandle(hWrite);

    std::string output;
    char buffer[1024];
    DWORD bytesRead;
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
    std::string line;
    int volumeNumber = -1;
    while (std::getline(iss, line)) {
        size_t volPos = line.find("Volumen");
        if (volPos == std::string::npos) {
            volPos = line.find("Volume");
        }
        if (volPos != std::string::npos) {
            // Parse volume number and label
            std::string numStr = line.substr(volPos + 8, 3);
            size_t spacePos = numStr.find(' ');
            if (spacePos != std::string::npos) {
                numStr = numStr.substr(0, spacePos);
            }
            int volNum = std::atoi(numStr.c_str());
            
            std::string label = line.substr(volPos + 15, 13);
            // Trim leading and trailing spaces
            size_t start = label.find_first_not_of(" \t");
            size_t end = label.find_last_not_of(" \t");
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
        if (eventManager) eventManager->notifyLogUpdate("Error: No se encontró el volumen EFI con etiqueta " + std::string(EFI_VOLUME_LABEL) + ".\r\n");
        return false;
    }

    if (eventManager) eventManager->notifyLogUpdate("Volumen EFI encontrado (número " + std::to_string(volumeNumber) + "). Creando script de formateo...\r\n");

    // Now, create script to select and format EFI
    GetTempFileNameA(tempPath, "format_efi", 0, tempFile);
    scriptFile.open(tempFile);
    scriptFile << "select volume " << volumeNumber << "\n";
    scriptFile << "format fs=fat32 quick label=\"" << EFI_VOLUME_LABEL << "\"\n";
    scriptFile << "exit\n";
    scriptFile.close();

    // Execute the format script
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = NULL;
    si.hStdError = NULL;

    cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DeleteFileA(tempFile);
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo ejecutar diskpart para formateo EFI.\r\n");
        return false;
    }

    if (eventManager) eventManager->notifyLogUpdate("Ejecutando formateo de partición EFI...\r\n");

    WaitForSingleObject(pi.hProcess, 300000); // 5 minutes

    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(tempFile);

    // Wait a bit for the system to recognize the changes
    Sleep(5000);

    // Refresh volume information
    system("mountvol /r >nul 2>&1");

    std::ofstream logFile2((logDir + "\\" + REFORMAT_EXIT_LOG_FILE).c_str(), std::ios::app);
    if (logFile2) {
        logFile2 << "EFI Format command exit code: " << exitCode << "\n";
        logFile2.close();
    }

    if (exitCode == 0) {
        if (eventManager) eventManager->notifyLogUpdate("Partición EFI reformateada exitosamente.\r\n");
        return true;
    } else {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló el formateo de la partición EFI (código " + std::to_string(exitCode) + ").\r\n");
        return false;
    }
}

bool PartitionManager::recoverSpace()
{
    if (eventManager) eventManager->notifyLogUpdate("Iniciando recuperación de espacio...\r\n");

    // First, find volumes with VOLUME_LABEL and EFI_VOLUME_LABEL
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "recover", 0, tempFile);

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        return false;
    }
    scriptFile << "list volume\n";
    scriptFile << "exit\n";
    scriptFile.close();

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(tempFile);
        return false;
    }

    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(tempFile);
        return false;
    }

    CloseHandle(hWrite);

    std::string output;
    char buffer[1024];
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 30000);

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(tempFile);

    if (exitCode != 0) {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló list volume para recuperación.\r\n");
        return false;
    }

    // Parse to find volume numbers for VOLUME_LABEL and EFI_VOLUME_LABEL
    std::istringstream iss(output);
    std::string line;
    int dataVol = -1, efiVol = -1;
    while (std::getline(iss, line)) {
        size_t volPos = line.find("Volumen");
        if (volPos == std::string::npos) {
            volPos = line.find("Volume");
        }
        if (volPos != std::string::npos) {
            std::string numStr = line.substr(volPos + 8, 3);
            size_t spacePos = numStr.find(' ');
            if (spacePos != std::string::npos) {
                numStr = numStr.substr(0, spacePos);
            }
            int volNum = std::atoi(numStr.c_str());
            
            std::string label = line.substr(volPos + 15, 13);
            size_t start = label.find_first_not_of(" \t");
            size_t end = label.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                label = label.substr(start, end - start + 1);
            } else {
                label = "";
            }
            
            if (label == VOLUME_LABEL) {
                dataVol = volNum;
            } else if (label == EFI_VOLUME_LABEL) {
                efiVol = volNum;
            }
        }
    }

    if (dataVol == -1 && efiVol == -1) {
        if (eventManager) eventManager->notifyLogUpdate("No se encontraron particiones creadas por el programa.\r\n");
        return true; // Nothing to do
    }

    // Create script to delete volumes and extend C:
    GetTempFileNameA(tempPath, "recover_script", 0, tempFile);
    scriptFile.open(tempFile);
    scriptFile << "select disk 0\n";
    if (dataVol != -1) {
        scriptFile << "select volume " << dataVol << "\n";
        scriptFile << "delete volume\n";
    }
    if (efiVol != -1) {
        scriptFile << "select volume " << efiVol << "\n";
        scriptFile << "delete volume\n";
    }
    scriptFile << "select partition 1\n"; // Assume C: is partition 1
    scriptFile << "extend\n";
    scriptFile << "exit\n";
    scriptFile.close();

    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = NULL;
    si.hStdError = NULL;

    cmd = "diskpart /s " + std::string(tempFile);
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DeleteFileA(tempFile);
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo ejecutar diskpart para recuperación.\r\n");
        return false;
    }

    if (eventManager) eventManager->notifyLogUpdate("Ejecutando recuperación de espacio...\r\n");

    WaitForSingleObject(pi.hProcess, 300000); // 5 minutes

    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(tempFile);

    if (exitCode == 0) {
        if (eventManager) eventManager->notifyLogUpdate("Espacio recuperado exitosamente.\r\n");
        return true;
    } else {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló la recuperación de espacio (código " + std::to_string(exitCode) + ").\r\n");
        return false;
    }
}