#include "isomounter.h"
#include <windows.h>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <cctype>

ISOMounter::ISOMounter()
{
}

ISOMounter::~ISOMounter()
{
}

const char* ISOMounter::getTimestamp() {
    static char buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return buffer;
}

bool ISOMounter::mountISO(const std::string& isoPath, std::string& driveLetter)
{
    std::ofstream logFile("logs\\iso_mount.log", std::ios::app);
    logFile << getTimestamp() << "Mounting ISO: " << isoPath << std::endl;
    
    // Trim the isoPath to remove leading/trailing whitespace
    std::string trimmedIsoPath = isoPath;
    trimmedIsoPath.erase(trimmedIsoPath.begin(), std::find_if(trimmedIsoPath.begin(), trimmedIsoPath.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    trimmedIsoPath.erase(std::find_if(trimmedIsoPath.rbegin(), trimmedIsoPath.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), trimmedIsoPath.end());
    
    std::string mountCmd = "powershell -Command \"$iso = Mount-DiskImage -ImagePath '" + trimmedIsoPath + "' -PassThru; $volume = Get-DiskImage -ImagePath '" + trimmedIsoPath + "' | Get-Volume; if ($volume) { $volume.DriveLetter } else { 'FAILED' }\"";
    logFile << getTimestamp() << "Mount command: " << mountCmd << std::endl;
    
    std::string mountResult = exec(mountCmd.c_str());
    logFile << getTimestamp() << "Mount result: '" << mountResult << "'" << std::endl;
    
    // Check if mount was successful by looking for a drive letter in the result
    driveLetter.clear();
    for (char c : mountResult) {
        if (isalpha(c)) {
            driveLetter = c;
            break;
        }
    }
    logFile << getTimestamp() << "Extracted drive letter: '" << driveLetter << "'" << std::endl;
    bool success = !driveLetter.empty();
    logFile << getTimestamp() << "Mount " << (success ? "successful" : "failed") << std::endl;
    logFile.close();
    return success;
}

bool ISOMounter::unmountISO(const std::string& isoPath)
{
    // Trim the isoPath to remove leading/trailing whitespace
    std::string trimmedIsoPath = isoPath;
    trimmedIsoPath.erase(trimmedIsoPath.begin(), std::find_if(trimmedIsoPath.begin(), trimmedIsoPath.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    trimmedIsoPath.erase(std::find_if(trimmedIsoPath.rbegin(), trimmedIsoPath.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), trimmedIsoPath.end());
    
    std::string dismountCmd = "powershell -Command \"Dismount-DiskImage -ImagePath '" + trimmedIsoPath + "'\"";
    std::string result = exec(dismountCmd.c_str());
    // Check for success
    return result.find("successfully") != std::string::npos || result.empty(); // empty might mean no error
}

std::string ISOMounter::exec(const char* cmd) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "";
    }

    CloseHandle(hWrite);

    char buffer[128];
    std::string result = "";
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}