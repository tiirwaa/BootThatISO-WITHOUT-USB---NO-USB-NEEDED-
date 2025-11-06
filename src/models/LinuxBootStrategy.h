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

        // For Linux ISOs, use our EFI chainloader that will find and execute the correct Linux bootloader
        // This resolves the 0xc000007b compatibility error by using a proper EFI intermediary

        // First, copy the correct Linux bootloader to the ESP as our "chainloader"
        copyChainloaderToESP(espDevice, efiPath);

        // Configure BCD to point to our chainloader.efi (which is actually the Linux bootloader)
        std::string chainloaderPath = "\\EFI\\BootThatISO\\chainloader.efi";
        std::string cmd1            = BCD_CMD + " /set " + guid + " device partition=" + espDevice;
        std::string cmd2            = BCD_CMD + " /set " + guid + " path " + chainloaderPath;
        std::string cmd3            = BCD_CMD + " /set " + guid + " description \"Linux ISO Boot\"";
        std::string cmd4            = BCD_CMD + " /set " + guid + " systemroot \\EFI";
        std::string cmd5            = BCD_CMD + " /set " + guid + " detecthal No";
        std::string cmd6            = BCD_CMD + " /set " + guid + " winpe No";
        std::string cmd7            = BCD_CMD + " /set " + guid + " ems No";

        // Log commands for debugging
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string   logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing BCD commands for LinuxBootStrategy (using EFI chainloader):" << std::endl;
            logFile << "  GUID: " << guid << std::endl;
            logFile << "  Chainloader EFI path: " << chainloaderPath << std::endl;
            logFile << "  ESP device: " << espDevice << std::endl;
            logFile << "  Original Linux EFI path: " << efiPath << std::endl;
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

        // Note: displayorder and default are now handled by BCDManager for consistency
        // This allows Linux entries to appear in the Windows Boot Manager menu like other strategies
    }

private:
    void copyChainloaderToESP(const std::string &espDevice, const std::string &efiPath) {
        // Copy GRUB EFI as chainloader and generate grub.cfg for ISO booting

        std::string grubSrc            = Utils::getExeDirectory() + "chainloader\\grubx64.efi";
        std::string chainloaderDestDir = espDevice + "EFI\\BootThatISO";
        std::string chainloaderDest    = chainloaderDestDir + "\\chainloader.efi";
        std::string cfgDest            = chainloaderDestDir + "\\grub.cfg";

        // Create destination directory
        CreateDirectoryA(chainloaderDestDir.c_str(), NULL);

        // Copy GRUB EFI as chainloader
        if (CopyFileA(grubSrc.c_str(), chainloaderDest.c_str(), FALSE)) {
            // Generate grub.cfg
            generateGrubCfg(cfgDest, espDevice, efiPath);

            // Log success
            std::string logDir = Utils::getExeDirectory() + "logs";
            CreateDirectoryA(logDir.c_str(), NULL);
            std::string   logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
            std::ofstream logFile(logFilePath.c_str(), std::ios::app);
            if (logFile) {
                logFile << "GRUB EFI chainloader copied to ESP:" << std::endl;
                logFile << "  Source: " << grubSrc << std::endl;
                logFile << "  Destination: " << chainloaderDest << std::endl;
                logFile << "  GRUB cfg: " << cfgDest << std::endl;
                logFile.close();
            }
        } else {
            // Log error
            std::string logDir = Utils::getExeDirectory() + "logs";
            CreateDirectoryA(logDir.c_str(), NULL);
            std::string   logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
            std::ofstream logFile(logFilePath.c_str(), std::ios::app);
            if (logFile) {
                logFile << "Failed to copy GRUB EFI chainloader to ESP:" << std::endl;
                logFile << "  Source: " << grubSrc << std::endl;
                logFile << "  Destination: " << chainloaderDest << std::endl;
                logFile << "  Error: " << GetLastError() << std::endl;
                logFile.close();
            }
        }
    }

    void generateGrubCfg(const std::string &cfgPath, const std::string &espDevice, const std::string &efiPath) {
        // Generate GRUB config for booting Linux ISO
        // Assume ISO is on the same partition as ESP, adjust path accordingly
        std::string isoPath = "\\ISOS\\linux.iso"; // Placeholder, should be dynamic based on actual ISO location

        std::ofstream cfgFile(cfgPath.c_str());
        if (cfgFile) {
            cfgFile << "set isofile=\"" << isoPath << "\"" << std::endl;
            cfgFile << "loopback loop (hd0,gpt3)$isofile" << std::endl; // Adjust partition as needed
            cfgFile << "linux (loop)/casper/vmlinuz boot=casper iso-scan/filename=$isofile quiet splash" << std::endl;
            cfgFile << "initrd (loop)/casper/initrd" << std::endl;
            cfgFile << "boot" << std::endl;
            cfgFile.close();
        }
    }

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