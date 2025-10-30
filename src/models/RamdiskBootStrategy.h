#ifndef RAMDISKBOOTSTRATEGY_H
#define RAMDISKBOOTSTRATEGY_H

#include "BootStrategy.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include <fstream>

class RamdiskBootStrategy : public BootStrategy {
public:
    std::string getBCDLabel() const override {
        return "ISOBOOT_RAM";
    }

    void configureBCD(const std::string& guid, const std::string& dataDevice, const std::string& espDevice, const std::string& efiPath) override {
        const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";
        // dataDevice is expected as a drive letter like "Z:" where the ISO file lives
    // ramdiskPath: use the copied ISO file on the data partition
    std::string ramdiskPath = "[" + dataDevice + "]\\iso.iso";
    std::string cmd1 = BCD_CMD + " /set " + guid + " device ramdisk=" + ramdiskPath;
    std::string cmd2 = BCD_CMD + " /set " + guid + " osdevice ramdisk=" + ramdiskPath;
    std::string cmd3 = BCD_CMD + " /set " + guid + " path \"" + efiPath + "\"";

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
            logFile << "Executing BCD commands for RamdiskBootStrategy:" << std::endl;
            logFile << "Note: ramdisksdipath parameter is not needed in Windows 10/11 - SDI is found automatically in ramdisk" << std::endl;

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
            bool hasPath = verifyResult.find("path") != std::string::npos;
            
            if (hasDevice && hasOsDevice && hasPath) {
                logFile << "SUCCESS: Essential ramdisk parameters configured correctly\n";
            } else {
                logFile << "WARNING: Some ramdisk parameters may be missing!\n";
                logFile << "  device+ramdisk: " << (hasDevice ? "YES" : "NO") << "\n";
                logFile << "  osdevice+ramdisk: " << (hasOsDevice ? "YES" : "NO") << "\n";
                logFile << "  path: " << (hasPath ? "YES" : "NO") << "\n";
            }

            logFile.close();
        }
    }
};

#endif // RAMDISKBOOTSTRATEGY_H