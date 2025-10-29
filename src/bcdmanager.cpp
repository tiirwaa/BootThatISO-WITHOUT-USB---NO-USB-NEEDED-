#include "bcdmanager.h"
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>

BCDManager::BCDManager()
{
}

BCDManager::~BCDManager()
{
}

std::string exec(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = _popen(cmd, "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof buffer, pipe) != NULL) {
        result += buffer;
    }
    _pclose(pipe);
    return result;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

bool BCDManager::configureBCD(const std::string& driveLetter)
{
    std::string output = exec("bcdedit /copy {current} /d \"EasyISOBoot\"");
    size_t pos = output.find("{");
    if (pos == std::string::npos) return false;
    size_t end = output.find("}", pos);
    if (end == std::string::npos) return false;
    std::string guid = output.substr(pos, end - pos + 1);

    std::string ramdiskGuid = "{7619dcc8-fafe-11d9-b411-000476eba25f}";
    std::string cmd1 = "bcdedit /set " + guid + " device ramdisk=[" + driveLetter + "]\\boot.iso," + ramdiskGuid;
    exec(cmd1.c_str());

    std::string cmd2 = "bcdedit /set " + guid + " osdevice ramdisk=[" + driveLetter + "]\\boot.iso," + ramdiskGuid;
    exec(cmd2.c_str());

    std::string cmd3 = "bcdedit /set " + guid + " path \\windows\\system32\\winload.exe";
    exec(cmd3.c_str());

    std::string cmd4 = "bcdedit /default " + guid;
    exec(cmd4.c_str());

    return true;
}

bool BCDManager::restoreBCD()
{
    std::string output = exec("bcdedit /enum");
    auto lines = split(output, '\n');
    std::string guid;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("description") != std::string::npos && lines[i].find("EasyISOBoot") != std::string::npos) {
            if (i > 0 && lines[i-1].find("identifier") != std::string::npos) {
                size_t pos = lines[i-1].find("{");
                if (pos != std::string::npos) {
                    size_t end = lines[i-1].find("}", pos);
                    if (end != std::string::npos) {
                        guid = lines[i-1].substr(pos, end - pos + 1);
                    }
                }
            }
            break;
        }
    }
    if (!guid.empty()) {
        std::string cmd1 = "bcdedit /delete " + guid;
        exec(cmd1.c_str());
        std::string cmd2 = "bcdedit /default {current}";
        exec(cmd2.c_str());
        return true;
    }
    return false;
}