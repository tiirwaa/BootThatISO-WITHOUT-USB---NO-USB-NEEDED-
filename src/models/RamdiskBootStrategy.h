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
        return RAMDISK_BCD_LABEL;
    }

    std::string getType() const override {
        return "ramdisk";
    }

    void configureBCD(const std::string &guid, const std::string &dataDevice, const std::string &espDevice,
                      const std::string &efiPath) override {
        const std::string BCD_CMD          = "C:\\Windows\\System32\\bcdedit.exe";
        const std::string ramdiskOptionsId = "{ramdiskoptions}";
    const std::string bootWimRelative  = "\\sources\\boot.wim";
    const std::string sdiRelative      = "\\boot\\boot.sdi";

    // Choose correct OS loader depending on firmware type
    // - UEFI:   \Windows\System32\Boot\winload.efi
    // - Legacy: \Windows\System32\winload.exe
    std::string         winloadPath;
    FIRMWARE_TYPE       fwType = FirmwareTypeUnknown;
    BOOL                gotFw  = GetFirmwareType(&fwType);
    bool                isUEFI = gotFw && (fwType == FirmwareTypeUefi);
    winloadPath                 = isUEFI ? "\\Windows\\System32\\Boot\\winload.efi"
                         : "\\Windows\\System32\\winload.exe";

        std::string ramdiskValue = "[" + dataDevice + "]" + bootWimRelative + "," + ramdiskOptionsId;

        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::string   logFilePath = logDir + "\\" + BCD_CONFIG_LOG_FILE;
        std::ofstream logFile(logFilePath.c_str(), std::ios::app);
        if (logFile) {
            logFile << "Executing BCD commands for RamdiskBootStrategy (boot.wim ramdisk mode):" << std::endl;
        }

        auto execAndLog = [&](const std::string &cmd, bool allowExists = false) {
            if (logFile) {
                logFile << "  " << cmd << std::endl;
            }
            std::string result = Utils::exec(cmd.c_str());
            if (logFile) {
                logFile << "  Result: " << result << std::endl;
            }
            if (allowExists) {
                if (result.find("already exists") != std::string::npos ||
                    result.find("ya existe") != std::string::npos) {
                    return result;
                }
            }
            return result;
        };

        execAndLog(BCD_CMD + " /create " + ramdiskOptionsId, true);
        execAndLog(BCD_CMD + " /set " + ramdiskOptionsId + " ramdisksdidevice partition=" + dataDevice);
        execAndLog(BCD_CMD + " /set " + ramdiskOptionsId + " ramdisksdipath " + sdiRelative);

        execAndLog(BCD_CMD + " /set " + guid + " inherit {bootloadersettings}");
        execAndLog(BCD_CMD + " /set " + guid + " device ramdisk=" + ramdiskValue);
        execAndLog(BCD_CMD + " /set " + guid + " osdevice ramdisk=" + ramdiskValue);
        execAndLog(BCD_CMD + " /set " + guid + " path " + winloadPath);
        execAndLog(BCD_CMD + " /set " + guid + " systemroot \\Windows");
        execAndLog(BCD_CMD + " /set " + guid + " winpe yes");
        execAndLog(BCD_CMD + " /set " + guid + " detecthal yes");
        execAndLog(BCD_CMD + " /set " + guid + " ems no");

        std::string verifyCmd    = BCD_CMD + " /enum " + guid;
        std::string verifyResult = Utils::exec(verifyCmd.c_str());
        if (logFile) {
            logFile << "BCD entry verification after ramdisk setup:\n" << verifyResult << std::endl;
        }

        std::string verifyRamdiskOptions = Utils::exec((BCD_CMD + " /enum " + ramdiskOptionsId).c_str());
        if (logFile) {
            logFile << "Ramdisk options object state:\n" << verifyRamdiskOptions << std::endl;
        }

        bool hasDevice =
            verifyResult.find("ramdisk=") != std::string::npos && verifyResult.find("boot.wim") != std::string::npos;
    bool hasPath              = verifyResult.find(winloadPath) != std::string::npos;
        bool hasRamdiskOptionsRef = verifyResult.find(ramdiskOptionsId) != std::string::npos;
        bool hasSdi               = verifyRamdiskOptions.find("ramdisksdipath") != std::string::npos &&
                      verifyRamdiskOptions.find("boot\\boot.sdi") != std::string::npos;

        if (logFile) {
            if (hasDevice && hasPath && hasRamdiskOptionsRef && hasSdi) {
        logFile << "SUCCESS: Ramdisk BCD entry configured with boot.wim and "
            << (isUEFI ? "winload.efi" : "winload.exe") << std::endl;
            } else {
                logFile << "WARNING: Ramdisk BCD entry may be missing critical parameters" << std::endl;
                logFile << "  device includes boot.wim: " << (hasDevice ? "YES" : "NO") << std::endl;
        logFile << "  path uses " << (isUEFI ? "winload.efi" : "winload.exe") << ": "
            << (hasPath ? "YES" : "NO") << std::endl;
                logFile << "  references {ramdiskoptions}: " << (hasRamdiskOptionsRef ? "YES" : "NO") << std::endl;
                logFile << "  ramdisksdipath configured: " << (hasSdi ? "YES" : "NO") << std::endl;
            }
            logFile.close();
        }
    }
};

#endif // RAMDISKBOOTSTRATEGY_H
