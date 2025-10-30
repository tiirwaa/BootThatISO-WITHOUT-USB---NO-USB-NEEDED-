#include "isotypedetector.h"
#include <windows.h>

ISOTypeDetector::ISOTypeDetector() {
    // Constructor vacío por ahora
}

ISOTypeDetector::~ISOTypeDetector() {
    // Destructor vacío por ahora
}

bool ISOTypeDetector::isWindowsISO(const std::string& mountedIsoPath) {
    // Check if it's Windows ISO
    DWORD sourcesAttrs = GetFileAttributesA((mountedIsoPath + "sources").c_str());
    bool hasSources = (sourcesAttrs != INVALID_FILE_ATTRIBUTES && (sourcesAttrs & FILE_ATTRIBUTE_DIRECTORY));
    bool isWindowsISO = false;
    if (hasSources) {
        DWORD installWimAttrs = GetFileAttributesA((mountedIsoPath + "sources\\install.wim").c_str());
        DWORD installEsdAttrs = GetFileAttributesA((mountedIsoPath + "sources\\install.esd").c_str());
        isWindowsISO = (installWimAttrs != INVALID_FILE_ATTRIBUTES) || (installEsdAttrs != INVALID_FILE_ATTRIBUTES);
    }
    return isWindowsISO;
}