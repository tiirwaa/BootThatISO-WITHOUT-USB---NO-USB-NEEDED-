#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>

struct VolumeInfo {
    std::string volumeId;
    std::string fileSystemLabel;
    std::string fileSystem;
    UINT64 sizeBytes;
    bool isMounted;
    std::string driveLetter;
};

class VolumeManager {
private:
    IWbemServices* pSvc;
    IWbemLocator* pLoc;
    bool comInitialized;

    bool InitializeWMI();
    void CleanupWMI();
    bool callWmiMethod(const std::wstring& className, const std::wstring& methodName,
                       IWbemClassObject* pInstance, IWbemClassObject** ppOutParams = nullptr);

public:
    VolumeManager();
    ~VolumeManager();

    bool formatVolume(const std::string& volumeLabel, const std::string& fileSystem, std::string& errorMsg);
    bool assignDriveLetter(const std::string& volumeId, char driveLetter, std::string& errorMsg);
    std::vector<VolumeInfo> getVolumes();
    VolumeInfo getVolumeByLabel(const std::string& label);
};