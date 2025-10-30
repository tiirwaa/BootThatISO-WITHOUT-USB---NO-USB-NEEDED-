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
        // dataDevice is expected as a drive letter like "Z:" where the ISO file lives
    // ramdiskPath: use the copied ISO file on the data partition
    std::string ramdiskPath = "[" + dataDevice + "]\\iso.iso";
    std::string cmd1 = "bcdedit /set " + guid + " device ramdisk=" + ramdiskPath;
    std::string cmd2 = "bcdedit /set " + guid + " osdevice ramdisk=" + ramdiskPath;
    std::string cmd3 = "bcdedit /set " + guid + " path \"" + efiPath + "\"";

    // Additional ramdisk options: point ramdisksdidevice to the data partition and ramdisksdipath to the standard boot SDI inside the ISO
    // For Windows install ISOs, the SDI is typically at \boot\boot.sdi
    std::string cmdRamdiskSdiDevice = "bcdedit /set " + guid + " ramdisksdidevice partition=" + dataDevice;
    std::string cmdRamdiskSdiPath = "bcdedit /set " + guid + " ramdisksdipath \\boot\\boot.sdi";

        // Log commands for debugging
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing BCD commands for RamdiskBootStrategy:" << std::endl;
            logFile << "  " << cmd1 << std::endl;
            std::string result1 = Utils::exec(cmd1.c_str());
            logFile << "  Result: " << (result1.find("error") != std::string::npos ? "ERROR: " : "OK") << result1 << std::endl;

            logFile << "  " << cmd2 << std::endl;
            std::string result2 = Utils::exec(cmd2.c_str());
            logFile << "  Result: " << (result2.find("error") != std::string::npos ? "ERROR: " : "OK") << result2 << std::endl;

            logFile << "  " << cmd3 << std::endl;
            std::string result3 = Utils::exec(cmd3.c_str());
            logFile << "  Result: " << (result3.find("error") != std::string::npos ? "ERROR: " : "OK") << result3 << std::endl;

            logFile << "  " << cmdRamdiskSdiDevice << std::endl;
            std::string result4 = Utils::exec(cmdRamdiskSdiDevice.c_str());
            logFile << "  Result: " << (result4.find("error") != std::string::npos ? "ERROR: " : "OK") << result4 << std::endl;

            logFile << "  " << cmdRamdiskSdiPath << std::endl;
            std::string result5 = Utils::exec(cmdRamdiskSdiPath.c_str());
            logFile << "  Result: " << (result5.find("error") != std::string::npos ? "ERROR: " : "OK") << result5 << std::endl;

            logFile.close();
        }

        Utils::exec(cmd1.c_str());
        Utils::exec(cmd2.c_str());
        Utils::exec(cmd3.c_str());
        Utils::exec(cmdRamdiskSdiDevice.c_str());
        Utils::exec(cmdRamdiskSdiPath.c_str());
    }
};

#endif // RAMDISKBOOTSTRATEGY_H