#ifndef BCDMANAGER_H
#define BCDMANAGER_H

#include <string>

class BCDManager
{
public:
    BCDManager();
    ~BCDManager();

    bool configureBCD(const std::string& driveLetter);
    bool restoreBCD();
};

#endif // BCDMANAGER_H