#include "bcdmanager.h"
#include "constants.h"
#include <windows.h>
#include <winnt.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <fstream>

BCDManager::BCDManager()
{
}

BCDManager::~BCDManager()
{
}

std::string exec(const char* cmd) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "";
    }

    CloseHandle(hWrite);

    char buffer[128];
    std::string result = "";
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

WORD BCDManager::GetMachineType(const std::string& filePath) {
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMapping) {
        CloseHandle(hFile);
        return 0;
    }

    LPVOID lpBase = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!lpBase) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)lpBase;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        UnmapViewOfFile(lpBase);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((BYTE*)lpBase + dosHeader->e_lfanew);
    if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
        UnmapViewOfFile(lpBase);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    WORD machine = ntHeader->FileHeader.Machine;
    UnmapViewOfFile(lpBase);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return machine;
}

std::string BCDManager::configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, const std::string& mode)
{
    // Get volume GUID for data partition
    WCHAR dataVolumeName[MAX_PATH];
    std::wstring wDriveLetter = std::wstring(driveLetter.begin(), driveLetter.end()) + L"\\";
    if (!GetVolumeNameForVolumeMountPointW(wDriveLetter.c_str(), dataVolumeName, MAX_PATH)) {
        return "Error al obtener el nombre del volumen de datos";
    }
    char narrowDataVolumeName[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, dataVolumeName, -1, narrowDataVolumeName, sizeof(narrowDataVolumeName), NULL, NULL);
    std::string dataDevice = narrowDataVolumeName;

    // Get volume GUID for ESP
    WCHAR espVolumeName[MAX_PATH];
    std::wstring wEspDriveLetter = std::wstring(espDriveLetter.begin(), espDriveLetter.end()) + L"\\";
    if (!GetVolumeNameForVolumeMountPointW(wEspDriveLetter.c_str(), espVolumeName, MAX_PATH)) {
        return "Error al obtener el nombre del volumen ESP";
    }
    char narrowEspVolumeName[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, espVolumeName, -1, narrowEspVolumeName, sizeof(narrowEspVolumeName), NULL, NULL);
    std::string espDevice = narrowEspVolumeName;

    // Set default to current to avoid issues with deleting the default entry
    exec("bcdedit /default {current}");

    // Delete any existing ISOBOOT entries to avoid duplicates
    std::string enumOutput = exec("bcdedit /enum");
    auto lines = split(enumOutput, '\n');
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("description") != std::string::npos && lines[i].find("ISOBOOT") != std::string::npos) {
            if (i > 0 && lines[i-1].find("identifier") != std::string::npos) {
                size_t pos = lines[i-1].find("{");
                if (pos != std::string::npos) {
                    size_t end = lines[i-1].find("}", pos);
                    if (end != std::string::npos) {
                        std::string guid = lines[i-1].substr(pos, end - pos + 1);
                        std::string deleteCmd = "bcdedit /delete " + guid;
                        exec(deleteCmd.c_str());
                    }
                }
            }
        }
    }

    std::string bcdLabel = (mode == "RAMDISK") ? RAMDISK_BCD_LABEL : EXTRACTED_BCD_LABEL;
    std::string output = exec(("bcdedit /copy {default} /d \"" + std::string(bcdLabel) + "\"").c_str());
    if (output.find("error") != std::string::npos || output.find("{") == std::string::npos) return "Error al copiar entrada BCD";
    size_t pos = output.find("{");
    size_t end = output.find("}", pos);
    if (end == std::string::npos) return "Error al extraer GUID de la nueva entrada";
    std::string guid = output.substr(pos, end - pos + 1);

    // Find the EFI boot file in ESP
    std::string efiBootFile;
    std::string candidate5 = espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi";
    if (GetFileAttributesA(candidate5.c_str()) != INVALID_FILE_ATTRIBUTES) {
        efiBootFile = candidate5;
    } else {
        std::string candidate6 = espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
        if (GetFileAttributesA(candidate6.c_str()) != INVALID_FILE_ATTRIBUTES) {
            efiBootFile = candidate6;
        } else {
            std::string candidate3 = espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI";
            if (GetFileAttributesA(candidate3.c_str()) != INVALID_FILE_ATTRIBUTES) {
                efiBootFile = candidate3;
            } else {
                std::string candidate1 = espDriveLetter + "\\EFI\\boot\\bootx64.efi";
                if (GetFileAttributesA(candidate1.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    efiBootFile = candidate1;
                } else {
                    std::string candidate4 = espDriveLetter + "\\EFI\\boot\\BOOTIA32.EFI";
                    if (GetFileAttributesA(candidate4.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        efiBootFile = candidate4;
                    } else {
                        std::string candidate2 = espDriveLetter + "\\EFI\\boot\\bootia32.efi";
                        if (GetFileAttributesA(candidate2.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            efiBootFile = candidate2;
                        } else {
                            return "Archivo EFI boot no encontrado en ESP";
                        }
                    }
                }
            }
        }
    }

    WORD machine = GetMachineType(efiBootFile);
    if (machine != IMAGE_FILE_MACHINE_AMD64 && machine != IMAGE_FILE_MACHINE_I386) {
        return "Arquitectura EFI no soportada";
    }

    // Log selected file and mode
    std::ofstream logFile("bcd_config_log.txt");
    logFile << "Selected EFI boot file: " << efiBootFile << "\n";
    logFile << "Selected mode: " << mode << "\n";

    // Compute the relative path for BCD
    std::string efiPath = efiBootFile.substr(espDriveLetter.length());
    // efiPath matches the case in the file system

    logFile << "EFI path: " << efiPath << "\n";

    std::string cmd1, cmd2, cmd3;
    if (mode == "RAMDISK") {
        // For RAMDISK mode: configure device and osdevice as ramdisk pointing to the ISO file
        std::string ramdiskPath = "[" + dataDevice + "]\\iso.iso";
        cmd1 = "bcdedit /set " + guid + " device ramdisk=" + ramdiskPath;
        cmd2 = "bcdedit /set " + guid + " osdevice ramdisk=" + ramdiskPath;
        cmd3 = "bcdedit /set " + guid + " path " + efiPath;
        logFile << "RAMDISK mode: device/osdevice set to ramdisk=" << ramdiskPath << "\n";

        // Additional ramdisk options
        std::string cmdRamdiskSdiDevice = "bcdedit /set " + guid + " ramdisksdidevice partition=" + dataDevice;
        std::string cmdRamdiskSdiPath = "bcdedit /set " + guid + " ramdisksdipath \\iso.iso";
        exec(cmdRamdiskSdiDevice.c_str());
        exec(cmdRamdiskSdiPath.c_str());
        logFile << "RAMDISK mode: ramdisksdidevice and ramdisksdipath configured.\n";
    } else {
        // For EXTRACTED mode: standard EFI booting
        cmd1 = "bcdedit /set " + guid + " device partition=" + espDevice;
        cmd2 = "bcdedit /set " + guid + " osdevice partition=" + dataDevice;
        cmd3 = "bcdedit /set " + guid + " path " + efiPath;
        logFile << "EXTRACTED mode: standard EFI configuration.\n";
    }

    std::string result1 = exec(cmd1.c_str());
    if (result1.find("error") != std::string::npos) return "Error al configurar device: " + cmd1;

    std::string result2 = exec(cmd2.c_str());
    if (result2.find("error") != std::string::npos) return "Error al configurar osdevice: " + cmd2;

    std::string result3 = exec(cmd3.c_str());
    if (result3.find("error") != std::string::npos) return "Error al configurar path: " + cmd3;

    // Remove systemroot for EFI booting
    std::string cmd4 = "bcdedit /deletevalue " + guid + " systemroot";
    exec(cmd4.c_str()); // Don't check error as it might not exist

    std::string cmd6 = "bcdedit /default " + guid;
    std::string result6 = exec(cmd6.c_str());
    if (result6.find("error") != std::string::npos) return "Error al configurar default: " + cmd6;

    logFile.close();

    return "";
}

bool BCDManager::restoreBCD()
{
    std::string output = exec("bcdedit /enum");
    auto lines = split(output, '\n');
    std::string guid;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("description") != std::string::npos && lines[i].find("ISOBOOT") != std::string::npos) {
            if (i > 0 && lines[i-1].find("identifier") != std::string::npos) {
                size_t pos = lines[i-1].find("{");
                if (pos != std::string::npos) {
                    size_t end = lines[i-1].find("}", pos);
                    if (end != std::string::npos) {
                        guid = lines[i-1].substr(pos, end - pos + 1);
                    }
                }
            }
            break;
        }
    }
    if (!guid.empty()) {
        std::string cmd1 = "bcdedit /delete " + guid;
        exec(cmd1.c_str());
        std::string cmd2 = "bcdedit /default {current}";
        exec(cmd2.c_str());
        return true;
    }
    return false;
}