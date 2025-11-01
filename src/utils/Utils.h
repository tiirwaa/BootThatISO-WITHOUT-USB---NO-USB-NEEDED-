#ifndef UTILS_H
#define UTILS_H

#include <string>

namespace Utils {
    std::string exec(const char* cmd);
    long long getFileSize(const std::string& filePath);
    long long getDirectorySize(const std::string& dirPath);
    std::string getExeDirectory();
    std::wstring utf8_to_wstring(const std::string& utf8);
    std::string wstring_to_utf8(const std::wstring& wstr);
    std::string ansi_to_utf8(const std::string& ansi);
    std::string calculateMD5(const std::string& filePath);
}

#endif // UTILS_H