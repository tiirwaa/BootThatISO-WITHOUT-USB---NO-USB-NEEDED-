#pragma once

#include <string>
#include <optional>

class VolumeManager {
public:
    VolumeManager();

    // Get volume GUID for a drive letter
    std::optional<std::string> getVolumeGUID(const std::string &driveLetter);

private:
    // Helper methods for volume enumeration
    std::optional<std::string> getVolumeGUIDWithRetry(const std::wstring &wDriveLetter);
    std::optional<std::string> enumerateAndFindVolume(const std::wstring &wDriveLetter);
};