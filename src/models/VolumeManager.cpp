#include "VolumeManager.h"
#include "../utils/Utils.h"

bool VolumeManager::formatVolume(const std::string &volumeLabel, const std::string &fileSystem, std::string &errorMsg) {
    WmiStorageManager wmi;
    if (!wmi.Initialize()) {
        errorMsg = "Failed to initialize WMI";
        return false;
    }

    // Find the volume by label
    std::string           query = "SELECT * FROM MSFT_Volume WHERE FileSystemLabel = '" + volumeLabel + "'";
    IEnumWbemClassObject *pEnum = wmi.ExecQuery(std::wstring(query.begin(), query.end()).c_str());

    if (!pEnum) {
        errorMsg = "Failed to query volumes";
        return false;
    }

    IWbemClassObject *pVolume = nullptr;
    ULONG             uReturn = 0;
    HRESULT           hr      = pEnum->Next(WBEM_INFINITE, 1, &pVolume, &uReturn);
    pEnum->Release();

    if (FAILED(hr) || uReturn == 0) {
        errorMsg = "Volume not found";
        return false;
    }

    // Get volume path
    VARIANT varPath;
    VariantInit(&varPath);
    hr = pVolume->Get(L"Path", 0, &varPath, NULL, NULL);

    if (FAILED(hr)) {
        pVolume->Release();
        VariantClear(&varPath);
        errorMsg = "Failed to get volume path";
        return false;
    }

    std::wstring volumePath(varPath.bstrVal);
    VariantClear(&varPath);
    pVolume->Release();

    // Use Windows format command
    std::string cmd = "format " + std::string(volumePath.begin(), volumePath.end()) + " /FS:" + fileSystem +
                      " /V:" + volumeLabel + " /Q /Y";

    STARTUPINFOA        si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        errorMsg = "Failed to start format process";
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        errorMsg = "Format failed with exit code " + std::to_string(exitCode);
        return false;
    }

    return true;
}

bool VolumeManager::assignDriveLetter(const std::string &volumeLabel, char driveLetter, std::string &errorMsg) {
    WmiStorageManager wmi;
    if (!wmi.Initialize()) {
        errorMsg = "Failed to initialize WMI";
        return false;
    }

    // Find the volume by label
    std::string           query = "SELECT * FROM MSFT_Volume WHERE FileSystemLabel = '" + volumeLabel + "'";
    IEnumWbemClassObject *pEnum = wmi.ExecQuery(std::wstring(query.begin(), query.end()).c_str());

    if (!pEnum) {
        errorMsg = "Failed to query volumes";
        return false;
    }

    IWbemClassObject *pVolume = nullptr;
    ULONG             uReturn = 0;
    HRESULT           hr      = pEnum->Next(WBEM_INFINITE, 1, &pVolume, &uReturn);
    pEnum->Release();

    if (FAILED(hr) || uReturn == 0) {
        errorMsg = "Volume not found";
        return false;
    }

    // Call AddAccessPath method
    std::vector<std::pair<std::string, VARIANT>> params;
    VARIANT                                      varAccessPath;
    VariantInit(&varAccessPath);
    varAccessPath.vt      = VT_BSTR;
    varAccessPath.bstrVal = SysAllocString(std::wstring(std::string(1, driveLetter) + ":").c_str());
    params.push_back({"AccessPath", varAccessPath});

    IWbemClassObject *pOutParams = nullptr;
    bool              result     = callWmiMethod("MSFT_Volume", "AddAccessPath", params, &pOutParams, errorMsg);

    VariantClear(&varAccessPath);
    pVolume->Release();

    if (pOutParams) {
        pOutParams->Release();
    }

    return result;
}

bool VolumeManager::getVolumeInfo(const std::string &volumeLabel, std::string &fileSystem, UINT64 &sizeBytes,
                                  std::string &errorMsg) {
    WmiStorageManager wmi;
    if (!wmi.Initialize()) {
        errorMsg = "Failed to initialize WMI";
        return false;
    }

    // Find the volume by label
    std::string           query = "SELECT * FROM MSFT_Volume WHERE FileSystemLabel = '" + volumeLabel + "'";
    IEnumWbemClassObject *pEnum = wmi.ExecQuery(std::wstring(query.begin(), query.end()).c_str());

    if (!pEnum) {
        errorMsg = "Failed to query volumes";
        return false;
    }

    IWbemClassObject *pVolume = nullptr;
    ULONG             uReturn = 0;
    HRESULT           hr      = pEnum->Next(WBEM_INFINITE, 1, &pVolume, &uReturn);
    pEnum->Release();

    if (FAILED(hr) || uReturn == 0) {
        errorMsg = "Volume not found";
        return false;
    }

    // Get file system
    VARIANT varFS;
    VariantInit(&varFS);
    if (SUCCEEDED(pVolume->Get(L"FileSystem", 0, &varFS, NULL, NULL))) {
        if (varFS.bstrVal) {
            std::wstring ws(varFS.bstrVal);
            fileSystem = std::string(ws.begin(), ws.end());
        }
        VariantClear(&varFS);
    }

    // Get size
    VARIANT varSize;
    VariantInit(&varSize);
    if (SUCCEEDED(pVolume->Get(L"Size", 0, &varSize, NULL, NULL))) {
        sizeBytes = varSize.ullVal;
        VariantClear(&varSize);
    }

    pVolume->Release();
    return true;
}

bool VolumeManager::callWmiMethod(const std::string &className, const std::string &methodName,
                                  const std::vector<std::pair<std::string, VARIANT>> &params,
                                  IWbemClassObject **ppOutParams, std::string &errorMsg) {
    WmiStorageManager wmi;
    if (!wmi.Initialize()) {
        errorMsg = "Failed to initialize WMI";
        return false;
    }

    IWbemServices *pSvc = wmi.GetServices();
    if (!pSvc) {
        errorMsg = "WMI services not available";
        return false;
    }

    // Get the class object
    IWbemClassObject *pClass = nullptr;
    HRESULT           hr     = pSvc->GetObject(_bstr_t(className.c_str()), 0, NULL, &pClass, NULL);
    if (FAILED(hr)) {
        errorMsg = "Failed to get class object";
        return false;
    }

    // Get the method
    IWbemClassObject *pInParams = nullptr;
    hr                          = pClass->GetMethod(_bstr_t(methodName.c_str()), 0, &pInParams, NULL);
    if (FAILED(hr)) {
        pClass->Release();
        errorMsg = "Failed to get method";
        return false;
    }

    // Set parameters
    for (const auto &param : params) {
        hr = pInParams->Put(_bstr_t(param.first.c_str()), 0, &param.second, 0);
        if (FAILED(hr)) {
            pInParams->Release();
            pClass->Release();
            errorMsg = "Failed to set parameter " + param.first;
            return false;
        }
    }

    // Execute method
    hr = pSvc->ExecMethod(_bstr_t(className.c_str()), _bstr_t(methodName.c_str()), 0, NULL, pInParams, ppOutParams,
                          NULL);

    pInParams->Release();
    pClass->Release();

    if (FAILED(hr)) {
        errorMsg = "Method execution failed";
        return false;
    }

    return true;
}