#include "isocopymanager.h"
#include "../utils/constants.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <cctype>
#include <set>
#include <ctime>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <cstddef>
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"
#include "models/HashInfo.h"
#include "../utils/AppKeys.h"
#include "isotypedetector.h"
#include "../models/efimanager.h"
#include "../models/isomounter.h"
#include "../models/filecopymanager.h"
#include "version.h"
#include "../models/IniConfigurator.h"
#include "../boot/BootWimProcessor.h"
#include "../models/ContentExtractor.h"
#include "../models/HashVerifier.h"
#include "../models/ISOReader.h"
static bool     isValidPE(const std::string &path);
static uint16_t getPEMachine(const std::string &path);
static BOOL     copyFileUtf8(const std::string &src, const std::string &dst);
static HashInfo readHashInfo(const std::string &path);

ISOCopyManager &ISOCopyManager::getInstance() {
    static ISOCopyManager instance;
    return instance;
}

ISOCopyManager::ISOCopyManager()
    : typeDetector(std::make_unique<ISOTypeDetector>()), efiManager(nullptr),
      isoMounter(std::make_unique<ISOMounter>()), fileCopyManager(nullptr),
      iniConfigurator(std::make_unique<IniConfigurator>()), bootWimProcessor(nullptr), contentExtractor(nullptr),
      hashVerifier(std::make_unique<HashVerifier>()), isoReader(std::make_unique<ISOReader>()),
      isWindowsISODetected(false) {}

ISOCopyManager::~ISOCopyManager() {}

bool ISOCopyManager::getIsWindowsISO() const {
    return isWindowsISODetected;
}

const char *ISOCopyManager::getTimestamp() {
    static char buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm     localTime;
    localtime_s(&localTime, &now);
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return buffer;
}

std::string ISOCopyManager::exec(const char *cmd, EventManager *eventManager) {
    HANDLE              hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return "";

    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    si.dwFlags      = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput   = hWrite;
    si.hStdError    = hWrite;
    si.wShowWindow  = SW_HIDE;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "";
    }

    CloseHandle(hWrite);

    char        buffer[128];
    std::string result = "";
    DWORD       bytesRead;
    HANDLE      handles[2] = {hRead, pi.hProcess};
    DWORD       waitResult;

    while ((waitResult = WaitForMultipleObjects(2, handles, FALSE, 100)) != WAIT_OBJECT_0 + 1) {
        if (eventManager && eventManager->isCancelRequested()) {
            // Terminate the process if cancellation is requested
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(hRead);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return "";
        }
        if (waitResult == WAIT_OBJECT_0) {
            // Data available in pipe
            if (!ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) || bytesRead == 0) {
                break;
            }
            buffer[bytesRead] = '\0';
            result += buffer;
        } else if (waitResult == WAIT_TIMEOUT) {
            // Continue waiting
        } else {
            break;
        }
    }

    CloseHandle(hRead);
    if (waitResult == WAIT_OBJECT_0 + 1) {
        // Process finished
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

DWORD CALLBACK CopyProgressRoutine(LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
                                   LARGE_INTEGER StreamSize, LARGE_INTEGER StreamBytesTransferred, DWORD dwStreamNumber,
                                   DWORD dwCallbackReason, HANDLE hSourceFile, HANDLE hDestinationFile, LPVOID lpData) {
    EventManager *eventManager = static_cast<EventManager *>(lpData);
    if (eventManager) {
        if (eventManager->isCancelRequested()) {
            return PROGRESS_CANCEL;
        }
        if (TotalFileSize.QuadPart > 0) {
            eventManager->notifyDetailedProgress(TotalBytesTransferred.QuadPart, TotalFileSize.QuadPart,
                                                 "Copiando ISO");
        }
    }
    return PROGRESS_CONTINUE;
}

void ISOCopyManager::listDirectoryRecursive(std::ofstream &log, const std::string &path, int depth, int maxDepth,
                                            EventManager &eventManager, long long &fileCount) {
    if (depth >= maxDepth)
        return;

    WIN32_FIND_DATAA findData;
    HANDLE           hFind = FindFirstFileA((path + "*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        std::string fileName = findData.cFileName;
        if (fileName != "." && fileName != "..") {
            fileCount++;
            // Update progress at the start and every 100 files to show activity
            if (fileCount == 1 || fileCount % 100 == 0) {
                eventManager.notifyDetailedProgress(fileCount, 0,
                                                    "Analizando archivos del ISO (" + std::to_string(fileCount) + ")");
            }
            std::string indent(depth * 2, ' ');
            log << indent << fileName;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                log << "/" << std::endl;
                listDirectoryRecursive(log, path + fileName + "\\", depth + 1, maxDepth, eventManager, fileCount);
            } else {
                log << std::endl;
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}

bool ISOCopyManager::extractISOContents(EventManager &eventManager, const std::string &isoPath,
                                        const std::string &destPath, const std::string &espPath, bool extractContent,
                                        bool extractBootWim, bool copyInstallWim, const std::string &mode,
                                        const std::string &format) {
    // Initialize managers with EventManager
    fileCopyManager  = std::make_unique<FileCopyManager>(eventManager);
    efiManager       = std::make_unique<EFIManager>(eventManager, *fileCopyManager);
    bootWimProcessor = std::make_unique<BootWimProcessor>(eventManager, *fileCopyManager);
    contentExtractor = std::make_unique<ContentExtractor>(eventManager, *fileCopyManager);

    std::string modeLabel = LocalizationManager::getInstance().getUtf8String("bootMode." + mode);
    if (modeLabel.empty()) {
        if (mode == AppKeys::BootModeRam) {
            modeLabel = "Boot desde Memoria";
        } else if (mode == AppKeys::BootModeExtract) {
            modeLabel = "Boot desde Disco";
        } else {
            modeLabel = mode;
        }
    }

    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    // Create log file for debugging
    std::ofstream logFile(logDir + "\\" + ISO_EXTRACT_LOG_FILE);
    logFile << getTimestamp() << "Starting ISO extraction from: " << isoPath << std::endl;
    logFile << getTimestamp() << "Destination (data): " << destPath << std::endl;
    logFile << getTimestamp() << "ESP path: " << espPath << std::endl;
    logFile << getTimestamp() << "Extract content: " << (extractContent ? "Yes" : "No") << std::endl;

    bool        integratePrograms = false;
    std::string programsSrc;
    std::string sourceInstallPath;

    long long isoSize     = Utils::getFileSize(isoPath);
    long long copiedSoFar = 0;
    eventManager.notifyDetailedProgress(5, 100, "Analizando ISO");

    std::string sourcePath = isoPath;
    logFile << getTimestamp() << "Source path: " << sourcePath << std::endl;

    eventManager.notifyDetailedProgress(10, 100, "Analizando contenido ISO");

    // List all files in the ISO
    auto      files     = isoReader->listFiles(isoPath);
    long long fileCount = files.size();
    logFile << getTimestamp() << "Total files analyzed: " << fileCount << std::endl;

    // Log some files
    size_t limit = std::min<size_t>(20, files.size());
    for (size_t i = 0; i < limit; ++i) {
        logFile << getTimestamp() << "File: " << files[i] << std::endl;
    }

    eventManager.notifyLogUpdate("Analizando contenido del ISO...\r\n");

    // Check if it's Windows ISO
    bool isWindowsISO  = false;
    bool hasSourcesDir = false;
    bool hasSetupExe   = false;
    bool hasBootMgr    = false;

    for (const auto &file : files) {
        std::string lower = Utils::toLower(file);
        if (lower.find("boot.wim") != std::string::npos || lower.find("install.wim") != std::string::npos ||
            lower.find("install.esd") != std::string::npos) {
            isWindowsISO = true;
            break;
        }
        if (lower.find("sources/") == 0 || lower == "sources") {
            hasSourcesDir = true;
        }
        if (lower.find("setup.exe") != std::string::npos) {
            hasSetupExe = true;
        }
        if (lower.find("bootmgr") != std::string::npos) {
            hasBootMgr = true;
        }
    }

    // Heuristic: If we have sources directory and setup.exe, it's likely a Windows ISO
    // even if we can't read the UDF part with boot.wim/install.wim
    if (!isWindowsISO && hasSourcesDir && hasSetupExe) {
        isWindowsISO = true;
        logFile << getTimestamp() << "Windows ISO detected via heuristic (sources + setup.exe)" << std::endl;
    }
    logFile << getTimestamp() << "Is Windows ISO: " << (isWindowsISO ? "Yes" : "No") << std::endl;

    if (isWindowsISO) {
        eventManager.notifyLogUpdate("ISO de Windows detectado.\r\n");
    } else {
        eventManager.notifyLogUpdate("ISO no-Windows detectado.\r\n");
    }

    isWindowsISODetected = isWindowsISO;

    // Calculate MD5 of the ISO
    std::string hashFilePath = destPath + "\\ISOBOOTHASH";
    bool        skipCopy     = hashVerifier->shouldSkipCopy(isoPath, hashFilePath, mode, format);
    if (skipCopy) {
        logFile << getTimestamp() << "ISO hash, version, mode and format match existing, skipping content copy"
                << std::endl;
        eventManager.notifyLogUpdate(
            "Hash, version, modo y formato del ISO coinciden, omitiendo copia de contenido.\r\n");
    }

    if (extractContent && !skipCopy) {
        std::vector<std::string> excludePatterns;
        if (!isoReader->extractAll(isoPath, destPath, excludePatterns)) {
            logFile.close();
            return false;
        }
        copiedSoFar += isoSize;
    } else if (!extractContent && isWindowsISO && !skipCopy) {
        // For Windows ISOs in RAM mode, flag Programs integration without pre-extracting the folder
        if (mode == AppKeys::BootModeRam) {
            integratePrograms = true;
            programsSrc       = destPath + "Programs";
            logFile << getTimestamp() << "Programs integration scheduled for RAM boot (no pre-extraction)" << std::endl;
            eventManager.notifyLogUpdate("Integracion de Programs en boot.wim para arranque RAM.\r\n");
        }
    } else {
        logFile << getTimestamp() << "Skipping content extraction (" << modeLabel << " mode)" << std::endl;
    }

    // Extract boot.wim if requested
    bool bootWimSuccess = true;
    if (extractBootWim) {
        bootWimSuccess = bootWimProcessor->processBootWim(isoPath, destPath, espPath, integratePrograms, programsSrc,
                                                          copiedSoFar, extractBootWim, copyInstallWim, logFile);
    }

    // Copy install file if requested
    if (copyInstallWim && isWindowsISO) {
        // Choose source inside ISO
        bool        esdPreferred = isoReader->fileExists(isoPath, "sources/install.esd");
        std::string installFile  = esdPreferred ? "sources/install.esd" : "sources/install.wim";
        std::string installDest  = destPath + "sources\\" + installFile.substr(installFile.find_last_of('/') + 1);

        eventManager.notifyDetailedProgress(75, 100, "Copiando archivo de instalacion");
        eventManager.notifyLogUpdate("Copiando archivo de instalacion...\r\n");
        bool extracted = isoReader->extractFile(isoPath, installFile, installDest);
        if (extracted) {
            auto validateInstall = [&](bool logOnWarn) -> bool {
                unsigned long long        srcSize = 0ULL;
                bool                      haveSrc = isoReader->getFileSize(isoPath, installFile, srcSize);
                WIN32_FILE_ATTRIBUTE_DATA dstInfo{};
                unsigned long long        dstSize = 0ULL;
                if (GetFileAttributesExA(installDest.c_str(), GetFileExInfoStandard, &dstInfo)) {
                    dstSize = (static_cast<unsigned long long>(dstInfo.nFileSizeHigh) << 32) | dstInfo.nFileSizeLow;
                }

                logFile << getTimestamp() << "Install file extracted to " << installDest
                        << ", srcSize=" << (haveSrc ? std::to_string(srcSize) : std::string("unknown"))
                        << ", dstSize=" << dstSize << std::endl;

                bool sizeOk = !haveSrc || (srcSize == dstSize);
                if (!sizeOk && logOnWarn) {
                    eventManager.notifyLogUpdate(
                        "Advertencia: tamano de origen/destino no coincide para install.*.\r\n");
                }

                std::string infoCmd =
                    std::string("\"") + Utils::getDismPath() + "\" /Get-WimInfo /WimFile:\"" + installDest + "\"";
                std::string infoOut;
                int         infoCode   = Utils::execWithExitCode(infoCmd.c_str(), infoOut);
                int         indexCount = 0;
                size_t      p          = 0;
                while ((p = infoOut.find("Index :", p)) != std::string::npos) {
                    ++indexCount;
                    p += 7;
                }
                bool dismOk =
                    (infoCode == 0) &&
                    (indexCount >= 1 || infoOut.find("The operation completed successfully") != std::string::npos ||
                     infoOut.find("correctamente") != std::string::npos);

                if (sizeOk && dismOk) {
                    logFile << getTimestamp() << "Install image validation: OK (indices=" << indexCount << ")"
                            << std::endl;
                    eventManager.notifyLogUpdate("Archivo de instalacion copiado y validado correctamente.\r\n");
                    return true;
                }

                logFile << getTimestamp() << "Install image validation: FAILED (code=" << infoCode
                        << ", sizeOk=" << (sizeOk ? "true" : "false") << ", indices=" << indexCount << ")\nOutput:\n"
                        << infoOut << std::endl;
                if (logOnWarn) {
                    eventManager.notifyLogUpdate("Error/Advertencia al validar install.*; revise iso_extract.log.\r\n");
                }
                return false;
            };

            bool ok = validateInstall(true);
            if (!ok) {
                eventManager.notifyLogUpdate("Reintentando extraccion de install.*...\r\n");
                DeleteFileA(installDest.c_str());
                if (isoReader->extractFile(isoPath, installFile, installDest)) {
                    validateInstall(false);
                }
            }
        } else {
            logFile << getTimestamp() << "Failed to extract install file" << std::endl;
            eventManager.notifyLogUpdate("Error al copiar archivo de instalacion.\r\n");
        }
        eventManager.notifyDetailedProgress(0, 0, "");
    }

    // Extract EFI
    eventManager.notifyDetailedProgress(80, 100, "Extrayendo EFI");
    bool efiSuccess = efiManager->extractEFI(isoPath, espPath, isWindowsISO, copiedSoFar, isoSize);

    logFile << getTimestamp() << "EFI extraction " << (efiSuccess ? "SUCCESS" : "FAILED") << std::endl;
    if (extractBootWim) {
        logFile << getTimestamp() << "boot.wim processing " << (bootWimSuccess ? "SUCCESS" : "FAILED") << std::endl;
    }
    if (extractContent) {
        logFile << getTimestamp() << "Content extraction completed." << std::endl;
    }
    logFile.close();

    bool overallSuccess = efiSuccess && bootWimSuccess;
    if (overallSuccess) {
        // Write the current ISO hash, mode and format to the file
        hashVerifier->saveHashInfo(hashFilePath, Utils::calculateMD5(isoPath), mode, format);
    }

    eventManager.notifyDetailedProgress(100, 100, "");
    return overallSuccess;
}

bool ISOCopyManager::copyISOFile(EventManager &eventManager, const std::string &isoPath, const std::string &destPath) {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::string destFile = destPath + "iso.iso";

    // Create log file for debugging
    std::ofstream logFile(logDir + "\\" + ISO_FILE_COPY_LOG_FILE);
    logFile << getTimestamp() << "Copying ISO file from: " << isoPath << std::endl;
    logFile << getTimestamp() << "To: " << destFile << std::endl;

    BOOL result = CopyFileExA(isoPath.c_str(), destFile.c_str(), CopyProgressRoutine, &eventManager, NULL, 0);
    if (result) {
        logFile << getTimestamp() << "ISO file copied successfully." << std::endl;
        eventManager.notifyDetailedProgress(0, 0, ""); // Clear
    } else {
        logFile << getTimestamp() << "Failed to copy ISO file. Error: " << GetLastError() << std::endl;
    }
    logFile.close();

    return result != FALSE;
}

// Quick validation for PE/EFI files: check for 'MZ' header
static bool isValidPE(const std::string &path) {
    std::wstring wpath = Utils::utf8_to_wstring(path);
    HANDLE       h =
        CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CHAR  hdr[2] = {0};
    DWORD read   = 0;
    BOOL  ok     = ReadFile(h, hdr, 2, &read, NULL);
    CloseHandle(h);
    if (!ok || read < 2)
        return false;
    return hdr[0] == 'M' && hdr[1] == 'Z';
}

// Returns IMAGE_FILE_MACHINE_* value or 0 on failure
static uint16_t getPEMachine(const std::string &path) {
    std::wstring wpath = Utils::utf8_to_wstring(path);
    HANDLE       h =
        CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD read     = 0;
    DWORD e_lfanew = 0;
    // Read e_lfanew at offset 0x3C (DWORD)
    SetFilePointer(h, 0x3C, NULL, FILE_BEGIN);
    if (!ReadFile(h, &e_lfanew, sizeof(DWORD), &read, NULL) || read != sizeof(DWORD)) {
        CloseHandle(h);
        return 0;
    }
    // Seek to PE signature + Machine (e_lfanew + 4)
    DWORD machine = 0;
    SetFilePointer(h, e_lfanew + 4, NULL, FILE_BEGIN);
    if (!ReadFile(h, &machine, sizeof(uint16_t), &read, NULL) || read != sizeof(uint16_t)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return static_cast<uint16_t>(machine & 0xFFFF);
}

// Copy using wide APIs from UTF-8 input paths
static BOOL copyFileUtf8(const std::string &src, const std::string &dst) {
    std::wstring wsrc = Utils::utf8_to_wstring(src);
    std::wstring wdst = Utils::utf8_to_wstring(dst);
    return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE);
}

// Helper function to read hash info from file
static HashInfo readHashInfo(const std::string &path) {
    HashInfo      info = {"", "", "", ""};
    std::ifstream file(path);
    if (file.is_open()) {
        std::getline(file, info.hash);
        std::getline(file, info.version);
        std::getline(file, info.mode);
        std::getline(file, info.format);
    }
    return info;
}
