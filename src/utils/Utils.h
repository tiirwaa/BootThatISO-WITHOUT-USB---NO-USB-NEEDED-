#ifndef UTILS_H
#define UTILS_H

#include <string>

namespace Utils {
std::string exec(const char *cmd);
// Execute a command and capture stdout; return process exit code, and set output.
int execWithExitCode(const char *cmd, std::string &output);
// Returns full path to dism.exe (typically %SystemRoot%\System32\dism.exe)
std::string  getDismPath();
long long    getFileSize(const std::string &filePath);
long long    getDirectorySize(const std::string &dirPath);
std::string  getExeDirectory();
std::wstring utf8_to_wstring(const std::string &utf8);
std::string  wstring_to_utf8(const std::wstring &wstr);
std::string  ansi_to_utf8(const std::string &ansi);
std::string  calculateMD5(const std::string &filePath);
std::string  toLower(const std::string &str);
bool         matchesPattern(const std::string &str, const std::string &pattern);
} // namespace Utils

#endif // UTILS_H