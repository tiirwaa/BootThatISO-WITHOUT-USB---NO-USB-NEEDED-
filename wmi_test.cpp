#include <iostream>
#include <thread>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <fstream>
#include <string>

#pragma comment(lib, "wbemuuid.lib")

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
        std::string logDir = ".\\logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::ofstream logFile((logDir + "\\wmi_test_debug.log").c_str(), std::ios::app);
        if (logFile) {
            logFile << "=== WMI Test Initialize Debug Log ===\n";
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
};

void testWmiInThread() {
    std::cout << "Testing WMI initialization in background thread..." << std::endl;

    WmiStorageManager wmi;
    if (wmi.Initialize()) {
        std::cout << "WMI initialization succeeded in background thread!" << std::endl;

        // Try a simple query
        IEnumWbemClassObject* pEnum = wmi.ExecQuery(L"SELECT * FROM MSFT_Disk WHERE Number = 0");
        if (pEnum) {
            std::cout << "WMI query succeeded!" << std::endl;
            pEnum->Release();
        } else {
            std::cout << "WMI query failed!" << std::endl;
        }
    } else {
        std::cout << "WMI initialization failed in background thread!" << std::endl;
    }
}

int main() {
    std::cout << "Starting WMI test..." << std::endl;

    // Test WMI in main thread first
    std::cout << "Testing WMI in main thread..." << std::endl;
    WmiStorageManager wmiMain;
    if (wmiMain.Initialize()) {
        std::cout << "WMI initialization succeeded in main thread!" << std::endl;
    } else {
        std::cout << "WMI initialization failed in main thread!" << std::endl;
    }

    // Test WMI in background thread
    std::thread testThread(testWmiInThread);
    testThread.join();

    // Test multiple WMI instances to simulate the issue
    std::cout << "Testing multiple WMI instances..." << std::endl;
    WmiStorageManager wmi1;
    WmiStorageManager wmi2;
    WmiStorageManager wmi3;

    bool result1 = wmi1.Initialize();
    bool result2 = wmi2.Initialize();
    bool result3 = wmi3.Initialize();

    std::cout << "WMI instance 1: " << (result1 ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "WMI instance 2: " << (result2 ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "WMI instance 3: " << (result3 ? "SUCCESS" : "FAILED") << std::endl;

    std::cout << "WMI test completed." << std::endl;
    return 0;
}