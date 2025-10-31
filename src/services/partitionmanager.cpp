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
std::string normalizeDriveRoot(const std::string& drive) {
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
    char buffer[MAX_PATH] = {0};
    DWORD length = GetEnvironmentVariableA("SystemDrive", buffer, MAX_PATH);
    if (length >= 2 && buffer[1] == ':') {
        return normalizeDriveRoot(std::string(buffer, length));
    }
    char windowsDir[MAX_PATH] = {0};
    UINT written = GetWindowsDirectoryA(windowsDir, MAX_PATH);
    if (written >= 2 && windowsDir[1] == ':') {
        return normalizeDriveRoot(std::string(windowsDir, windowsDir + 2));
    }
    return "C:\\";
}
}
// Helper to append and flush to general_log.log
void logToGeneral(const std::string& msg) {
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::string logDir = Utils::getExeDirectory() + "logs";
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

PartitionManager& PartitionManager::getInstance() {
    static PartitionManager instance;
    return instance;
}

PartitionManager::PartitionManager()
    : eventManager(nullptr),
      monitoredDrive(detectSystemDrive())
{
}
void PartitionManager::setMonitoredDrive(const std::string& driveRoot)
{
    std::string normalized = normalizeDriveRoot(driveRoot);
    if (normalized.empty()) {
        normalized = detectSystemDrive();
    }
    monitoredDrive = normalized;
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
    
    // Log to file
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\space_validation.log").c_str());
    if (logFile) {
        logFile << "Available space: " << availableGB << " GB\n";
        logFile << "Is valid: " << (result.isValid ? "yes" : "no") << "\n";
        if (!result.isValid) {
            std::ostringstream oss;
            oss << "No hay suficiente espacio disponible. Se requieren al menos 10 GB, pero solo hay " << availableGB << " GB disponibles.";
            result.errorMessage = oss.str();
            logFile << "Error: " << result.errorMessage << "\n";
        }
        logFile.close();
    }
    
    return result;
}

long long PartitionManager::getAvailableSpaceGB(const std::string& driveRoot)
{
    std::string target = driveRoot.empty() ? monitoredDrive : normalizeDriveRoot(driveRoot);
    if (target.empty()) {
        target = detectSystemDrive();
        monitoredDrive = target;
    }

    ULARGE_INTEGER freeBytesAvailable{}, totalNumberOfBytes{}, totalNumberOfFreeBytes{};
    if (GetDiskFreeSpaceExA(target.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return static_cast<long long>(freeBytesAvailable.QuadPart / (1024LL * 1024 * 1024));
    }
    return 0;
}

bool PartitionManager::createPartition(const std::string& format, bool skipIntegrityCheck)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);

    // Step 1: Perform disk integrity check (unless skipped)
    if (!skipIntegrityCheck) {
        if (!performDiskIntegrityCheck()) {
            return false;
        }
    }

    // Step 2: Verify disk is GPT
    if (!performGptCheck()) {
        return false;
    }

    // Step 3: Recover space for partitions
    if (!performSpaceRecovery()) {
        return false;
    }

    // Step 4: Create partitions using diskpart
    if (!performDiskpartOperations(format)) {
        return false;
    }

    // Step 5: Verify partitions were created successfully
    if (!verifyPartitionsCreated()) {
        if (eventManager) eventManager->notifyLogUpdate("Advertencia: No se pudo verificar la creación de particiones, pero diskpart reportó éxito.\r\n");
    }

    return true;
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

bool PartitionManager::efiPartitionExists()
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
                if (_stricmp(volumeName, EFI_VOLUME_LABEL) == 0) {
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
                if (_stricmp(volName, EFI_VOLUME_LABEL) == 0) {
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
    debugLog << "Searching for BOOTTHATISO partition...\n";

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

    // Parse output to find volume number for BOOTTHATISO
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
    scriptFile << "$volumes = Get-Volume | Where-Object { $_.FileSystemLabel -eq '" << VOLUME_LABEL << "' -or $_.FileSystemLabel -eq '" << EFI_VOLUME_LABEL << "' }\n";
    scriptFile << "foreach ($vol in $volumes) {\n";
    scriptFile << "    $part = Get-Partition | Where-Object { $_.AccessPaths -contains $vol.Path }\n";
    scriptFile << "    if ($part) {\n";
    scriptFile << "        Remove-PartitionAccessPath -DiskNumber 0 -PartitionNumber $part.PartitionNumber -AccessPath $vol.Path -Confirm:$false\n";
    scriptFile << "        Remove-Partition -DiskNumber 0 -PartitionNumber $part.PartitionNumber -Confirm:$false\n";
    scriptFile << "    }\n";
    scriptFile << "}\n";
    scriptFile << "$systemPartition = Get-Partition | Where-Object { $_.DriveLetter -eq 'C' }\n";
    scriptFile << "if ($systemPartition) {\n";
    scriptFile << "    $supportedSize = Get-PartitionSupportedSize -DiskNumber 0 -PartitionNumber $systemPartition.PartitionNumber\n";
    scriptFile << "    Resize-Partition -DiskNumber 0 -PartitionNumber $systemPartition.PartitionNumber -Size $supportedSize.SizeMax -Confirm:$false\n";
    scriptFile << "}\n";
    scriptFile.close();

    // Log the script content
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logScriptFile((logDir + "\\recover_script_log.txt").c_str());
    if (logScriptFile) {
        std::ifstream readScript(psFile);
        std::string scriptContent((std::istreambuf_iterator<char>(readScript)), std::istreambuf_iterator<char>());
        logScriptFile << scriptContent;
        logScriptFile.close();
    }
    if (eventManager) eventManager->notifyLogUpdate("Script de recuperación creado.\r\n");

    // Execute PowerShell script
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    HANDLE hRead, hWrite;
    std::string output;
    char buffer[1024];
    DWORD bytesRead;
    DWORD exitCode;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DeleteFileA(psFile.c_str());
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo crear pipe para recuperación.\r\n");
        return false;
    }

    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    std::string cmd = "powershell -ExecutionPolicy Bypass -File \"" + psFile + "\"";
    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        DeleteFileA(psFile.c_str());
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo ejecutar PowerShell para recuperación.\r\n");
        return false;
    }

    CloseHandle(hWrite);

    if (eventManager) {
        eventManager->notifyLogUpdate("Ejecutando script de PowerShell para recuperar espacio...\r\n");
    }

    output.clear();
    std::string pendingLine;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        std::string chunk(buffer, bytesRead);
        output += chunk;

        if (eventManager) {
            pendingLine += chunk;
            std::size_t newlinePos;
            while ((newlinePos = pendingLine.find('\n')) != std::string::npos) {
                std::string line = pendingLine.substr(0, newlinePos + 1);
                std::string utf8Line = Utils::ansi_to_utf8(line);
                if (!utf8Line.empty()) {
                    eventManager->notifyLogUpdate(utf8Line);
                }
                pendingLine.erase(0, newlinePos + 1);
            }
        }
    }

    if (eventManager && !pendingLine.empty()) {
        std::string utf8Remainder = Utils::ansi_to_utf8(pendingLine);
        if (!utf8Remainder.empty()) {
            eventManager->notifyLogUpdate(utf8Remainder);
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
        if (eventManager) eventManager->notifyLogUpdate("Espacio recuperado exitosamente.\r\n");
        return true;
    } else {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló la recuperación de espacio (código " + std::to_string(exitCode) + ").\r\n");
        return false;
    }
}

bool PartitionManager::RestartComputer()
{
    if (eventManager) eventManager->notifyLogUpdate("Intentando reiniciar el sistema...\r\n");
    
    // Enable shutdown privilege
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo abrir el token del proceso.\r\n");
        return false;
    }
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0)) {
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo ajustar los privilegios.\r\n");
        CloseHandle(hToken);
        return false;
    }
    if (GetLastError() != ERROR_SUCCESS) {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló la verificación de privilegios.\r\n");
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);
    
    // Attempt to restart the computer
    if (ExitWindowsEx(EWX_REBOOT, 0)) {
        return true;
    } else {
        DWORD error = GetLastError();
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo reiniciar el sistema. Código de error: " + std::to_string(error) + "\r\n");
        return false;
    }
}

bool PartitionManager::isDiskGpt()
{
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char tempFile[MAX_PATH];
    GetTempFileNameA(tempPath, "listdisk", 0, tempFile);

    std::ofstream scriptFile(tempFile);
    if (!scriptFile) {
        return false;
    }
    scriptFile << "list disk\n";
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
        return false;
    }

    // Log the output
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\diskpart_list_disk.log").c_str());
    if (logFile) {
        logFile << "Diskpart list disk output:\n" << Utils::ansi_to_utf8(output) << "\n";
        logFile << "Exit code: " << exitCode << "\n";
        logFile.close();
    }

    // Parse output to check if disk 0 is GPT
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("Disco 0") != std::string::npos || line.find("Disk 0") != std::string::npos) {
            if (line.find("*") != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}


bool PartitionManager::performDiskIntegrityCheck()
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    if (eventManager) eventManager->notifyLogUpdate("Verificando integridad del disco...\r\n");

    // Run chkdsk C: to check for errors
    STARTUPINFOA si_chk = { sizeof(si_chk) };
    PROCESS_INFORMATION pi_chk;
    SECURITY_ATTRIBUTES sa_chk;
    sa_chk.nLength = sizeof(sa_chk);
    sa_chk.lpSecurityDescriptor = NULL;
    sa_chk.bInheritHandle = TRUE;

    HANDLE hRead_chk, hWrite_chk;
    if (!CreatePipe(&hRead_chk, &hWrite_chk, &sa_chk, 0)) {
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo crear pipe para chkdsk.\r\n");
        return false;
    }

    si_chk.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si_chk.hStdOutput = hWrite_chk;
    si_chk.hStdError = hWrite_chk;
    si_chk.wShowWindow = SW_HIDE;

    std::string cmd_chk = "chkdsk C:";
    if (!CreateProcessA(NULL, const_cast<char*>(cmd_chk.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si_chk, &pi_chk)) {
        CloseHandle(hRead_chk);
        CloseHandle(hWrite_chk);
        if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo ejecutar chkdsk.\r\n");
        return false;
    }

    CloseHandle(hWrite_chk);

    std::string output_chk;
    char buffer_chk[1024];
    DWORD bytesRead_chk;
    while (ReadFile(hRead_chk, buffer_chk, sizeof(buffer_chk) - 1, &bytesRead_chk, NULL) && bytesRead_chk > 0) {
        buffer_chk[bytesRead_chk] = '\0';
        output_chk += buffer_chk;
    }

    CloseHandle(hRead_chk);

    WaitForSingleObject(pi_chk.hProcess, 300000); // 5 minutes

    DWORD exitCode_chk;
    GetExitCodeProcess(pi_chk.hProcess, &exitCode_chk);

    CloseHandle(pi_chk.hProcess);
    CloseHandle(pi_chk.hThread);

    // Log the chkdsk output
    std::ofstream chkLog((logDir + "\\" + CHKDSK_LOG_FILE).c_str());
    if (chkLog) {
        chkLog << "Chkdsk exit code: " << exitCode_chk << "\n";
        chkLog << "Output:\n" << Utils::ansi_to_utf8(output_chk) << "\n";
        chkLog.close();
    }

    // Show chkdsk result in UI log
    if (eventManager) eventManager->notifyLogUpdate("Resultado de verificación de disco:\r\n" + Utils::ansi_to_utf8(output_chk) + "\r\n");

    // If errors found, ask user if they want to repair
    if (exitCode_chk != 0) {
        std::wstring repairPrompt = LocalizedOrW("message.diskErrorsFoundPrompt", L"Se encontraron errores en el disco C:. ?Desea reparar el disco y reiniciar el sistema?");
        std::wstring repairTitle = LocalizedOrW("title.repairDisk", L"Reparar disco");
        int result = MessageBoxW(NULL, repairPrompt.c_str(), repairTitle.c_str(), MB_YESNO | MB_ICONQUESTION);
        if (result != IDYES) {
            return false;
        }

        if (eventManager) eventManager->notifyLogUpdate("Ejecutando chkdsk /f para reparar errores...\r\n");

        // Run chkdsk /f
        STARTUPINFOA si_f = { sizeof(si_f) };
        PROCESS_INFORMATION pi_f;
        SECURITY_ATTRIBUTES sa_f;
        sa_f.nLength = sizeof(sa_f);
        sa_f.lpSecurityDescriptor = NULL;
        sa_f.bInheritHandle = TRUE;

        HANDLE hRead_f, hWrite_f;
        HANDLE hRead_stdin, hWrite_stdin;
        if (!CreatePipe(&hRead_stdin, &hWrite_stdin, &sa_f, 0)) {
            if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo crear pipe para stdin.\r\n");
            return false;
        }
        if (!CreatePipe(&hRead_f, &hWrite_f, &sa_f, 0)) {
            CloseHandle(hRead_stdin);
            CloseHandle(hWrite_stdin);
            if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo crear pipe para chkdsk /f.\r\n");
            return false;
        }

        si_f.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si_f.hStdInput = hRead_stdin;
        si_f.hStdOutput = hWrite_f;
        si_f.hStdError = hWrite_f;
        si_f.wShowWindow = SW_HIDE;

        std::string cmd_f = "chkdsk C: /f";
        if (!CreateProcessA(NULL, const_cast<char*>(cmd_f.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si_f, &pi_f)) {
            CloseHandle(hRead_f);
            CloseHandle(hWrite_f);
            CloseHandle(hRead_stdin);
            CloseHandle(hWrite_stdin);
            if (eventManager) eventManager->notifyLogUpdate("Error: No se pudo ejecutar chkdsk /f.\r\n");
            return false;
        }

        // Send 'S' to stdin
        DWORD bytesWritten;
        bool inputSent = false;

        std::string output_f;
        char buffer_f[1024];
        DWORD bytesRead_f;

        // Read output in real-time
        while (true) {
            DWORD bytesAvailable = 0;
            if (!PeekNamedPipe(hRead_f, NULL, 0, NULL, &bytesAvailable, NULL)) {
                break;
            }
            if (bytesAvailable > 0) {
                if (ReadFile(hRead_f, buffer_f, min(sizeof(buffer_f) - 1, bytesAvailable), &bytesRead_f, NULL) && bytesRead_f > 0) {
                    buffer_f[bytesRead_f] = '\0';
                    output_f += buffer_f;
                    // Notify partial output
                    if (eventManager) eventManager->notifyLogUpdate(buffer_f);
                    // Check if prompt is present and send input
                    if (!inputSent && output_f.find("(S/N)") != std::string::npos) {
                        if (eventManager) eventManager->notifyLogUpdate("Enviando respuesta 'S' a chkdsk...\r\n");
                        WriteFile(hWrite_stdin, "S\r\n", 3, &bytesWritten, NULL);
                        CloseHandle(hWrite_stdin);
                        inputSent = true;
                        Sleep(2000); // Give time for chkdsk to process the input
                    }
                }
            }

            DWORD waitResult = WaitForSingleObject(pi_f.hProcess, 100); // 100ms timeout
            if (waitResult == WAIT_OBJECT_0) {
                break;
            }
        }

        // Tracing: loop ended
        const std::string loopEndMsg = "Bucle de lectura en tiempo real terminado. Esperando fin del proceso chkdsk /f...\r\n";
        if (eventManager) eventManager->notifyLogUpdate(loopEndMsg);
        logToGeneral(loopEndMsg);

        DWORD exitCode_f;
        GetExitCodeProcess(pi_f.hProcess, &exitCode_f);

        // Tracing: process ended
        const std::string processEndMsg = "Chkdsk /f terminó con código: " + std::to_string(exitCode_f) + "\r\n";
        if (eventManager) eventManager->notifyLogUpdate(processEndMsg);
        logToGeneral(processEndMsg);

        CloseHandle(pi_f.hProcess);
        CloseHandle(pi_f.hThread);

        // Tracing: final read
        const std::string finalReadMsg = "Leyendo salida final...\r\n";
        if (eventManager) eventManager->notifyLogUpdate(finalReadMsg);
        logToGeneral(finalReadMsg);

        // Force final read of any remaining output after process ends
        while (ReadFile(hRead_f, buffer_f, sizeof(buffer_f) - 1, &bytesRead_f, NULL) && bytesRead_f > 0) {
            buffer_f[bytesRead_f] = '\0';
            output_f += buffer_f;
            if (eventManager) eventManager->notifyLogUpdate(buffer_f);
        }

        const std::string finalReadDoneMsg = "Lectura final completada.\r\n";
        if (eventManager) eventManager->notifyLogUpdate(finalReadDoneMsg);
        logToGeneral(finalReadDoneMsg);

        CloseHandle(hRead_f);
        CloseHandle(hWrite_f);
        CloseHandle(hRead_stdin);

        // Log the chkdsk /f output
        std::ofstream chkLogF((logDir + "\\" + CHKDSK_F_LOG_FILE).c_str());
        if (chkLogF) {
            chkLogF << "Chkdsk /f exit code: " << exitCode_f << "\n";
            chkLogF << "Output:\n" << Utils::ansi_to_utf8(output_f) << "\n";
            chkLogF.close();
        }

        // Show chkdsk /f result in UI log
        if (eventManager) {
            eventManager->notifyLogUpdate("Resultado de chkdsk /f:\r\n" + Utils::ansi_to_utf8(output_f) + "\r\n");
            Sleep(100); // Give UI time to flush
        }

        // FINAL scheduling detection and restart logic (always runs after process ends)
        std::string utf8_output_f = Utils::ansi_to_utf8(output_f);
        const std::string verifyMsg = "Verificando si se programó para el próximo reinicio...\r\n";
        logToGeneral(verifyMsg);
        if (eventManager) eventManager->notifyLogUpdate(verifyMsg);
        Sleep(100);

        std::string lower = output_f;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
        // Debug: log output details
        logToGeneral("output_f length: " + std::to_string(output_f.length()) + "\n");
        logToGeneral("lower substr: " + lower.substr(0, 200) + "\n"); // first 200 chars
        bool hasSeComprobar = lower.find("se comprobar") != std::string::npos;
        bool hasReiniciar = lower.find("reiniciar") != std::string::npos;
        logToGeneral("has 'se comprobar': " + std::string(hasSeComprobar ? "yes" : "no") + "\n");
        logToGeneral("has 'reiniciar': " + std::string(hasReiniciar ? "yes" : "no") + "\n");

        logToGeneral("Starting detection\n");
        bool scheduled = false;

        // Language-independent detection: check exit code 3 (scheduled for next boot)
        if (exitCode_f == 3) {
            scheduled = true;
            logToGeneral("Scheduled = true via exit code 3\n");
        }
        // Robust detection: check for key phrases in raw output
        if (hasSeComprobar && hasReiniciar) {
            scheduled = true;
            logToGeneral("Scheduled = true via robust check\n");
        } else {
            // Fallback to markers in UTF-8
            std::string lower_utf8 = utf8_output_f;
            std::transform(lower_utf8.begin(), lower_utf8.end(), lower_utf8.begin(), [](unsigned char c){ return std::tolower(c); });
            const char* markers[] = {
                "próxima vez",
                "proxima vez",
                "próximo arranque",
                "proximo arranque",
                "se comprobar",
                "se comprobará",
                "se comprobara",
                "este volumen",
                "(s/n)",
                "will be checked",
                "next boot",
                "restart"
            };
            for (const char* m : markers) {
                std::string mm = m;
                std::string mmLower = mm;
                std::transform(mmLower.begin(), mmLower.end(), mmLower.begin(), [](unsigned char c){ return std::tolower(c); });
                if (utf8_output_f.find(mm) != std::string::npos || lower_utf8.find(mmLower) != std::string::npos) {
                    scheduled = true;
                    logToGeneral("Scheduled = true via marker: " + std::string(m) + "\n");
                    break;
                }
            }
        }
        if (utf8_output_f.find("Este volumen se comprobará la próxima vez que se reinicie el sistema.") != std::string::npos ||
            lower.find("este volumen se comprobará la próxima vez que se reinicie el sistema.") != std::string::npos) {
            scheduled = true;
            logToGeneral("Scheduled = true via exact match\n");
        }

        logToGeneral("Final scheduled: " + std::string(scheduled ? "true" : "false") + "\n");

        if (scheduled) {
            const std::string scheduledMsg = "Chkdsk ha programado una verificación para el próximo reinicio. Intentando reiniciar el sistema...\r\n";
            logToGeneral(scheduledMsg);
            if (eventManager) eventManager->notifyLogUpdate(scheduledMsg);
            Sleep(1000); // small pause before attempting restart
            bool restarted = RestartComputer();
            if (!restarted) {
                DWORD lastErr = GetLastError();
                const std::string failMsg = "RestartComputer() falló; código de error: " + std::to_string(lastErr) + ". Intentando comando de emergencia 'shutdown /r /t 0'...\r\n";
                logToGeneral(failMsg);
                if (eventManager) eventManager->notifyLogUpdate(failMsg);
                int ret = system("shutdown /r /t 0");
                const std::string shutdownMsg = "Resultado comando 'shutdown': " + std::to_string(ret) + " (0=OK, otro=error o sin privilegios)\r\n";
                logToGeneral(shutdownMsg);
                if (eventManager) eventManager->notifyLogUpdate(shutdownMsg);
            }
            logToGeneral("Fin de proceso de verificación/reinicio.\r\n");
            return false;
        } else {
            const std::string notScheduledMsg = "No se detectó programación de chkdsk para el próximo reinicio. No se reiniciará automáticamente.\r\n";
            logToGeneral(notScheduledMsg);
            if (eventManager) eventManager->notifyLogUpdate(notScheduledMsg);
        }
        logToGeneral("Fin de proceso de verificación/reinicio.\r\n");
        if (eventManager) {
            eventManager->notifyLogUpdate("Fin de proceso de verificación/reinicio.\r\n");
            Sleep(100);
        }

        if (exitCode_f != 0) {
            if (eventManager) eventManager->notifyLogUpdate("Error: Chkdsk /f falló.\r\n");
            return false;
        }
    }
    return true;
}

bool PartitionManager::performGptCheck()
{
    if (!isDiskGpt()) {
        if (eventManager) eventManager->notifyLogUpdate("Error: El disco no es GPT. La aplicación requiere un disco GPT para crear particiones EFI.\r\n");
        return false;
    } else {
        if (eventManager) eventManager->notifyLogUpdate("Disco confirmado como GPT. Procediendo con la creación de particiones...\r\n");
    }
    return true;
}

bool PartitionManager::performSpaceRecovery()
{
    // Always recover space to ensure clean state
    if (eventManager) eventManager->notifyLogUpdate("Recuperando espacio para particiones...\r\n");
    if (!recoverSpace()) {
        if (eventManager) eventManager->notifyLogUpdate("Error: Falló la recuperación de espacio.\r\n");
        return false;
    }
    return true;
}

bool PartitionManager::performDiskpartOperations(const std::string& format)
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    if (eventManager) eventManager->notifyLogUpdate("Creando script de diskpart para particiones...\r\n");
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
    scriptFile << "shrink desired=12000 minimum=12000\n";
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

    // Write to log.txt
    std::ofstream logFile((logDir + "\\" + DISKPART_LOG_FILE).c_str());
    if (logFile) {
        logFile << "\xef\xbb\xbf"; // UTF-8 BOM
        logFile << "Diskpart script executed.\n";
        logFile << "Script content:\n";
        logFile << "select disk 0\n";
        logFile << "select volume C\n";
        logFile << "shrink desired=12000 minimum=12000\n";
        logFile << "create partition efi size=500\n";
        logFile << "format fs=fat32 quick label=\"" << EFI_VOLUME_LABEL << "\"\n";
        logFile << "create partition primary size=10000\n";
        logFile << "format fs=" << fsFormat << " quick label=\"" << VOLUME_LABEL << "\"\n";
        logFile << "exit\n";
        logFile << "\nExit code: " << exitCode << "\n";
        logFile << "\nDiskpart output:\n" << Utils::ansi_to_utf8(output) << "\n";
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

bool PartitionManager::verifyPartitionsCreated()
{
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
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

    // Log partition detection result
    std::ofstream logFile((logDir + "\\" + DISKPART_LOG_FILE).c_str(), std::ios::app);
    if (logFile) {
        logFile << "\nPartition detectable after creation: " << (partitionFound ? "YES" : "NO") << "\n";
        logFile.close();
    }

    return partitionFound;
}



