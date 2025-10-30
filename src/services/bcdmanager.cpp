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
    Utils::exec("bcdedit /default {current}");

    // Delete any existing ISOBOOT entries to avoid duplicates
    // Use block parsing and case-insensitive search to handle localized bcdedit output
    std::string enumOutput = Utils::exec("bcdedit /enum all");
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
                    std::string deleteCmd = "bcdedit /delete " + guid + " /f"; // force remove
                    Utils::exec(deleteCmd.c_str());
                }
            }
        }
    }

    std::string bcdLabel = strategy.getBCDLabel();
    std::string output = Utils::exec(("bcdedit /copy {default} /d \"" + bcdLabel + "\"").c_str());
    if (output.find("error") != std::string::npos || output.find("{") == std::string::npos) return "Error al copiar entrada BCD";
    size_t pos = output.find("{");
    size_t end = output.find("}", pos);
    if (end == std::string::npos) return "Error al extraer GUID de la nueva entrada";
    std::string guid = output.substr(pos, end - pos + 1);

    // Find the EFI boot file in ESP - prioritize based on boot mode
    std::string efiBootFile;

    if (bcdLabel == "ISOBOOT") {
        // Extracted Boot Mode - prefer bootmgfw.efi for EFI boot
        std::string candidate1 = espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
        if (GetFileAttributesA(candidate1.c_str()) != INVALID_FILE_ATTRIBUTES) {
            efiBootFile = candidate1;
        } else {
            std::string candidate2 = espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi";
            if (GetFileAttributesA(candidate2.c_str()) != INVALID_FILE_ATTRIBUTES) {
                efiBootFile = candidate2;
            } else {
                // Fallback to standard EFI boot files
                std::string candidate3 = espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI";
                if (GetFileAttributesA(candidate3.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    efiBootFile = candidate3;
                } else {
                    std::string candidate4 = espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI";
                    if (GetFileAttributesA(candidate4.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        efiBootFile = candidate4;
                    } else {
                        std::string candidate5 = espDriveLetter + "\\EFI\\boot\\bootx64.efi";
                        if (GetFileAttributesA(candidate5.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            efiBootFile = candidate5;
                        } else {
                            return "Archivo EFI boot no encontrado en ESP para modo Instalación Completa";
                        }
                    }
                }
            }
        }
    } else if (bcdLabel == "ISOBOOT_RAM") {
        // Ramdisk Boot Mode - prefer BOOTX64.EFI which is designed for installation media
        std::string candidate1 = espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI";
        if (GetFileAttributesA(candidate1.c_str()) != INVALID_FILE_ATTRIBUTES) {
            efiBootFile = candidate1;
        } else {
            std::string candidate2 = espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI";
            if (GetFileAttributesA(candidate2.c_str()) != INVALID_FILE_ATTRIBUTES) {
                efiBootFile = candidate2;
            } else {
                std::string candidate3 = espDriveLetter + "\\EFI\\boot\\bootx64.efi";
                if (GetFileAttributesA(candidate3.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    efiBootFile = candidate3;
                } else {
                    // Fallback to bootmgfw.efi
                    std::string candidate4 = espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
                    if (GetFileAttributesA(candidate4.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        efiBootFile = candidate4;
                    } else {
                        std::string candidate5 = espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi";
                        if (GetFileAttributesA(candidate5.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            efiBootFile = candidate5;
                        } else {
                            return "Archivo EFI boot no encontrado en ESP para modo Boot desde Memoria";
                        }
                    }
                }
            }
        }
    } else {
        // Fallback to original logic for unknown modes
        std::string candidate1 = espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI";  // Standard EFI boot uppercase
        if (GetFileAttributesA(candidate1.c_str()) != INVALID_FILE_ATTRIBUTES) {
            efiBootFile = candidate1;
        } else {
            std::string candidate2 = espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI";  // lowercase dir
            if (GetFileAttributesA(candidate2.c_str()) != INVALID_FILE_ATTRIBUTES) {
                efiBootFile = candidate2;
            } else {
                std::string candidate3 = espDriveLetter + "\\EFI\\boot\\bootx64.efi";  // Alternative case
                if (GetFileAttributesA(candidate3.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    efiBootFile = candidate3;
                } else {
                    std::string candidate4 = espDriveLetter + "\\EFI\\boot\\BOOTIA32.EFI";  // 32-bit
                    if (GetFileAttributesA(candidate4.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        efiBootFile = candidate4;
                    } else {
                        std::string candidate5 = espDriveLetter + "\\EFI\\boot\\bootia32.efi";  // 32-bit alternative case
                        if (GetFileAttributesA(candidate5.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            efiBootFile = candidate5;
                        } else {
                            // Fallback to Microsoft Boot files
                            std::string candidate6 = espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgr.efi";  // Windows bootmgr
                            if (GetFileAttributesA(candidate6.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                efiBootFile = candidate6;
                            } else {
                                std::string candidate7 = espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi";  // Windows bootmgfw
                                if (GetFileAttributesA(candidate7.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                    efiBootFile = candidate7;
                                } else {
                                    // Last resort: system files
                                    std::string candidate8 = espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi";
                                    if (GetFileAttributesA(candidate8.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                        efiBootFile = candidate8;
                                    } else {
                                        return "Archivo EFI boot no encontrado en ESP";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    WORD machine = GetMachineType(efiBootFile);
    if (machine != IMAGE_FILE_MACHINE_AMD64 && machine != IMAGE_FILE_MACHINE_I386) {
        if (eventManager) eventManager->notifyLogUpdate("Error: Arquitectura EFI no soportada.\r\n");
        return "Arquitectura EFI no soportada";
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

    // Pass simple drive letters (e.g., "Z:") to strategies so they can use partition= syntax
    strategy.configureBCD(guid, driveLetter, espDriveLetter, efiPath);

    // Remove systemroot for EFI booting
    std::string cmd4 = "bcdedit /deletevalue " + guid + " systemroot";
    Utils::exec(cmd4.c_str()); // Don't check error as it might not exist

    std::string cmd6 = "bcdedit /default " + guid;
    std::string result6 = Utils::exec(cmd6.c_str());
    if (result6.find("error") != std::string::npos) {
        if (eventManager) eventManager->notifyLogUpdate("Error al configurar default: " + cmd6 + "\r\n");
        return "Error al configurar default: " + cmd6;
    }

    logFile.close();

    return "";
}

bool BCDManager::restoreBCD()
{
    std::string output = Utils::exec("bcdedit /enum");
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
        Utils::exec(cmd1.c_str());
        std::string cmd2 = "bcdedit /default {current}";
        Utils::exec(cmd2.c_str());
        return true;
    }
    return false;
}