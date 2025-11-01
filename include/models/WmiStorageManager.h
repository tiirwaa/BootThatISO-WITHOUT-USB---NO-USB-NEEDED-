#pragma once
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <atlbase.h>
#include <string>
#include <fstream>

#pragma comment(lib, "wbemuuid.lib")

// Forward declaration for Utils
namespace Utils {
    std::string getExeDirectory();
}

// WMI Helper Class for Storage Management
class WmiStorageManager {
private:
    IWbemServices* pSvc;
    IWbemLocator* pLoc;
    bool comInitialized;

public:
    WmiStorageManager() : pSvc(nullptr), pLoc(nullptr), comInitialized(false) {}
    ~WmiStorageManager() {
        if (pSvc) pSvc->Release();
        if (pLoc) pLoc->Release();
        if (comInitialized) CoUninitialize();
    }

    bool Initialize() {
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::ofstream logFile((logDir + "\\wmi_debug.log").c_str(), std::ios::app);
        if (logFile) {
            logFile << "=== WMI Initialize Debug Log ===\n";
        }

        // Initialize COM in MTA mode for this thread
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (logFile) {
            logFile << "CoInitializeEx: hr = 0x" << std::hex << hr << " (" << std::dec << hr << ")\n";
            if (hr == RPC_E_CHANGED_MODE) {
                logFile << "COM already initialized in different threading model\n";
            } else if (hr == S_FALSE) {
                logFile << "COM already initialized in same threading model\n";
            } else if (hr == S_OK) {
                logFile << "COM initialized successfully in MTA mode\n";
            }
        }
        if (SUCCEEDED(hr) && hr != S_FALSE) {
            comInitialized = true;
        } else if (hr == S_FALSE) {
            // COM already initialized, don't uninitialize in destructor
            comInitialized = false;
        } else {
            // Failed to initialize COM
            if (logFile) {
                logFile << "CoInitializeEx FAILED\n";
                logFile.close();
            }
            return false;
        }

        hr = CoInitializeSecurity(
            NULL, -1, NULL, NULL,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL, EOAC_NONE, NULL);
        if (logFile) {
            logFile << "CoInitializeSecurity: hr = 0x" << std::hex << hr << " (" << std::dec << hr << ")\n";
        }
        if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
            if (logFile) {
                logFile << "CoInitializeSecurity FAILED (not due to already initialized)\n";
                logFile.close();
            }
            return false;
        } else if (hr == RPC_E_TOO_LATE) {
            if (logFile) {
                logFile << "CoInitializeSecurity: COM security already initialized, continuing...\n";
            }
        }

        hr = CoCreateInstance(
            CLSID_WbemLocator, 0,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, (LPVOID*)&pLoc);
        if (logFile) {
            logFile << "CoCreateInstance: hr = 0x" << std::hex << hr << " (" << std::dec << hr << ")\n";
        }
        if (FAILED(hr)) {
            if (logFile) {
                logFile << "CoCreateInstance FAILED\n";
                logFile.close();
            }
            return false;
        }

        hr = pLoc->ConnectServer(
            _bstr_t(L"ROOT\\Microsoft\\Windows\\Storage"),
            NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (logFile) {
            logFile << "ConnectServer: hr = 0x" << std::hex << hr << " (" << std::dec << hr << ")\n";
        }
        if (FAILED(hr)) {
            if (logFile) {
                logFile << "ConnectServer FAILED\n";
                logFile.close();
            }
            return false;
        }

        hr = CoSetProxyBlanket(
            pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
            NULL, RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
        if (logFile) {
            logFile << "CoSetProxyBlanket: hr = 0x" << std::hex << hr << " (" << std::dec << hr << ")\n";
            if (SUCCEEDED(hr)) {
                logFile << "All WMI initialization steps succeeded\n";
            } else {
                logFile << "CoSetProxyBlanket FAILED\n";
            }
            logFile.close();
        }
        return SUCCEEDED(hr);
    }

    IWbemServices* GetServices() { return pSvc; }

    bool ExecuteMethod(const std::wstring& className,
                      const std::wstring& methodName,
                      IWbemClassObject* pInstance,
                      IWbemClassObject** ppOutParams = nullptr) {
        if (!pSvc) return false;

        BSTR bstrClass = SysAllocString(className.c_str());
        BSTR bstrMethod = SysAllocString(methodName.c_str());

        HRESULT hr = pSvc->ExecMethod(bstrClass, bstrMethod,
                                     0, NULL, pInstance, ppOutParams, NULL);

        SysFreeString(bstrClass);
        SysFreeString(bstrMethod);

        return SUCCEEDED(hr);
    }

    IWbemClassObject* GetClassObject(const std::wstring& className) {
        if (!pSvc) return nullptr;

        BSTR bstrClass = SysAllocString(className.c_str());
        IWbemClassObject* pClass = nullptr;
        HRESULT hr = pSvc->GetObject(bstrClass, 0, NULL, &pClass, NULL);
        SysFreeString(bstrClass);

        if (FAILED(hr)) return nullptr;
        return pClass;
    }

    IEnumWbemClassObject* ExecQuery(const std::wstring& query) {
        if (!pSvc) return nullptr;

        BSTR bstrQuery = SysAllocString(query.c_str());
        BSTR bstrLanguage = SysAllocString(L"WQL");
        IEnumWbemClassObject* pEnumerator = nullptr;

        HRESULT hr = pSvc->ExecQuery(bstrLanguage, bstrQuery,
                                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                    NULL, &pEnumerator);

        SysFreeString(bstrQuery);
        SysFreeString(bstrLanguage);

        if (FAILED(hr)) return nullptr;
        return pEnumerator;
    }
};