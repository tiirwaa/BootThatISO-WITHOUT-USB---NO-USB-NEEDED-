#ifndef PARTITION_DETECTOR_H
#define PARTITION_DETECTOR_H

#include <vector>
#include <string>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma comment(lib, "wbemuuid.lib")

struct PartitionInfo {
    UINT32      partitionNumber;
    UINT64      sizeBytes;
    std::string driveLetter;
    std::string fileSystemLabel;
    bool        hasVolume;
    std::string partitionType;
};

class PartitionDetector {
private:
    class WmiStorageManager {
    private:
        IWbemLocator  *pLoc;
        IWbemServices *pSvc;
        bool           comInitialized;

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

            hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                                      NULL, EOAC_NONE, NULL);
            if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
                return false;
            }

            hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID *)&pLoc);
            if (FAILED(hr)) {
                return false;
            }

            hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\Microsoft\\Windows\\Storage"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
            if (FAILED(hr)) {
                return false;
            }

            hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                                   RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            if (FAILED(hr)) {
                return false;
            }
            return true;
        }

        IEnumWbemClassObject *ExecQuery(const wchar_t *query) {
            if (!pSvc)
                return nullptr;

            IEnumWbemClassObject *pEnum = nullptr;
            HRESULT               hr    = pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t(query),
                                                          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnum);

            if (FAILED(hr)) {
                return nullptr;
            }

            return pEnum;
        }
    };

public:
    // Detect partitions by file system labels (existing method)
    std::vector<PartitionInfo> findPartitionsByLabels(const std::vector<std::string> &labels);

    // Detect partitions by size (new method for unformatted partitions)
    std::vector<PartitionInfo> findPartitionsBySize(UINT64 minSizeBytes, UINT64 maxSizeBytes = 0);

    // Detect partitions without assigned volumes
    std::vector<PartitionInfo> findUnformattedPartitions();

    // Detect all partitions on disk
    std::vector<PartitionInfo> findAllPartitions();

    // Find system partition (usually C:)
    PartitionInfo findSystemPartition();

    // Check if partition should be deleted during space recovery
    bool shouldDeletePartition(const PartitionInfo &partition);

private:
    std::vector<PartitionInfo> queryPartitions(const std::string &whereClause = "");
    PartitionInfo              wmiObjectToPartitionInfo(IWbemClassObject *pPartition);
};

#endif // PARTITION_DETECTOR_H