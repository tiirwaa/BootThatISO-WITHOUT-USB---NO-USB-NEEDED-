#ifndef RAMDISKBOOTSTRATEGY_H
#define RAMDISKBOOTSTRATEGY_H

#include "BootStrategy.h"
#include "../utils/Utils.h"

class RamdiskBootStrategy : public BootStrategy {
public:
    std::string getBCDLabel() const override {
        return "ISOBOOT_RAM";
    }

    void configureBCD(const std::string& guid, const std::string& dataDevice, const std::string& espDevice, const std::string& efiPath) override {
        // dataDevice is expected as a drive letter like "Z:" where the ISO file lives
        std::string ramdiskPath = "[" + dataDevice + "]\\iso.iso";
        std::string cmd1 = "bcdedit /set " + guid + " device ramdisk=" + ramdiskPath;
        std::string cmd2 = "bcdedit /set " + guid + " osdevice ramdisk=" + ramdiskPath;
        std::string cmd3 = "bcdedit /set " + guid + " path \"" + efiPath + "\"";

        Utils::exec(cmd1.c_str());
        Utils::exec(cmd2.c_str());
        Utils::exec(cmd3.c_str());

        // Additional ramdisk options
        std::string cmdRamdiskSdiDevice = "bcdedit /set " + guid + " ramdisksdidevice partition=" + dataDevice;
        std::string cmdRamdiskSdiPath = "bcdedit /set " + guid + " ramdisksdipath \\iso.iso";
        Utils::exec(cmdRamdiskSdiDevice.c_str());
        Utils::exec(cmdRamdiskSdiPath.c_str());
    }
};

#endif // RAMDISKBOOTSTRATEGY_H