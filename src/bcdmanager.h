#ifndef BCDMANAGER_H
#define BCDMANAGER_H

#include <windows.h>
#include <string>

class BCDManager
{
public:
    BCDManager();
    ~BCDManager();

    std::string configureBCD(const std::string& driveLetter, bool isWindowsISO);
    bool restoreBCD();

private:
    WORD GetMachineType(const std::string& filePath);
};

#endif // BCDMANAGER_H