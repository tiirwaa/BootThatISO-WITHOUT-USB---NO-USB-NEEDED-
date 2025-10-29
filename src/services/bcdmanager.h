#ifndef BCDMANAGER_H
#define BCDMANAGER_H

#include <windows.h>
#include <string>
#include "../models/BootStrategy.h"

class BCDManager
{
private:
    BCDManager();
    ~BCDManager();
    BCDManager(const BCDManager&) = delete;
    BCDManager& operator=(const BCDManager&) = delete;

public:
    static BCDManager& getInstance();

    // mode: "RAMDISK" or "EXTRACTED" - affects how BCD entries are created (ramdisk-specific settings when RAMDISK)
    std::string configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, BootStrategy& strategy);
    bool restoreBCD();

private:
    WORD GetMachineType(const std::string& filePath);
};

#endif // BCDMANAGER_H