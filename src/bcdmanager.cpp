#include "bcdmanager.h"
#include <windows.h>
#include <winnt.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>

BCDManager::BCDManager()
{
}

BCDManager::~BCDManager()
{
}

std::string exec(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = _popen(cmd, "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof buffer, pipe) != NULL) {
        result += buffer;
    }
    _pclose(pipe);
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

std::string BCDManager::configureBCD(const std::string& driveLetter, bool isWindowsISO)
{
    // Set default to current to avoid issues with deleting the default entry
    exec("bcdedit /default {current}");

    // Delete any existing EASYISOBOOT entries to avoid duplicates
    std::string enumOutput = exec("bcdedit /enum");
    auto lines = split(enumOutput, '\n');
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("description") != std::string::npos && lines[i].find("EASYISOBOOT") != std::string::npos) {
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

    std::string output = exec("bcdedit /copy {default} /d \"EASYISOBOOT\"");
    if (output.find("error") != std::string::npos || output.find("{") == std::string::npos) return "Error al copiar entrada BCD";
    size_t pos = output.find("{");
    size_t end = output.find("}", pos);
    if (end == std::string::npos) return "Error al extraer GUID de la nueva entrada";
    std::string guid = output.substr(pos, end - pos + 1);

    // Find the EFI boot file
    std::string efiBootFile;
    std::string candidate1 = driveLetter + "\\efi\\boot\\bootx64.efi";
    if (GetFileAttributesA(candidate1.c_str()) != INVALID_FILE_ATTRIBUTES) {
        efiBootFile = candidate1;
    } else {
        std::string candidate2 = driveLetter + "\\efi\\boot\\bootia32.efi";
        if (GetFileAttributesA(candidate2.c_str()) != INVALID_FILE_ATTRIBUTES) {
            efiBootFile = candidate2;
        } else {
            std::string candidate3 = driveLetter + "\\efi\\boot\\BOOTX64.EFI";
            if (GetFileAttributesA(candidate3.c_str()) != INVALID_FILE_ATTRIBUTES) {
                efiBootFile = candidate3;
            } else {
                std::string candidate4 = driveLetter + "\\efi\\boot\\BOOTIA32.EFI";
                if (GetFileAttributesA(candidate4.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    efiBootFile = candidate4;
                } else {
                    return "Archivo EFI boot no encontrado";
                }
            }
        }
    }

    WORD machine = GetMachineType(efiBootFile);
    if (machine != IMAGE_FILE_MACHINE_AMD64 && machine != IMAGE_FILE_MACHINE_I386) {
        return "Arquitectura EFI no soportada";
    }

    std::string efiBootPath = efiBootFile.substr(driveLetter.length());

    // Configure for EFI booting
    std::string cmd1, cmd2, cmd3;
    if (isWindowsISO) {
        // For Windows ISOs, device partition, path to winload.efi
        cmd1 = "bcdedit /set " + guid + " device partition=" + driveLetter;
        cmd2 = "bcdedit /set " + guid + " osdevice partition=" + driveLetter;
        cmd3 = "bcdedit /set " + guid + " path \\windows\\system32\\winload.efi";
    } else {
        // For other ISOs, device partition, path to EFI boot file
        cmd1 = "bcdedit /set " + guid + " device partition=" + driveLetter;
        cmd2 = "bcdedit /set " + guid + " osdevice partition=" + driveLetter;
        cmd3 = "bcdedit /set " + guid + " path " + efiBootPath;
    }

    std::string result1 = exec(cmd1.c_str());
    if (result1.find("error") != std::string::npos) return "Error al configurar device: " + cmd1;

    std::string result2 = exec(cmd2.c_str());
    if (result2.find("error") != std::string::npos) return "Error al configurar osdevice: " + cmd2;

    std::string result3 = exec(cmd3.c_str());
    if (result3.find("error") != std::string::npos) return "Error al configurar path: " + cmd3;

    if (!isWindowsISO) {
        // Configure RAMDISK for non-Windows ISOs
        std::string cmd4_ram = "bcdedit /set " + guid + " ramdisksdidevice partition=" + driveLetter;
        std::string result4_ram = exec(cmd4_ram.c_str());
        if (result4_ram.find("error") != std::string::npos) return "Error al configurar ramdisksdidevice: " + cmd4_ram;

        std::string cmd5_ram = "bcdedit /set " + guid + " ramdisksdipath \\iso.iso";
        std::string result5_ram = exec(cmd5_ram.c_str());
        if (result5_ram.find("error") != std::string::npos) return "Error al configurar ramdisksdipath: " + cmd5_ram;

        std::string cmd6_ram = "bcdedit /set " + guid + " ramdiskoptions {5189B25C-5558-4BF2-BCA4-289B11BD29E2}";
        std::string result6_ram = exec(cmd6_ram.c_str());
        if (result6_ram.find("error") != std::string::npos) return "Error al configurar ramdiskoptions: " + cmd6_ram;
    }

    // Remove systemroot for EFI booting
    std::string cmd4 = "bcdedit /deletevalue " + guid + " systemroot";
    exec(cmd4.c_str()); // Don't check error as it might not exist

    std::string cmd6 = "bcdedit /default " + guid;
    std::string result6 = exec(cmd6.c_str());
    if (result6.find("error") != std::string::npos) return "Error al configurar default: " + cmd6;

    return "";
}

bool BCDManager::restoreBCD()
{
    std::string output = exec("bcdedit /enum");
    auto lines = split(output, '\n');
    std::string guid;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("description") != std::string::npos && lines[i].find("EASYISOBOOT") != std::string::npos) {
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