#include <iostream>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <string>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

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
        // Initialize COM in MTA mode for this thread
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) && hr != S_FALSE) {
            comInitialized = true;
        } else if (hr == S_FALSE) {
            comInitialized = false;
        } else {
            std::cout << "CoInitializeEx FAILED: hr = 0x" << std::hex << hr << std::dec << std::endl;
            return false;
        }

        hr = CoInitializeSecurity(
            NULL, -1, NULL, NULL,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL, EOAC_NONE, NULL);
        if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
            std::cout << "CoInitializeSecurity FAILED: hr = 0x" << std::hex << hr << std::dec << std::endl;
            return false;
        }

        hr = CoCreateInstance(
            CLSID_WbemLocator, 0,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, (LPVOID*)&pLoc);
        if (FAILED(hr)) {
            std::cout << "CoCreateInstance FAILED: hr = 0x" << std::hex << hr << std::dec << std::endl;
            return false;
        }

        hr = pLoc->ConnectServer(
            _bstr_t(L"ROOT\\Microsoft\\Windows\\Storage"),
            NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (FAILED(hr)) {
            std::cout << "ConnectServer FAILED: hr = 0x" << std::hex << hr << std::dec << std::endl;
            return false;
        }

        hr = CoSetProxyBlanket(
            pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
            NULL, RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
        if (FAILED(hr)) {
            std::cout << "CoSetProxyBlanket FAILED: hr = 0x" << std::hex << hr << std::dec << std::endl;
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
};

int main() {
    std::cout << "=== DIAGNÓSTICO DE PARTICIONES DEL DISCO ===" << std::endl;
    std::cout << "Este programa lista todas las particiones del disco 0 y sus etiquetas de volumen." << std::endl;
    std::cout << "Busca particiones con etiquetas 'ISOBOOT' o 'ISOEFI' que deberían ser eliminadas." << std::endl;
    std::cout << std::endl;

    WmiStorageManager wmi;
    if (!wmi.Initialize()) {
        std::cout << "ERROR: No se pudo inicializar WMI" << std::endl;
        return 1;
    }

    // Query for partitions
    IEnumWbemClassObject* pEnum = wmi.ExecQuery(L"SELECT * FROM MSFT_Partition WHERE DiskNumber = 0");
    if (!pEnum) {
        std::cout << "ERROR: No se pudo consultar particiones" << std::endl;
        return 1;
    }

    IWbemClassObject* pPartition = nullptr;
    ULONG uReturn = 0;
    bool foundTargetPartitions = false;

    std::cout << "PARTICIONES ENCONTRADAS EN EL DISCO 0:" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    while (pEnum->Next(WBEM_INFINITE, 1, &pPartition, &uReturn) == S_OK && uReturn > 0) {
        // Get partition number
        VARIANT varPartitionNumber;
        VariantInit(&varPartitionNumber);
        if (SUCCEEDED(pPartition->Get(L"PartitionNumber", 0, &varPartitionNumber, NULL, NULL))) {
            UINT32 partitionNumber = varPartitionNumber.uintVal;
            VariantClear(&varPartitionNumber);

            // Get drive letter if available
            VARIANT varDriveLetter;
            VariantInit(&varDriveLetter);
            std::string driveLetter = "";
            if (SUCCEEDED(pPartition->Get(L"DriveLetter", 0, &varDriveLetter, NULL, NULL))) {
                if (varDriveLetter.bstrVal) {
                    std::wstring ws(varDriveLetter.bstrVal);
                    driveLetter = std::string(ws.begin(), ws.end());
                }
                VariantClear(&varDriveLetter);
            }

            // Get partition size
            VARIANT varSize;
            VariantInit(&varSize);
            UINT64 sizeGB = 0;
            if (SUCCEEDED(pPartition->Get(L"Size", 0, &varSize, NULL, NULL))) {
                sizeGB = varSize.ullVal / (1024ULL * 1024 * 1024);
                VariantClear(&varSize);
            }

            std::cout << "Partición " << partitionNumber;
            if (!driveLetter.empty()) {
                std::cout << " (Unidad " << driveLetter << ":)";
            }
            std::cout << " - Tamaño: " << sizeGB << " GB" << std::endl;

            // Check if this partition has volumes
            std::string query = "ASSOCIATORS OF {MSFT_Partition.DiskNumber=0,PartitionNumber=" +
                              std::to_string(partitionNumber) + "} WHERE AssocClass=MSFT_PartitionToVolume";
            IEnumWbemClassObject* pVolEnum = wmi.ExecQuery(std::wstring(query.begin(), query.end()).c_str());

            if (pVolEnum) {
                IWbemClassObject* pVolume = nullptr;
                ULONG volReturn = 0;
                bool hasVolumes = false;

                while (pVolEnum->Next(WBEM_INFINITE, 1, &pVolume, &volReturn) == S_OK && volReturn > 0) {
                    hasVolumes = true;

                    VARIANT varLabel;
                    VariantInit(&varLabel);
                    if (SUCCEEDED(pVolume->Get(L"FileSystemLabel", 0, &varLabel, NULL, NULL))) {
                        std::wstring label = varLabel.bstrVal ? varLabel.bstrVal : L"";
                        std::string labelStr(label.begin(), label.end());
                        VariantClear(&varLabel);

                        std::cout << "  └─ Volumen: '" << labelStr << "'";

                        if (label == L"ISOBOOT" || label == L"ISOEFI") {
                            std::cout << " ← *** ESTA SERÍA ELIMINADA ***";
                            foundTargetPartitions = true;
                        }
                        std::cout << std::endl;
                    }
                    pVolume->Release();
                }

                if (!hasVolumes) {
                    std::cout << "  └─ Sin volúmenes asignados" << std::endl;
                }

                pVolEnum->Release();
            } else {
                std::cout << "  └─ Error al consultar volúmenes" << std::endl;
            }
        }
        pPartition->Release();
    }
    pEnum->Release();

    std::cout << std::endl;
    std::cout << "RESULTADO DEL DIAGNÓSTICO:" << std::endl;
    std::cout << "---------------------------" << std::endl;

    if (foundTargetPartitions) {
        std::cout << "✓ Se encontraron particiones con etiquetas 'ISOBOOT' o 'ISOEFI'" << std::endl;
        std::cout << "  Estas particiones deberían ser eliminadas durante la recuperación de espacio." << std::endl;
    } else {
        std::cout << "✗ No se encontraron particiones con etiquetas 'ISOBOOT' o 'ISOEFI'" << std::endl;
        std::cout << "  Por eso el log muestra 'No se encontraron particiones para eliminar'." << std::endl;
        std::cout << "  Esto significa que el disco ya está 'limpio' o las particiones tienen otras etiquetas." << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Posibles causas si esperabas encontrar particiones:" << std::endl;
    std::cout << "1. Las particiones ya fueron eliminadas en una ejecución anterior" << std::endl;
    std::cout << "2. Las particiones tienen etiquetas diferentes (no 'ISOBOOT' o 'ISOEFI')" << std::endl;
    std::cout << "3. Las particiones están en un disco diferente (no el disco 0)" << std::endl;
    std::cout << "4. Error en la creación de particiones en ejecuciones anteriores" << std::endl;

    return 0;
}