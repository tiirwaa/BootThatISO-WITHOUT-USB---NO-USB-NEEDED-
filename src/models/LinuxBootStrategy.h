#ifndef LINUXBOOTSTRATEGY_H
#define LINUXBOOTSTRATEGY_H

#include "BootStrategy.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include <fstream>
#include <string>

class LinuxBootStrategy : public BootStrategy {
public:
    std::string getBCDLabel() const override {
        return "Linux ISO Boot (Firmware)";
    }

    std::string getType() const override {
        return "linux";
    }

    void configureBCD(const std::string &guid, const std::string &dataDevice, const std::string &espDevice,
                      const std::string &efiPath) override {
        const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";

        // For Linux ISOs, we cannot use Windows Boot Manager to execute Linux EFI bootloaders
        // Instead, we'll configure the firmware boot manager directly using bcdedit /set {fwbootmgr}

        // Create a firmware boot entry that points directly to the Linux EFI bootloader
        std::string fwGuid = createFirmwareBootEntry(espDevice, efiPath);

        // Configure BCD to use the firmware boot manager instead of Windows boot manager
        std::string cmd1 = BCD_CMD + " /set " + guid + " device partition=" + espDevice;
        std::string cmd2 = BCD_CMD + " /set " + guid + " osdevice partition=" + espDevice;
        std::string cmd3 = BCD_CMD + " /set " + guid + " path \\EFI\\Microsoft\\Boot\\bootmgfw.efi";
        std::string cmd4 = BCD_CMD + " /set " + guid + " systemroot \\Windows";
        std::string cmd5 = BCD_CMD + " /set " + guid + " detecthal Yes";
        std::string cmd6 = BCD_CMD + " /set " + guid + " winpe Yes";
        std::string cmd7 = BCD_CMD + " /set " + guid + " ems No";

        // Log commands for debugging
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string   logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing BCD commands for LinuxBootStrategy:" << std::endl;
            logFile << "  Firmware GUID: " << fwGuid << std::endl;
            logFile << "  Target EFI path: " << efiPath << std::endl;
            logFile << "  " << cmd1 << std::endl;
            std::string result1 = Utils::exec(cmd1.c_str());
            logFile << "  Result: " << result1 << std::endl;

            logFile << "  " << cmd2 << std::endl;
            std::string result2 = Utils::exec(cmd2.c_str());
            logFile << "  Result: " << result2 << std::endl;

            logFile << "  " << cmd3 << std::endl;
            std::string result3 = Utils::exec(cmd3.c_str());
            logFile << "  Result: " << result3 << std::endl;

            logFile.close();
        }

        Utils::exec(cmd1.c_str());
        Utils::exec(cmd2.c_str());
        Utils::exec(cmd3.c_str());
        Utils::exec(cmd4.c_str());
        Utils::exec(cmd5.c_str());
        Utils::exec(cmd6.c_str());
        Utils::exec(cmd7.c_str());

        // Try to add to firmware boot order
        if (!fwGuid.empty()) {
            std::string fwCmd = BCD_CMD + " /set {fwbootmgr} displayorder " + fwGuid + " /addfirst";
            Utils::exec(fwCmd.c_str());
            if (logFile.is_open()) {
                logFile << "  " << fwCmd << std::endl;
                std::string fwResult = Utils::exec(fwCmd.c_str());
                logFile << "  Result: " << fwResult << std::endl;
            }
        }
    }

private:
    std::string createFirmwareBootEntry(const std::string &espDevice, const std::string &targetEfiPath) {
        const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";

        // Create a firmware boot entry that points to the Linux EFI bootloader
        std::string targetPath  = espDevice + targetEfiPath;
        std::string description = "Linux ISO Boot";

        // Use bcdedit to create a firmware boot entry
        std::string createCmd    = BCD_CMD + " /create /application firmware /d \"" + description + "\"";
        std::string createResult = Utils::exec(createCmd.c_str());

        // Extract GUID from result
        std::string fwGuid;
        size_t      pos = createResult.find("{");
        if (pos != std::string::npos) {
            size_t end = createResult.find("}", pos);
            if (end != std::string::npos) {
                fwGuid = createResult.substr(pos, end - pos + 1);
            }
        }

        if (!fwGuid.empty()) {
            // Configure the firmware entry to point to our Linux EFI bootloader
            std::string setPathCmd = BCD_CMD + " /set " + fwGuid + " path " + targetEfiPath;
            Utils::exec(setPathCmd.c_str());

            // Log the creation
            std::string logDir = Utils::getExeDirectory() + "logs";
            CreateDirectoryA(logDir.c_str(), NULL);
            std::string   logFilePath = logDir + "\\linux_boot_log.txt";
            std::ofstream logFile(logFilePath.c_str(), std::ios::app);
            if (logFile) {
                logFile << "Firmware boot entry created: " << fwGuid << std::endl;
                logFile << "Target path: " << targetPath << std::endl;
                logFile << "BCD path: " << targetEfiPath << std::endl;
                logFile.close();
            }
        }

        return fwGuid;
    }
};

#endif // LINUXBOOTSTRATEGY_H