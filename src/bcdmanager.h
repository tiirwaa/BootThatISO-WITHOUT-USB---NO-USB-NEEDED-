#ifndef BCDMANAGER_H
#define BCDMANAGER_H

#include <windows.h>
#include <string>

class BCDManager
{
public:
    BCDManager();
    ~BCDManager();

    // mode: "RAMDISK" or "EXTRACTED" - affects how BCD entries are created (ramdisk-specific settings when RAMDISK)
    std::string configureBCD(const std::string& driveLetter, const std::string& espDriveLetter, const std::string& mode);
    bool restoreBCD();

private:
    WORD GetMachineType(const std::string& filePath);
};

#endif // BCDMANAGER_H