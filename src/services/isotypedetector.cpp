#include "isotypedetector.h"
#include <windows.h>
#include <fstream>
#include <ctime>
#include <iomanip>

ISOTypeDetector::ISOTypeDetector() {
    // Constructor vacío por ahora
}

ISOTypeDetector::~ISOTypeDetector() {
    // Destructor vacío por ahora
}

const char* ISOTypeDetector::getTimestamp() {
    static char buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return buffer;
}

bool ISOTypeDetector::isWindowsISO(const std::string& mountedIsoPath) {
    // Create log file for debugging
    std::ofstream logFile("logs\\iso_type_detection.log", std::ios::app);
    logFile << getTimestamp() << "Checking ISO type for path: " << mountedIsoPath << std::endl;
    
    // Check if it's Windows ISO
    std::string sourcesPath = mountedIsoPath + "sources";
    DWORD sourcesAttrs = GetFileAttributesA(sourcesPath.c_str());
    bool hasSources = (sourcesAttrs != INVALID_FILE_ATTRIBUTES && (sourcesAttrs & FILE_ATTRIBUTE_DIRECTORY));
    logFile << getTimestamp() << "Sources directory '" << sourcesPath << "' exists: " << (hasSources ? "YES" : "NO") << " (attrs: " << sourcesAttrs << ")" << std::endl;
    
    bool isWindowsISO = false;
    if (hasSources) {
        std::string installWimPath = mountedIsoPath + "sources\\install.wim";
        std::string installEsdPath = mountedIsoPath + "sources\\install.esd";
        DWORD installWimAttrs = GetFileAttributesA(installWimPath.c_str());
        DWORD installEsdAttrs = GetFileAttributesA(installEsdPath.c_str());
        bool hasInstallWim = (installWimAttrs != INVALID_FILE_ATTRIBUTES);
        bool hasInstallEsd = (installEsdAttrs != INVALID_FILE_ATTRIBUTES);
        logFile << getTimestamp() << "install.wim '" << installWimPath << "' exists: " << (hasInstallWim ? "YES" : "NO") << " (attrs: " << installWimAttrs << ")" << std::endl;
        logFile << getTimestamp() << "install.esd '" << installEsdPath << "' exists: " << (hasInstallEsd ? "YES" : "NO") << " (attrs: " << installEsdAttrs << ")" << std::endl;
        isWindowsISO = hasInstallWim || hasInstallEsd;
    }
    logFile << getTimestamp() << "Is Windows ISO: " << (isWindowsISO ? "YES" : "NO") << std::endl;
    logFile.close();
    return isWindowsISO;
}