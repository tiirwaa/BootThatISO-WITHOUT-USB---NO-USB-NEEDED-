#pragma once

#include <string>

class ISOMounter {
public:
    ISOMounter();
    ~ISOMounter();

    bool mountISO(const std::string& isoPath, std::string& driveLetter);
    bool unmountISO(const std::string& isoPath);

private:
    std::string exec(const char* cmd);
    const char* getTimestamp();
};