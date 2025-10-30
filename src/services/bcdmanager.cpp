#include "bcdmanager.h"
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <winnt.h>
#include <string>
#include <ctime>
#include <vector>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <fstream>

BCDManager& BCDManager::getInstance() {
    static BCDManager instance;
    return instance;
}

BCDManager::BCDManager()
    : eventManager(nullptr)
{
}

BCDManager::~BCDManager()
{
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

std::string BCDManager::configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, BootStrategy& strategy)
{
    const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";
    if (eventManager) eventManager->notifyLogUpdate("Configurando Boot Configuration Data (BCD)...\r\n");

    // Get volume GUID for data partition
    WCHAR dataVolumeName[MAX_PATH];
    std::wstring wDriveLetter = Utils::utf8_to_wstring(driveLetter);
    if (wDriveLetter.empty() || wDriveLetter.back() != L'\\') wDriveLetter += L'\\';

    // Try GetVolumeNameForVolumeMountPointW with a few retries (transient mount timing issues)
    const int MAX_ATTEMPTS = 3;
    BOOL gotVolumeName = FALSE;
    DWORD lastErr = 0;

    // Prepare a small timestamp helper and a log file for detailed BCD config diagnostics
    auto getTS = []() -> std::string {
        char buffer[64];
        std::time_t now = std::time(nullptr);
        std::tm localTime;
        localtime_s(&localTime, &now);
        std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
        return std::string(buffer);
    };

    // Prepare a log file for detailed BCD config diagnostics
    std::string bcdLogDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(bcdLogDir.c_str(), NULL);
    std::ofstream dbgLog((bcdLogDir + "\\" + BCD_CONFIG_LOG_FILE).c_str(), std::ios::app);
    dbgLog << getTS() << "Attempting GetVolumeNameForVolumeMountPointW for: " << Utils::wstring_to_utf8(wDriveLetter) << std::endl;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
            if (GetVolumeNameForVolumeMountPointW(wDriveLetter.c_str(), dataVolumeName, MAX_PATH)) {
            gotVolumeName = TRUE;
            dbgLog << getTS() << "GetVolumeNameForVolumeMountPointW succeeded on attempt " << attempt << std::endl;
            break;
        } else {
            lastErr = GetLastError();
            dbgLog << getTS() << "GetVolumeNameForVolumeMountPointW failed on attempt " << attempt << ", error=" << lastErr << std::endl;
            // small delay before retry
            Sleep(500);
        }
    }

    if (!gotVolumeName) {
        if (eventManager) eventManager->notifyLogUpdate("GetVolumeNameForVolumeMountPointW failed for " + driveLetter + ", trying fallback enumeration...\r\n");
    dbgLog << getTS() << "Starting fallback volume enumeration to find mount point matching: " << Utils::wstring_to_utf8(wDriveLetter) << std::endl;

        BOOL found = FALSE;
        WCHAR volName[MAX_PATH];
        HANDLE hVol = FindFirstVolumeW(volName, MAX_PATH);
        if (hVol != INVALID_HANDLE_VALUE) {
            do {
                // Ensure volume name ends with backslash for GetVolumePathNamesForVolumeNameW
                std::wstring volNameStr = volName;
                if (volNameStr.back() != L'\\') volNameStr.push_back(L'\\');

                dbgLog << getTS() << "Enumerating volume: " << Utils::wstring_to_utf8(volNameStr) << std::endl;

                // Get mount points for this volume
                DWORD returnLen = 0;
                BOOL got = GetVolumePathNamesForVolumeNameW(volNameStr.c_str(), NULL, 0, &returnLen);
                DWORD gle = GetLastError();
                dbgLog << getTS() << "  GetVolumePathNamesForVolumeNameW initial call returned " << got << " lastErr=" << gle << " needLen=" << returnLen << std::endl;

                if (!got && gle == ERROR_MORE_DATA && returnLen > 0) {
                    std::vector<WCHAR> buf(returnLen);
                    if (GetVolumePathNamesForVolumeNameW(volNameStr.c_str(), buf.data(), returnLen, &returnLen)) {
                        // buffer contains multi-string of path names
                        WCHAR* p = buf.data();
                        while (*p) {
                            std::wstring mountPoint(p);
                            if (mountPoint.back() != L'\\') mountPoint.push_back(L'\\');
                            dbgLog << getTS() << "    Found mount point: " << Utils::wstring_to_utf8(mountPoint) << std::endl;
                            if (mountPoint == wDriveLetter) {
                                // Found matching volume
                                wcsncpy_s(dataVolumeName, volName, MAX_PATH);
                                found = TRUE;
                                dbgLog << getTS() << "    -> MATCH for " << Utils::wstring_to_utf8(wDriveLetter) << std::endl;
                                break;
                            }
                            p += wcslen(p) + 1;
                        }
                    } else {
                        dbgLog << getTS() << "    GetVolumePathNamesForVolumeNameW failed on populate, err=" << GetLastError() << std::endl;
                    }
                }

                if (found) break;
            } while (FindNextVolumeW(hVol, volName, MAX_PATH));
            FindVolumeClose(hVol);
            } else {
            dbgLog << getTS() << "FindFirstVolumeW failed, err=" << GetLastError() << std::endl;
        }

        if (!found) {
            dbgLog << getTS() << "Fallback enumeration did not find a matching mount point for " << Utils::wstring_to_utf8(wDriveLetter) << std::endl;
            dbgLog.close();
            return "Error al obtener el nombre del volumen de datos";
        }
        dbgLog << getTS() << "Fallback found volume: " << Utils::wstring_to_utf8(dataVolumeName) << std::endl;
        dbgLog.close();
    } else {
        // success from initial call; log and close debug log
        dbgLog << getTS() << "Volume name: " << Utils::wstring_to_utf8(dataVolumeName) << std::endl;
        dbgLog.close();
    }
    char narrowDataVolumeName[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, dataVolumeName, -1, narrowDataVolumeName, sizeof(narrowDataVolumeName), NULL, NULL);
    std::string dataDevice = narrowDataVolumeName;

    // Get volume GUID for ESP
    WCHAR espVolumeName[MAX_PATH];
    std::wstring wEspDriveLetter = Utils::utf8_to_wstring(espDriveLetter);
    if (wEspDriveLetter.empty() || wEspDriveLetter.back() != L'\\') wEspDriveLetter += L'\\';
    if (!GetVolumeNameForVolumeMountPointW(wEspDriveLetter.c_str(), espVolumeName, MAX_PATH)) {
        return "Error al obtener el nombre del volumen ESP";
    }
    char narrowEspVolumeName[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, espVolumeName, -1, narrowEspVolumeName, sizeof(narrowEspVolumeName), NULL, NULL);
    std::string espDevice = narrowEspVolumeName;

    // Set default to current to avoid issues with deleting the default entry
    Utils::exec((BCD_CMD + " /default {current}").c_str());

    // Delete any existing ISOBOOT entries to avoid duplicates
    // Use block parsing and case-insensitive search to handle localized bcdedit output
    std::string enumOutput = Utils::exec((BCD_CMD + " /enum all").c_str());
    auto blocks = split(enumOutput, '\n');
    // Parse into blocks separated by empty lines
    std::vector<std::string> entryBlocks;
    std::string currentBlock;
    for (const auto& line : blocks) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            if (!currentBlock.empty()) {
                entryBlocks.push_back(currentBlock);
                currentBlock.clear();
            }
        } else {
            currentBlock += line + "\n";
        }
    }
    if (!currentBlock.empty()) entryBlocks.push_back(currentBlock);

    auto icontains = [](const std::string& hay, const std::string& needle) {
        std::string h = hay;
        std::string n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return h.find(n) != std::string::npos;
    };

    std::string labelToFind = "ISOBOOT";
    for (const auto& blk : entryBlocks) {
        if (icontains(blk, labelToFind)) {
            // find GUID in block
            size_t pos = blk.find('{');
            if (pos != std::string::npos) {
                size_t end = blk.find('}', pos);
                if (end != std::string::npos) {
                    std::string guid = blk.substr(pos, end - pos + 1);
                    std::string deleteCmd = BCD_CMD + " /delete " + guid + " /f"; // force remove
                    Utils::exec(deleteCmd.c_str());
                }
            }
        }
    }

    std::string bcdLabel = strategy.getBCDLabel();
    
    // Create appropriate BCD entry based on boot mode
    std::string output;
    if (bcdLabel == "ISOBOOT_RAM") {
        // For ramdisk mode, create a new OSLOADER entry that supports ramdisk parameters
        output = Utils::exec((BCD_CMD + " /create /application OSLOADER /d \"" + bcdLabel + "\"").c_str());
    } else {
        // For other modes, copy the default entry
        output = Utils::exec((BCD_CMD + " /copy {default} /d \"" + bcdLabel + "\"").c_str());
    }
    
    if (output.find("error") != std::string::npos || output.find("{") == std::string::npos) return "Error al crear/copiar entrada BCD";
    size_t pos = output.find("{");
    size_t end = output.find("}", pos);
    if (end == std::string::npos) return "Error al extraer GUID de la nueva entrada";
    std::string guid = output.substr(pos, end - pos + 1);

    // Find the EFI boot file in ESP - prioritize based on boot mode
    std::string efiBootFile;

    // Build candidate lists per mode and choose the best candidate by validating architecture
    std::vector<std::string> candidates;
    if (bcdLabel == "ISOBOOT") {
        // Installation mode: prefer BOOTX64 files, then Microsoft bootmgfw
        candidates = {
            espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\bootx64.efi",
            espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"
        };
    } else if (bcdLabel == "ISOBOOT_RAM") {
        // Ramdisk mode: prefer BOOTX64 as well, then Microsoft bootmgfw
        candidates = {
            espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\bootx64.efi",
            espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"
        };
    } else {
        candidates = {
            espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\bootx64.efi",
            espDriveLetter + "\\EFI\\boot\\BOOTIA32.EFI",
            espDriveLetter + "\\EFI\\boot\\bootia32.efi",
            espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgr.efi",
            espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"
        };
    }

    // Evaluate candidates: collect existing candidates and their architectures
    int bestIndex = -1;
    int bestScore = -1; // 2 = amd64, 1 = i386, 0 = exists
    int firstAmd64Index = -1;
    int firstI386Index = -1;
    std::vector<int> existingIndices;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const std::string& c = candidates[i];
        if (GetFileAttributesA(c.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        existingIndices.push_back((int)i);
        WORD m = GetMachineType(c);
        int score = 0;
        if (m == IMAGE_FILE_MACHINE_AMD64) score = 2;
        else if (m == IMAGE_FILE_MACHINE_I386) score = 1;
        else score = 0;
        if (firstAmd64Index == -1 && m == IMAGE_FILE_MACHINE_AMD64) firstAmd64Index = (int)i;
        if (firstI386Index == -1 && m == IMAGE_FILE_MACHINE_I386) firstI386Index = (int)i;
        if (eventManager) {
            std::ostringstream oss;
            oss << std::hex << std::uppercase << m;
            eventManager->notifyLogUpdate("Candidate EFI: " + c + ", machine=0x" + oss.str() + " score=" + std::to_string(score) + "\r\n");
        }
        if (score > bestScore) {
            bestScore = score;
            bestIndex = (int)i;
            if (bestScore == 2) break; // best possible
        }
    }

    // If we found both architectures available, ask the user which one to use
    if (firstAmd64Index != -1 && firstI386Index != -1) {
        std::string amdPath = candidates[firstAmd64Index];
        std::string i386Path = candidates[firstI386Index];
        std::wstring wmsg = L"Se encontraron EFI de ambas arquitecturas en la ESP:\n\n";
        wmsg += Utils::utf8_to_wstring(std::string("x64: ") + amdPath) + L"\n";
        wmsg += Utils::utf8_to_wstring(std::string("x86: ") + i386Path) + L"\n\n";
        wmsg += L"¿Cuál desea usar? Seleccione Sí para x64, No para x86.";
        int res = MessageBoxW(NULL, wmsg.c_str(), L"Seleccionar arquitectura EFI", MB_YESNO | MB_ICONQUESTION);
        if (res == IDYES) {
            bestIndex = firstAmd64Index;
        } else {
            bestIndex = firstI386Index;
        }
    }

    if (bestIndex != -1) {
        efiBootFile = candidates[bestIndex];
    } else if (!existingIndices.empty()) {
        // fallback: pick the first existing candidate
        efiBootFile = candidates[existingIndices[0]];
    } else {
        return "Archivo EFI boot no encontrado en ESP";
    }

    WORD machine = GetMachineType(efiBootFile);
    if (machine != IMAGE_FILE_MACHINE_AMD64 && machine != IMAGE_FILE_MACHINE_I386) {
        if (eventManager) eventManager->notifyLogUpdate("Error: Arquitectura EFI no soportada.\r\n");
        return "Arquitectura EFI no soportada";
    }

    // Validate EFI architecture matches system architecture
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    WORD sysArch = (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? IMAGE_FILE_MACHINE_AMD64 :
                   (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) ? IMAGE_FILE_MACHINE_I386 : 0;
    if (machine != sysArch) {
        std::string errorMsg = "Error: Arquitectura EFI (0x" + std::to_string(machine) + ") no coincide con la del sistema (0x" + std::to_string(sysArch) + ").\r\n";
        if (eventManager) eventManager->notifyLogUpdate(errorMsg);
        return "Arquitectura EFI no compatible con el sistema";
    }

    // Log selected file and mode
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL); // Create logs directory if it doesn't exist
    std::ofstream logFile((logDir + "\\" + BCD_CONFIG_LOG_FILE).c_str(), std::ios::app);
    logFile << "Selected EFI boot file: " << efiBootFile << std::endl;
    logFile << "Selected mode: " << bcdLabel << std::endl;
    logFile << "ESP drive letter: " << espDriveLetter << std::endl;

    // Log detailed file checking for debugging
    if (bcdLabel == "ISOBOOT") {
        logFile << "EFI file selection for ISOBOOT mode:" << std::endl;
        std::string candidates[] = {
            espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\bootx64.efi"
        };
        for (const auto& candidate : candidates) {
            bool exists = (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES);
            logFile << "  " << candidate << " - " << (exists ? "EXISTS" : "NOT FOUND");
            if (candidate == efiBootFile) logFile << " [SELECTED]";
            logFile << std::endl;
        }
    } else if (bcdLabel == "ISOBOOT_RAM") {
        logFile << "EFI file selection for ISOBOOT_RAM mode:" << std::endl;
        std::string candidates[] = {
            espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
            espDriveLetter + "\\EFI\\boot\\bootx64.efi",
            espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"
        };
        for (const auto& candidate : candidates) {
            bool exists = (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES);
            logFile << "  " << candidate << " - " << (exists ? "EXISTS" : "NOT FOUND");
            if (candidate == efiBootFile) logFile << " [SELECTED]";
            logFile << std::endl;
        }
    }

    if (eventManager) eventManager->notifyLogUpdate("Archivo EFI seleccionado: " + efiBootFile + "\r\n");

    // Compute the relative path for BCD
    std::string efiPath = efiBootFile.substr(espDriveLetter.length());
    // efiPath matches the case in the file system

    logFile << "EFI path: " << efiPath << "\n";

    // For extracted mode, verify SDI file exists
    if (bcdLabel == "ISOBOOT") {
        std::string sdiPath = driveLetter + "\\boot\\boot.sdi";
        if (GetFileAttributesA(sdiPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::string errorMsg = "Error: Archivo SDI no encontrado en " + sdiPath + "\r\n";
            if (eventManager) eventManager->notifyLogUpdate(errorMsg);
            logFile << errorMsg;
            logFile.close();
            return "Archivo SDI no encontrado para extracted boot";
        }
        logFile << "SDI file verified: " << sdiPath << std::endl;
    } else if (bcdLabel == "ISOBOOT_RAM") {
        // For ramdisk mode, verify ISO file exists (SDI is inside the ISO)
        std::string isoPath = driveLetter + "\\iso.iso";
        if (GetFileAttributesA(isoPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::string errorMsg = "Error: Archivo ISO no encontrado en " + isoPath + "\r\n";
            if (eventManager) eventManager->notifyLogUpdate(errorMsg);
            logFile << errorMsg;
            logFile.close();
            return "Archivo ISO no encontrado para ramdisk boot";
        }
        logFile << "ISO file verified: " << isoPath << std::endl;
    }

    // Pass simple drive letters (e.g., "Z:") to strategies so they can use partition= syntax
    strategy.configureBCD(guid, driveLetter, espDriveLetter, efiPath);

    // Log after strategy configuration
    logFile << "Strategy configuration completed. Proceeding with final BCD setup...\n";

    // Remove systemroot for EFI booting (ignore error if it doesn't exist - normal for OSLOADER entries)
    std::string cmd4 = BCD_CMD + " /deletevalue " + guid + " systemroot";
    std::string result4 = Utils::exec(cmd4.c_str());
    logFile << "Remove systemroot command: " << cmd4 << "\nResult: " << result4 << "\n";
    // Note: This may fail for OSLOADER entries that don't have systemroot - that's OK

    std::string cmd6 = BCD_CMD + " /default " + guid;
    std::string result6 = Utils::exec(cmd6.c_str());
    logFile << "Set default command: " << cmd6 << "\nResult: " << result6 << "\n";
    if (result6.find("error") != std::string::npos || result6.find("Error") != std::string::npos) {
        if (eventManager) eventManager->notifyLogUpdate("Error al configurar default: " + result6 + "\r\n");
        logFile << "ERROR: Failed to set default boot entry. Result: " << result6 << "\n";
        logFile.close();
        return "Error al configurar default: " + result6;
    }

    logFile << "BCD configuration completed successfully\n";
    logFile.close();

    return "";
}

bool BCDManager::restoreBCD()
{
    const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";
    std::string output = Utils::exec((BCD_CMD + " /enum").c_str());
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
        std::string cmd1 = BCD_CMD + " /delete " + guid;
        Utils::exec(cmd1.c_str());
        std::string cmd2 = BCD_CMD + " /default {current}";
        Utils::exec(cmd2.c_str());
        return true;
    }
    return false;
}