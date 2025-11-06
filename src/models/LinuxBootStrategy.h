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
        return "Linux ISO Boot";
    }

    std::string getType() const override {
        return "linux";
    }

    void configureBCD(const std::string &guid, const std::string &dataDevice, const std::string &espDevice,
                      const std::string &efiPath) override {
        const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";

        // For Linux ISOs, create a BCD entry that points directly to the Linux EFI bootloader
        // We'll configure it as an OSLOADER entry but point it to the Linux EFI file directly

        // Configure BCD to point directly to the Linux EFI bootloader
        std::string cmd1 = BCD_CMD + " /set " + guid + " device partition=" + espDevice;
        std::string cmd2 = BCD_CMD + " /set " + guid + " path " + efiPath;
        std::string cmd3 = BCD_CMD + " /set " + guid + " description \"Linux ISO Boot\"";
        std::string cmd4 = BCD_CMD + " /set " + guid + " systemroot \\EFI";
        std::string cmd5 = BCD_CMD + " /set " + guid + " detecthal No";
        std::string cmd6 = BCD_CMD + " /set " + guid + " winpe No";
        std::string cmd7 = BCD_CMD + " /set " + guid + " ems No";

        // Log commands for debugging
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string   logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing BCD commands for LinuxBootStrategy:" << std::endl;
            logFile << "  GUID: " << guid << std::endl;
            logFile << "  Target EFI path: " << efiPath << std::endl;
            logFile << "  ESP device: " << espDevice << std::endl;
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

        // Add to display order so it appears in boot menu
        std::string displayCmd = BCD_CMD + " /displayorder " + guid + " /addfirst";
        Utils::exec(displayCmd.c_str());

        // Set as default to make it boot automatically
        std::string defaultCmd = BCD_CMD + " /default " + guid;
        Utils::exec(defaultCmd.c_str());
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