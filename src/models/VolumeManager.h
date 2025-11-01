#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <atlbase.h>
#include "../../include/models/WmiStorageManager.h"

#pragma comment(lib, "wbemuuid.lib")

// Volume management interface
class VolumeManager {
public:
    // Format a volume with specified file system
    static bool formatVolume(const std::string &volumeLabel, const std::string &fileSystem, std::string &errorMsg);

    // Assign a drive letter to a volume
    static bool assignDriveLetter(const std::string &volumeLabel, char driveLetter, std::string &errorMsg);

    // Get volume information
    static bool getVolumeInfo(const std::string &volumeLabel, std::string &fileSystem, UINT64 &sizeBytes,
                              std::string &errorMsg);

private:
    // Helper method to call WMI methods
    static bool callWmiMethod(const std::string &className, const std::string &methodName,
                              const std::vector<std::pair<std::string, VARIANT>> &params,
                              IWbemClassObject **ppOutParams, std::string &errorMsg);
};