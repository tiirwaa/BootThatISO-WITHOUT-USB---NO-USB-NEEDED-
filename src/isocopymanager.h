#ifndef ISOCOPYMANAGER_H
#define ISOCOPYMANAGER_H

#include <string>

class ISOCopyManager
{
public:
    ISOCopyManager();
    ~ISOCopyManager();

    bool extractEFIFiles(const std::string& isoPath, const std::string& destPath);
    bool copyISOFile(const std::string& isoPath, const std::string& destPath);
private:
    std::string exec(const char* cmd);
};

#endif // ISOCOPYMANAGER_H