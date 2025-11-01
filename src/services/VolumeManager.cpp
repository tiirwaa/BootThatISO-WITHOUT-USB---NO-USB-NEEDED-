#include "VolumeManager.h"
#include <comutil.h>
#include <iostream>
#include <sstream>

VolumeManager::VolumeManager() : pSvc(nullptr), pLoc(nullptr), comInitialized(false) {}

VolumeManager::~VolumeManager() {
    CleanupWMI();
}

bool VolumeManager::InitializeWMI() {
    if (pSvc)
        return true; // Already initialized

    // Initialize COM in MTA mode for this thread
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) && hr != S_FALSE) {
        comInitialized = true;
    } else if (hr == S_FALSE) {
        comInitialized = false;
    } else {
        return false;
    }

    hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL,
                              EOAC_NONE, NULL);
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
    return SUCCEEDED(hr);
}

void VolumeManager::CleanupWMI() {
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

bool VolumeManager::callWmiMethod(const std::wstring &className, const std::wstring &methodName,
                                  IWbemClassObject *pInstance, IWbemClassObject **ppOutParams) {
    if (!pSvc)
        return false;

    BSTR bstrClass  = SysAllocString(className.c_str());
    BSTR bstrMethod = SysAllocString(methodName.c_str());

    HRESULT hr = pSvc->ExecMethod(bstrClass, bstrMethod, 0, NULL, pInstance, ppOutParams, NULL);

    SysFreeString(bstrClass);
    SysFreeString(bstrMethod);

    return SUCCEEDED(hr);
}

bool VolumeManager::formatVolume(const std::string &volumeLabel, const std::string &fileSystem, std::string &errorMsg) {
    if (!InitializeWMI()) {
        errorMsg = "Failed to initialize WMI";
        return false;
    }

    try {
        // Find the volume by label
        std::wstringstream queryStream;
        queryStream << L"SELECT * FROM MSFT_Volume WHERE FileSystemLabel = '"
                    << std::wstring(volumeLabel.begin(), volumeLabel.end()) << L"'";
        std::wstring query = queryStream.str();

        BSTR                  bstrQuery    = SysAllocString(query.c_str());
        BSTR                  bstrLanguage = SysAllocString(L"WQL");
        IEnumWbemClassObject *pEnumerator  = nullptr;

        HRESULT hr = pSvc->ExecQuery(bstrLanguage, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                     NULL, &pEnumerator);

        SysFreeString(bstrQuery);
        SysFreeString(bstrLanguage);

        if (FAILED(hr) || !pEnumerator) {
            errorMsg = "Failed to query MSFT_Volume";
            return false;
        }

        IWbemClassObject *pVolume = nullptr;
        ULONG             uReturn = 0;
        hr                        = pEnumerator->Next(WBEM_INFINITE, 1, &pVolume, &uReturn);
        pEnumerator->Release();

        if (FAILED(hr) || uReturn == 0) {
            errorMsg = "Volume not found: " + volumeLabel;
            return false;
        }

        // Get the Format method
        IWbemClassObject *pClass        = nullptr;
        BSTR              bstrClassName = SysAllocString(L"MSFT_Volume");
        hr                              = pSvc->GetObject(bstrClassName, 0, NULL, &pClass, NULL);
        SysFreeString(bstrClassName);

        if (FAILED(hr) || !pClass) {
            pVolume->Release();
            errorMsg = "Failed to get MSFT_Volume class";
            return false;
        }

        IWbemClassObject *pMethod        = nullptr;
        BSTR              bstrMethodName = SysAllocString(L"Format");
        hr                               = pClass->GetMethod(bstrMethodName, 0, &pMethod, NULL);
        SysFreeString(bstrMethodName);
        pClass->Release();

        if (FAILED(hr) || !pMethod) {
            pVolume->Release();
            errorMsg = "Failed to get Format method";
            return false;
        }

        // Create input parameters
        IWbemClassObject *pInParams = nullptr;
        hr                          = pMethod->SpawnInstance(0, &pInParams);
        pMethod->Release();

        if (FAILED(hr) || !pInParams) {
            pVolume->Release();
            errorMsg = "Failed to create input parameters";
            return false;
        }

        // Set parameters
        VARIANT varFS;
        VariantInit(&varFS);
        varFS.vt = VT_BSTR;
        std::wstring fsW(fileSystem.begin(), fileSystem.end());
        varFS.bstrVal = SysAllocString(fsW.c_str());
        pInParams->Put(L"FileSystem", 0, &varFS, 0);
        VariantClear(&varFS);

        VARIANT varLabel;
        VariantInit(&varLabel);
        varLabel.vt = VT_BSTR;
        std::wstring labelW(volumeLabel.begin(), volumeLabel.end());
        varLabel.bstrVal = SysAllocString(labelW.c_str());
        pInParams->Put(L"FileSystemLabel", 0, &varLabel, 0);
        VariantClear(&varLabel);

        VARIANT varFull;
        VariantInit(&varFull);
        varFull.vt      = VT_BOOL;
        varFull.boolVal = VARIANT_FALSE; // Quick format
        pInParams->Put(L"Full", 0, &varFull, 0);
        VariantClear(&varFull);

        VARIANT varForce;
        VariantInit(&varForce);
        varForce.vt      = VT_BOOL;
        varForce.boolVal = VARIANT_TRUE;
        pInParams->Put(L"Force", 0, &varForce, 0);
        VariantClear(&varForce);

        // Execute method
        IWbemClassObject *pOutParams = nullptr;
        hr                           = callWmiMethod(L"MSFT_Volume", L"Format", pInParams, &pOutParams);

        pInParams->Release();
        pVolume->Release();

        if (FAILED(hr)) {
            errorMsg = "Format method failed";
            if (pOutParams)
                pOutParams->Release();
            return false;
        }

        // Check return value
        VARIANT varReturn;
        VariantInit(&varReturn);
        hr           = pOutParams->Get(L"ReturnValue", 0, &varReturn, NULL, NULL);
        bool success = SUCCEEDED(hr) && varReturn.uintVal == 0;
        VariantClear(&varReturn);
        pOutParams->Release();

        if (!success) {
            errorMsg = "Format returned error";
        }

        return success;
    } catch (...) {
        errorMsg = "Exception in formatVolume";
        return false;
    }
}

bool VolumeManager::assignDriveLetter(const std::string &volumeId, char driveLetter, std::string &errorMsg) {
    if (!InitializeWMI()) {
        errorMsg = "Failed to initialize WMI";
        return false;
    }

    try {
        // Find the volume by ID
        std::wstringstream queryStream;
        queryStream << L"SELECT * FROM MSFT_Volume WHERE ObjectId = '" << std::wstring(volumeId.begin(), volumeId.end())
                    << L"'";
        std::wstring query = queryStream.str();

        BSTR                  bstrQuery    = SysAllocString(query.c_str());
        BSTR                  bstrLanguage = SysAllocString(L"WQL");
        IEnumWbemClassObject *pEnumerator  = nullptr;

        HRESULT hr = pSvc->ExecQuery(bstrLanguage, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                     NULL, &pEnumerator);

        SysFreeString(bstrQuery);
        SysFreeString(bstrLanguage);

        if (FAILED(hr) || !pEnumerator) {
            errorMsg = "Failed to query MSFT_Volume";
            return false;
        }

        IWbemClassObject *pVolume = nullptr;
        ULONG             uReturn = 0;
        hr                        = pEnumerator->Next(WBEM_INFINITE, 1, &pVolume, &uReturn);
        pEnumerator->Release();

        if (FAILED(hr) || uReturn == 0) {
            errorMsg = "Volume not found: " + volumeId;
            return false;
        }

        // Get the AddAccessPath method
        IWbemClassObject *pClass        = nullptr;
        BSTR              bstrClassName = SysAllocString(L"MSFT_Volume");
        hr                              = pSvc->GetObject(bstrClassName, 0, NULL, &pClass, NULL);
        SysFreeString(bstrClassName);

        if (FAILED(hr) || !pClass) {
            pVolume->Release();
            errorMsg = "Failed to get MSFT_Volume class";
            return false;
        }

        IWbemClassObject *pMethod        = nullptr;
        BSTR              bstrMethodName = SysAllocString(L"AddAccessPath");
        hr                               = pClass->GetMethod(bstrMethodName, 0, &pMethod, NULL);
        SysFreeString(bstrMethodName);
        pClass->Release();

        if (FAILED(hr) || !pMethod) {
            pVolume->Release();
            errorMsg = "Failed to get AddAccessPath method";
            return false;
        }

        // Create input parameters
        IWbemClassObject *pInParams = nullptr;
        hr                          = pMethod->SpawnInstance(0, &pInParams);
        pMethod->Release();

        if (FAILED(hr) || !pInParams) {
            pVolume->Release();
            errorMsg = "Failed to create input parameters";
            return false;
        }

        // Set AccessPath parameter
        std::string accessPath = std::string(1, driveLetter) + ":\\";
        VARIANT     varPath;
        VariantInit(&varPath);
        varPath.vt = VT_BSTR;
        std::wstring pathW(accessPath.begin(), accessPath.end());
        varPath.bstrVal = SysAllocString(pathW.c_str());
        pInParams->Put(L"AccessPath", 0, &varPath, 0);
        VariantClear(&varPath);

        // Execute method
        IWbemClassObject *pOutParams = nullptr;
        hr                           = callWmiMethod(L"MSFT_Volume", L"AddAccessPath", pInParams, &pOutParams);

        pInParams->Release();
        pVolume->Release();

        if (FAILED(hr)) {
            errorMsg = "AddAccessPath method failed";
            if (pOutParams)
                pOutParams->Release();
            return false;
        }

        // Check return value
        VARIANT varReturn;
        VariantInit(&varReturn);
        hr           = pOutParams->Get(L"ReturnValue", 0, &varReturn, NULL, NULL);
        bool success = SUCCEEDED(hr) && varReturn.uintVal == 0;
        VariantClear(&varReturn);
        pOutParams->Release();

        if (!success) {
            errorMsg = "AddAccessPath returned error";
        }

        return success;
    } catch (...) {
        errorMsg = "Exception in assignDriveLetter";
        return false;
    }
}

std::vector<VolumeInfo> VolumeManager::getVolumes() {
    std::vector<VolumeInfo> volumes;

    if (!InitializeWMI()) {
        return volumes;
    }

    BSTR                  bstrQuery    = SysAllocString(L"SELECT * FROM MSFT_Volume");
    BSTR                  bstrLanguage = SysAllocString(L"WQL");
    IEnumWbemClassObject *pEnumerator  = nullptr;

    HRESULT hr = pSvc->ExecQuery(bstrLanguage, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
                                 &pEnumerator);

    SysFreeString(bstrQuery);
    SysFreeString(bstrLanguage);

    if (FAILED(hr) || !pEnumerator) {
        return volumes;
    }

    IWbemClassObject *pVolume = nullptr;
    ULONG             uReturn = 0;

    while (pEnumerator->Next(WBEM_INFINITE, 1, &pVolume, &uReturn) == S_OK) {
        VolumeInfo info;

        // Get ObjectId
        VARIANT varId;
        VariantInit(&varId);
        if (SUCCEEDED(pVolume->Get(L"ObjectId", 0, &varId, NULL, NULL))) {
            if (varId.vt == VT_BSTR && varId.bstrVal) {
                std::wstring idW(varId.bstrVal);
                info.volumeId = std::string(idW.begin(), idW.end());
            }
            VariantClear(&varId);
        }

        // Get FileSystemLabel
        VARIANT varLabel;
        VariantInit(&varLabel);
        if (SUCCEEDED(pVolume->Get(L"FileSystemLabel", 0, &varLabel, NULL, NULL))) {
            if (varLabel.vt == VT_BSTR && varLabel.bstrVal) {
                std::wstring labelW(varLabel.bstrVal);
                info.fileSystemLabel = std::string(labelW.begin(), labelW.end());
            }
            VariantClear(&varLabel);
        }

        // Get FileSystem
        VARIANT varFS;
        VariantInit(&varFS);
        if (SUCCEEDED(pVolume->Get(L"FileSystem", 0, &varFS, NULL, NULL))) {
            if (varFS.vt == VT_BSTR && varFS.bstrVal) {
                std::wstring fsW(varFS.bstrVal);
                info.fileSystem = std::string(fsW.begin(), fsW.end());
            }
            VariantClear(&varFS);
        }

        // Get Size
        VARIANT varSize;
        VariantInit(&varSize);
        if (SUCCEEDED(pVolume->Get(L"Size", 0, &varSize, NULL, NULL))) {
            if (varSize.vt == VT_UI8) {
                info.sizeBytes = varSize.ullVal;
            }
            VariantClear(&varSize);
        }

        volumes.push_back(info);
        pVolume->Release();
    }

    pEnumerator->Release();
    return volumes;
}

VolumeInfo VolumeManager::getVolumeByLabel(const std::string &label) {
    auto volumes = getVolumes();
    for (const auto &vol : volumes) {
        if (vol.fileSystemLabel == label) {
            return vol;
        }
    }
    return VolumeInfo(); // Return empty info if not found
}