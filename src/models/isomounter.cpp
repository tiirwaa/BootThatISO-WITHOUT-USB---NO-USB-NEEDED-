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
    
    // Trim the isoPath to remove leading/trailing whitespace, quotes and common invisible characters
    std::string trimmedIsoPath = isoPath;
    auto isTrimChar = [](char c)->bool {
        unsigned char uc = static_cast<unsigned char>(c);
        // treat control chars (<= 32), NBSP (0xA0), and quotes as trim characters
        return std::isspace(uc) || uc <= 32 || uc == 0xA0 || c == '"' || c == '\'' || c == '\0';
    };
    // Trim start
    while (!trimmedIsoPath.empty() && isTrimChar(trimmedIsoPath.front())) trimmedIsoPath.erase(trimmedIsoPath.begin());
    // Trim end
    while (!trimmedIsoPath.empty() && isTrimChar(trimmedIsoPath.back())) trimmedIsoPath.pop_back();

    logFile << getTimestamp() << "Trimmed ISO path: '" << trimmedIsoPath << "'" << std::endl;
    
    std::string mountCmd = "powershell -Command \"$iso = Mount-DiskImage -ImagePath \\\"" + trimmedIsoPath + "\\\" -PassThru; $volume = Get-DiskImage -ImagePath \\\"" + trimmedIsoPath + "\\\" | Get-Volume; if ($volume) { $volume.DriveLetter } else { 'FAILED' }\"";
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
    // Trim the isoPath to remove leading/trailing whitespace, quotes and common invisible characters
    std::string trimmedIsoPath = isoPath;
    auto isTrimChar2 = [](char c)->bool {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::isspace(uc) || uc <= 32 || uc == 0xA0 || c == '"' || c == '\'' || c == '\0';
    };
    while (!trimmedIsoPath.empty() && isTrimChar2(trimmedIsoPath.front())) trimmedIsoPath.erase(trimmedIsoPath.begin());
    while (!trimmedIsoPath.empty() && isTrimChar2(trimmedIsoPath.back())) trimmedIsoPath.pop_back();

    std::string dismountCmd = "powershell -Command \"Dismount-DiskImage -ImagePath \\\"" + trimmedIsoPath + "\\\"\"";
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