#ifndef ISOCOPYMANAGER_H
#define ISOCOPYMANAGER_H

#include <string>

class ISOCopyManager
{
public:
    ISOCopyManager();
    ~ISOCopyManager();

    bool copyISO(const std::string& isoPath, const std::string& destPath);
};

#endif // ISOCOPYMANAGER_H