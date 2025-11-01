#ifndef VOLUME_MANAGER_H
#define VOLUME_MANAGER_H

#include <string>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma comment(lib, "wbemuuid.lib")

class VolumeManager {
private:
    class WmiStorageManager {
    private:
        IWbemLocator* pLoc;
        IWbemServices* pSvc;
        bool comInitialized;

    public:
        WmiStorageManager() : pLoc(nullptr), pSvc(nullptr), comInitialized(false) {}

        ~WmiStorageManager() {
            if (pSvc) {
                pSvc->Release();
                pSvc = nullptr;
            }
            if (pLoc) {
                pLoc->Release();
                pLoc = nullptr;
            }
            if (comInitialized) {
                CoUninitialize();
                comInitialized = false;
            }
        }

        bool Initialize() {
            HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            if (SUCCEEDED(hr) && hr != S_FALSE) {
                comInitialized = true;
            } else if (hr == S_FALSE) {
                comInitialized = false;
            } else {
                return false;
            }

            hr = CoInitializeSecurity(
                NULL, -1, NULL, NULL,
                RPC_C_AUTHN_LEVEL_DEFAULT,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                NULL, EOAC_NONE, NULL);
            if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
                return false;
            }

            hr = CoCreateInstance(
                CLSID_WbemLocator, 0,
                CLSCTX_INPROC_SERVER,
                IID_IWbemLocator, (LPVOID*)&pLoc);
            if (FAILED(hr)) {
                return false;
            }

            hr = pLoc->ConnectServer(
                _bstr_t(L"ROOT\\Microsoft\\Windows\\Storage"),
                NULL, NULL, 0, NULL, 0, 0, &pSvc);
            if (FAILED(hr)) {
                return false;
            }

            hr = CoSetProxyBlanket(
                pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                NULL, RPC_C_AUTHN_LEVEL_CALL,
                RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            if (FAILED(hr)) {
                return false;
            }
            return true;
        }

        IEnumWbemClassObject* ExecQuery(const wchar_t* query) {
            if (!pSvc) return nullptr;

            IEnumWbemClassObject* pEnum = nullptr;
            HRESULT hr = pSvc->ExecQuery(
                _bstr_t("WQL"),
                _bstr_t(query),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                NULL,
                &pEnum);

            if (FAILED(hr)) {
                return nullptr;
            }

            return pEnum;
        }

        IWbemServices* GetServices() { return pSvc; }
    };

public:
    // Format a volume with specific file system and label
    bool formatVolume(const std::string& volumeLabel, const std::string& fileSystem, std::string& errorMsg);

    // Assign a drive letter to a volume
    bool assignDriveLetter(const std::string& volumeLabel, char driveLetter, std::string& errorMsg);

    // Get volume information
    bool getVolumeInfo(const std::string& volumeLabel, std::string& fileSystem, UINT64& sizeBytes, std::string& errorMsg);

private:
    bool callWmiMethod(const std::string& className, const std::string& methodName,
                      const std::vector<std::pair<std::string, VARIANT>>& params,
                      IWbemClassObject** ppOutParams, std::string& errorMsg);
};

#endif // VOLUME_MANAGER_H