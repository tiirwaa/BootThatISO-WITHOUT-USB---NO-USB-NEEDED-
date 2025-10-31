#ifndef EXTRACTEDBOOTSTRATEGY_H
#define EXTRACTEDBOOTSTRATEGY_H

#include "BootStrategy.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include <fstream>

class ExtractedBootStrategy : public BootStrategy {
public:
    std::string getBCDLabel() const override {
        return EXTRACTED_BCD_LABEL;
    }

    std::string getType() const override {
        return "extracted";
    }

    void configureBCD(const std::string& guid, const std::string& dataDevice, const std::string& espDevice, const std::string& efiPath) override {
        const std::string BCD_CMD = "C:\\Windows\\System32\\bcdedit.exe";
        // dataDevice and espDevice are expected to be drive letters like "Z:" and "Y:"
        std::string cmd1 = BCD_CMD + " /set " + guid + " device partition=" + dataDevice;
        std::string cmd2 = BCD_CMD + " /set " + guid + " osdevice partition=" + dataDevice;
        // For extracted mode, point to bootmgr on the data partition
        std::string cmd3 = BCD_CMD + " /set " + guid + " path \"\\bootmgr\"";

        // Log commands for debugging
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing BCD commands for ExtractedBootStrategy:" << std::endl;
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
    }
};

#endif // EXTRACTEDBOOTSTRATEGY_H