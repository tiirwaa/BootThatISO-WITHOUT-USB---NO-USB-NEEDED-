#ifndef HASHINFO_H
#define HASHINFO_H

#include <string>

struct HashInfo {
    std::string hash;
    std::string version;
    std::string mode;
    std::string format;
    std::string driversInjected; // "1" = drivers injected, "0" = no drivers
};

#endif // HASHINFO_H
