#ifndef RAMDISKBOOTSTRATEGY_H
#define RAMDISKBOOTSTRATEGY_H

#include "BootStrategy.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include <windows.h>
#include <fstream>
#include <string>

class RamdiskBootStrategy : public BootStrategy {
public:
    std::string getBCDLabel() const override {
        return "ISOBOOT_RAM";
    }

    void configureBCD(const std::string& guid, const std::string& dataDevice, const std::string& espDevice, const std::string& efiPath) override {
        const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";
        // ramdiskPath: use the copied ISO file on the data partition (drive-letter form)
        std::string ramdiskPath = "[" + dataDevice + "]\\iso.iso";

        // Attempt 1: Resolve the DOS device name (\Device\HarddiskVolumeN) from the drive letter
        std::string deviceFormRamdisk;
        try {
            // Ensure a form accepted by QueryDosDeviceW (e.g. "Z:")
            std::wstring wDriveLetter = Utils::utf8_to_wstring(dataDevice);
            if (wDriveLetter.size() == 1) {
                wDriveLetter.push_back(L':');
            } else if (wDriveLetter.size() > 1 && wDriveLetter.back() != L':') {
                wDriveLetter.push_back(L':');
            }
            WCHAR deviceName[MAX_PATH] = {0};
            if (!wDriveLetter.empty() && QueryDosDeviceW(wDriveLetter.c_str(), deviceName, MAX_PATH) != 0) {
                // deviceName e.g. "\\Device\\HarddiskVolume3"
                std::string deviceNameStr = Utils::wstring_to_utf8(std::wstring(deviceName));
                deviceFormRamdisk = deviceNameStr + "\\iso.iso";
            }
        } catch (...) {
            // ignore and fallback to GUID/letter based forms
        }

        // Attempt 2: Resolve the drive letter to a Volume GUID and append it (recommended for pre-boot resolution)
        std::string ramdiskPathWithGuid = ramdiskPath;
        try {
            std::wstring wDrive = Utils::utf8_to_wstring(dataDevice);
            if (wDrive.empty() || wDrive.back() != L'\\') wDrive += L'\\';
            WCHAR volName[MAX_PATH] = {0};
            if (GetVolumeNameForVolumeMountPointW(wDrive.c_str(), volName, MAX_PATH)) {
                std::wstring wVol(volName);
                size_t lbrace = wVol.find(L'{');
                size_t rbrace = wVol.find(L'}');
                if (lbrace != std::wstring::npos && rbrace != std::wstring::npos && rbrace > lbrace) {
                    std::wstring wGuid = wVol.substr(lbrace, rbrace - lbrace + 1); // includes braces
                    std::string guidStr = Utils::wstring_to_utf8(wGuid);
                    // append comma + GUID (no prefix)
                    // ramdiskPathWithGuid += "," + guidStr;
                }
            }
        } catch (...) {
            // best-effort: if resolving GUID fails, continue with drive-letter-only form
        }

        // Prepare BCD commands: use ramdisk with boot.wim
        std::string cmd1 = BCD_CMD + " /set " + guid + " device ramdisk=" + ramdiskPathWithGuid;
        std::string cmd2 = BCD_CMD + " /set " + guid + " osdevice ramdisk=" + ramdiskPathWithGuid;

    // Prefer EFI fallback files (BOOTX64) for ramdisk boot if present on the ESP.
    std::string selectedEfiPath = efiPath; // efiPath may be relative (starting with '\')
    try {
        std::vector<std::string> candidates = {
            espDevice + "\\EFI\\BOOT\\BOOTX64.EFI",
            espDevice + "\\EFI\\boot\\BOOTX64.EFI",
            espDevice + "\\EFI\\boot\\bootx64.efi",
            espDevice + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
            espDevice + "\\EFI\\microsoft\\boot\\bootmgfw.efi"
        };
        for (const auto& c : candidates) {
            if (GetFileAttributesA(c.c_str()) != INVALID_FILE_ATTRIBUTES) {
                // make relative path for BCD (remove drive letter and colon if present)
                if (c.size() > espDevice.size()) {
                    selectedEfiPath = c.substr(espDevice.size());
                } else {
                    selectedEfiPath = c;
                }
                break;
            }
        }
    } catch (...) {
        // ignore and use provided efiPath
    }

    std::string cmd3 = BCD_CMD + " /set " + guid + " path \"\\EFI\\BOOT\\BOOTX64.EFI\"";

    // Set inheritance for OSLOADER entry to get necessary boot settings
    std::string cmdInherit = BCD_CMD + " /set " + guid + " inherit {bootloadersettings}";

    // Note: ramdisksdipath is not needed for Windows 10/11 ISOs - SDI is found automatically in ramdisk
    // std::string cmdSdi = BCD_CMD + " /set " + guid + " ramdisksdipath \\boot\\boot.sdi";

        // Log commands for debugging
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing BCD commands for RamdiskBootStrategy (ISO ramdisk mode):" << std::endl;
            logFile << "Note: Using ISO file for RAMDISK boot with BOOTX64.EFI" << std::endl;

            // Execute inheritance first
            logFile << "  " << cmdInherit << std::endl;
            std::string resultInherit = Utils::exec(cmdInherit.c_str());
            logFile << "  Result: " << resultInherit << std::endl;

            logFile << "  " << cmd1 << std::endl;
            std::string result1 = Utils::exec(cmd1.c_str());
            logFile << "  Result: " << result1 << std::endl;

            logFile << "  " << cmd2 << std::endl;
            std::string result2 = Utils::exec(cmd2.c_str());
            logFile << "  Result: " << result2 << std::endl;

            logFile << "  " << cmd3 << std::endl;
            std::string result3 = Utils::exec(cmd3.c_str());
            logFile << "  Result: " << result3 << std::endl;

            // Removed ramdisksdipath setting as it's not supported for OSLOADER entries
            // logFile << "  " << cmdSdi << std::endl;
            // std::string resultSdi = Utils::exec(cmdSdi.c_str());
            // logFile << "  Result: " << resultSdi << std::endl;

            // Verify ramdisk parameters were set correctly
            std::string verifyCmd = BCD_CMD + " /enum " + guid;
            std::string verifyResult = Utils::exec(verifyCmd.c_str());
            logFile << "BCD entry verification after ramdisk setup:\n" << verifyResult << std::endl;
            
            // Check if essential ramdisk parameters are present
            bool hasDevice = verifyResult.find("device") != std::string::npos && verifyResult.find("ramdisk") != std::string::npos;
            bool hasOsDevice = verifyResult.find("osdevice") != std::string::npos && verifyResult.find("ramdisk") != std::string::npos;
            bool hasPath = verifyResult.find("path") != std::string::npos && verifyResult.find("BOOTX64.EFI") != std::string::npos;
            
            if (hasDevice && hasOsDevice && hasPath) {
                logFile << "SUCCESS: Essential ramdisk parameters configured correctly\n";
            } else {
                logFile << "WARNING: Some ramdisk parameters may be missing!\n";
                logFile << "  device+ramdisk: " << (hasDevice ? "YES" : "NO") << "\n";
                logFile << "  osdevice+ramdisk: " << (hasOsDevice ? "YES" : "NO") << "\n";
                logFile << "  path+BOOTX64: " << (hasPath ? "YES" : "NO") << "\n";
            }

            logFile.close();
        }
    }
};

#endif // RAMDISKBOOTSTRATEGY_H