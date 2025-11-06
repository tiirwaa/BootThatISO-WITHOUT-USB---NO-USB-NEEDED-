#include "BCDVolumeManager.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <string>
#include <vector>

BCDVolumeManager::BCDVolumeManager() {}

std::optional<std::string> BCDVolumeManager::getVolumeGUID(const std::string &driveLetter) {
    std::wstring wDriveLetter = Utils::utf8_to_wstring(driveLetter);
    if (wDriveLetter.empty() || wDriveLetter.back() != L'\\')
        wDriveLetter += L'\\';

    auto result = getVolumeGUIDWithRetry(wDriveLetter);
    if (result) {
        return result;
    }

    return enumerateAndFindVolume(wDriveLetter);
}

std::optional<std::string> BCDVolumeManager::getVolumeGUIDWithRetry(const std::wstring &wDriveLetter) {
    const int MAX_ATTEMPTS = 3;
    WCHAR     dataVolumeName[MAX_PATH];

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        if (GetVolumeNameForVolumeMountPointW(wDriveLetter.c_str(), dataVolumeName, MAX_PATH)) {
            char narrowVolumeName[MAX_PATH * 2];
            WideCharToMultiByte(CP_UTF8, 0, dataVolumeName, -1, narrowVolumeName, sizeof(narrowVolumeName), NULL, NULL);
            return std::string(narrowVolumeName);
        }
        Sleep(500); // Small delay before retry
    }

    return std::nullopt;
}

std::optional<std::string> BCDVolumeManager::enumerateAndFindVolume(const std::wstring &wDriveLetter) {
    WCHAR  volName[MAX_PATH];
    HANDLE hVol = FindFirstVolumeW(volName, MAX_PATH);
    if (hVol == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    do {
        std::wstring volNameStr = volName;
        if (volNameStr.back() != L'\\')
            volNameStr.push_back(L'\\');

        // Get mount points for this volume
        DWORD returnLen = 0;
        BOOL  got       = GetVolumePathNamesForVolumeNameW(volNameStr.c_str(), NULL, 0, &returnLen);
        DWORD gle       = GetLastError();

        if (!got && gle == ERROR_MORE_DATA && returnLen > 0) {
            std::vector<WCHAR> buf(returnLen);
            if (GetVolumePathNamesForVolumeNameW(volNameStr.c_str(), buf.data(), returnLen, &returnLen)) {
                WCHAR *p = buf.data();
                while (*p) {
                    std::wstring mountPoint(p);
                    if (mountPoint.back() != L'\\')
                        mountPoint.push_back(L'\\');
                    if (mountPoint == wDriveLetter) {
                        char narrowVolumeName[MAX_PATH * 2];
                        WideCharToMultiByte(CP_UTF8, 0, volName, -1, narrowVolumeName, sizeof(narrowVolumeName), NULL,
                                            NULL);
                        FindVolumeClose(hVol);
                        return std::string(narrowVolumeName);
                    }
                    p += wcslen(p) + 1;
                }
            }
        }
    } while (FindNextVolumeW(hVol, volName, MAX_PATH));

    FindVolumeClose(hVol);
    return std::nullopt;
}