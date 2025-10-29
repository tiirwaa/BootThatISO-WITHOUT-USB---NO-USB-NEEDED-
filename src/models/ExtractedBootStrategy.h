#ifndef EXTRACTEDBOOTSTRATEGY_H
#define EXTRACTEDBOOTSTRATEGY_H

#include "BootStrategy.h"
#include "../utils/Utils.h"

class ExtractedBootStrategy : public BootStrategy {
public:
    std::string getBCDLabel() const override {
        return "ISOBOOT";
    }

    void configureBCD(const std::string& guid, const std::string& dataDevice, const std::string& espDevice, const std::string& efiPath) override {
        std::string cmd1 = "bcdedit /set " + guid + " device partition=" + espDevice;
        std::string cmd2 = "bcdedit /set " + guid + " osdevice partition=" + dataDevice;
        std::string cmd3 = "bcdedit /set " + guid + " path \\EFI\\Microsoft\\Boot\\bootmgfw.efi";

        Utils::exec(cmd1.c_str());
        Utils::exec(cmd2.c_str());
        Utils::exec(cmd3.c_str());
    }
};

#endif // EXTRACTEDBOOTSTRATEGY_H