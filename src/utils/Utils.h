#ifndef UTILS_H
#define UTILS_H

#include <string>

namespace Utils {
    std::string exec(const char* cmd);
    long long getFileSize(const std::string& filePath);
}

#endif // UTILS_H