#ifndef EXTRACTEDBOOTSTRATEGY_H
#define EXTRACTEDBOOTSTRATEGY_H

#include "BootStrategy.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include <fstream>

class ExtractedBootStrategy : public BootStrategy {
public:
    std::string getBCDLabel() const override {
        return "ISOBOOT";
    }

    void configureBCD(const std::string& guid, const std::string& dataDevice, const std::string& espDevice, const std::string& efiPath) override {
        // dataDevice and espDevice are expected to be drive letters like "Z:" and "Y:"
        std::string cmd1 = "bcdedit /set " + guid + " device partition=" + espDevice;
        std::string cmd2 = "bcdedit /set " + guid + " osdevice partition=" + dataDevice;
        // Quote the path to handle spaces or special characters
        std::string cmd3 = "bcdedit /set " + guid + " path \"" + efiPath + "\"";

        // Log commands for debugging
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing: " << cmd1 << std::endl;
            logFile << "Executing: " << cmd2 << std::endl;
            logFile << "Executing: " << cmd3 << std::endl;
            logFile.close();
        }

        Utils::exec(cmd1.c_str());
        Utils::exec(cmd2.c_str());
        Utils::exec(cmd3.c_str());
    }
};

#endif // EXTRACTEDBOOTSTRATEGY_H