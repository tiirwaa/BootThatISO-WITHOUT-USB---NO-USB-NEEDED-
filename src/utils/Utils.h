#ifndef UTILS_H
#define UTILS_H

#include <string>

namespace Utils {
    std::string exec(const char* cmd);
    long long getFileSize(const std::string& filePath);
    std::string getExeDirectory();
    std::wstring utf8_to_wstring(const std::string& utf8);
    std::string wstring_to_utf8(const std::wstring& wstr);
}

#endif // UTILS_H