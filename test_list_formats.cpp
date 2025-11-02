#include <iostream>
#include <string>
#include <vector>
#include "7zip/Archive/IArchive.h"
#include "Windows/PropVariant.h"

extern "C" __declspec(dllimport) HRESULT __stdcall GetNumberOfFormats(UInt32 *numFormats);
extern "C" __declspec(dllimport) HRESULT __stdcall GetHandlerProperty2(UInt32 index, PROPID propID, PROPVARIANT *value);
extern "C" __declspec(dllimport) HRESULT __stdcall CreateArchiver(const GUID *clsid, const GUID *iid, void **outObject);

#include "7zip/Common/FileStreams.h"

int wmain(int argc, wchar_t** wargv) {
    UInt32 n = 0;
    HRESULT hr = GetNumberOfFormats(&n);
    std::cout << "GetNumberOfFormats hr=" << std::hex << hr << std::dec << ", n=" << n << "\n";
    for (UInt32 i = 0; i < n; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kName, &prop) == S_OK) {
            if (prop.vt == VT_BSTR && prop.bstrVal) {
                std::wcout << L"[" << i << L"] " << std::wstring(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal)) << L"\n";
            }
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }

    if (argc >= 2) {
        // try open as UDF (preferred) then ISO by name match
        GUID isoClsid{}; bool foundIso=false;
        GUID udfClsid{}; bool foundUdf=false;
        for (UInt32 i = 0; i < n; ++i) {
            NWindows::NCOM::CPropVariant prop;
            if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kName, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
                std::wstring name(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
                if (name == L"Udf") {
                    NWindows::NCOM::PropVariant_Clear(&prop);
                    if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kClassID, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal && SysStringByteLen(prop.bstrVal)==sizeof(GUID)) {
                        memcpy(&udfClsid, prop.bstrVal, sizeof(GUID));
                        foundUdf = true;
                    }
                }
                if (name == L"Iso") {
                    NWindows::NCOM::PropVariant_Clear(&prop);
                    if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kClassID, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal && SysStringByteLen(prop.bstrVal)==sizeof(GUID)) {
                        memcpy(&isoClsid, prop.bstrVal, sizeof(GUID));
                        foundIso = true;
                    }
                }
            }
            NWindows::NCOM::PropVariant_Clear(&prop);
        }
        const GUID *toTry[2] = { foundUdf ? &udfClsid : nullptr, foundIso ? &isoClsid : nullptr };
        for (int k=0;k<2;k++) if (toTry[k]) {
            CMyComPtr<IInArchive> arc;
            if (CreateArchiver(toTry[k], &IID_IInArchive, (void**)&arc) == S_OK && arc) {
                CInFileStream *fs = new CInFileStream();
                CMyComPtr<IInStream> in = fs;
                if (fs->Open(wargv[1])) {
                    const UInt64 kMaxCheckStartPosition = 1 << 20;
                    HRESULT hr2 = arc->Open(in, &kMaxCheckStartPosition, nullptr);
                    std::wcout << L"Open() hr=" << std::hex << hr2 << std::dec << L"\n";
                    UInt32 numItems=0; HRESULT hr3 = arc->GetNumberOfItems(&numItems);
                    std::wcout << L"GetNumberOfItems hr=" << std::hex << hr3 << std::dec << L" count=" << numItems << L"\n";
                } else {
                    std::wcout << L"Failed to open file: " << wargv[1] << L"\n";
                }
            }
        }
    }
    return 0;
}
