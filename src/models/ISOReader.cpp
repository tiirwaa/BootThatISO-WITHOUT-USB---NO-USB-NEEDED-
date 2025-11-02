// Real implementation using 7-Zip SDK ISO handler
#include "ISOReader.h"
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <windows.h>
#include <OleAuto.h>

#include "../utils/Utils.h"

// 7-Zip SDK headers
#include "7zip/Archive/IArchive.h"
#include "7zip/PropID.h"
#include "7zip/Common/FileStreams.h"
#include "Common/MyCom.h"
#include "Windows/PropVariant.h"

// Functions exported by ArchiveExports.cpp (linked statically)
STDAPI CreateArchiver(const GUID *clsid, const GUID *iid, void **outObject);
STDAPI GetNumberOfFormats(UInt32 *numFormats);
STDAPI GetHandlerProperty2(UInt32 formatIndex, PROPID propID, PROPVARIANT *value);

namespace {
    // simple UTF conversions
    std::wstring Utf8ToWide(const std::string &s) {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
        return out;
    }
    std::string WideToUtf8(const std::wstring &ws) {
        if (ws.empty()) return std::string();
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    // Obtain CLSID for the "Iso" handler from 7-Zip registry
    bool GetIsoHandlerClsid(GUID &outClsid) {
        UInt32 num = 0;
        if (GetNumberOfFormats(&num) != S_OK) return false;
        for (UInt32 i = 0; i < num; ++i) {
            NWindows::NCOM::CPropVariant prop;
            if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kName, &prop) != S_OK)
                continue;
            bool isIso = false;
            if (prop.vt == VT_BSTR && prop.bstrVal) {
                std::wstring name(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
                if (name == L"Iso") isIso = true;
            }
            NWindows::NCOM::PropVariant_Clear(&prop);
            if (!isIso) continue;

            if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kClassID, &prop) != S_OK)
                continue;
            if (prop.vt == VT_BSTR && prop.bstrVal && SysStringByteLen(prop.bstrVal) == sizeof(GUID)) {
                memcpy(&outClsid, prop.bstrVal, sizeof(GUID));
                NWindows::NCOM::PropVariant_Clear(&prop);
                return true;
            }
            NWindows::NCOM::PropVariant_Clear(&prop);
        }
        return false;
    }

    // Extract callback implementation: writes files under baseDir
    class ExtractCallback : public IArchiveExtractCallback {
    public:
        explicit ExtractCallback(IInArchive *arc, const std::wstring &base)
            : _ref(1), _archive(arc), _baseDir(base) {
            if (!_baseDir.empty() && (_baseDir.back() == L'/' || _baseDir.back() == L'\\')) {
                _baseDir.pop_back();
            }
            if (!_baseDir.empty()) {
                std::error_code ec; std::filesystem::create_directories(_baseDir, ec);
            }
        }

        // IUnknown
        STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject) override {
            if (!ppvObject) return E_POINTER;
            *ppvObject = nullptr;
            if (riid == IID_IUnknown || riid == IID_IArchiveExtractCallback || riid == IID_IProgress) {
                *ppvObject = static_cast<IArchiveExtractCallback*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }
        STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&_ref); }
        STDMETHOD_(ULONG, Release)() override {
            ULONG r = (ULONG)InterlockedDecrement(&_ref);
            if (r == 0) delete this;
            return r;
        }

    // IProgress
    STDMETHOD(SetTotal)(UInt64) override { return S_OK; }
    STDMETHOD(SetCompleted)(const UInt64 *) override { return S_OK; }

        // IArchiveExtractCallback
        STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode) override {
            if (!outStream) return E_POINTER;
            *outStream = nullptr;
            if (askExtractMode != NArchive::NExtract::NAskMode::kExtract) return S_OK;

            // get item path
            NWindows::NCOM::CPropVariant prop;
            std::wstring relPath;
            if (_archive->GetProperty(index, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
                relPath.assign(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            }
            NWindows::NCOM::PropVariant_Clear(&prop);

            // is directory?
            bool isDir = false;
            if (_archive->GetProperty(index, kpidIsDir, &prop) == S_OK && prop.vt == VT_BOOL) {
                isDir = (prop.boolVal != VARIANT_FALSE);
            }
            NWindows::NCOM::PropVariant_Clear(&prop);

            std::filesystem::path outPath = std::filesystem::path(_baseDir) / relPath;
            std::error_code ec;
            if (isDir) {
                std::filesystem::create_directories(outPath, ec);
                return S_OK;
            }

            std::filesystem::create_directories(outPath.parent_path(), ec);

            // create file stream
            CMyComPtr<ISequentialOutStream> out;
            COutFileStream *fileSpec = new COutFileStream();
            out = fileSpec;
            if (!fileSpec->Create(outPath.c_str(), true)) {
                out.Release();
                return S_OK; // skip file on failure to create
            }
            *outStream = out.Detach();
            return S_OK;
        }
        STDMETHOD(PrepareOperation)(Int32) override { return S_OK; }
        STDMETHOD(SetOperationResult)(Int32) override { return S_OK; }

    private:
        ~ExtractCallback() = default;
        LONG _ref;
        IInArchive *_archive; // not owning; Archive manages its lifetime
        std::wstring _baseDir;
    };

    struct OpenResult {
        CMyComPtr<IInArchive> archive;
        CMyComPtr<IInStream> inStream;
        bool ok = false;
    };

    OpenResult OpenIsoArchive(const std::wstring &isoPath) {
        OpenResult res;
        GUID clsid{};
        if (!GetIsoHandlerClsid(clsid)) return res;

        CMyComPtr<IInArchive> arc;
        if (CreateArchiver(&clsid, &IID_IInArchive, (void**)&arc) != S_OK || !arc) {
            return res;
        }

        CInFileStream *fileSpec = new CInFileStream();
        CMyComPtr<IInStream> file = fileSpec;
        if (!fileSpec->Open(isoPath.c_str())) {
            return res;
        }

        const UInt64 kMaxCheckStartPosition = 1 << 20; // 1 MiB scan window
        HRESULT hr = arc->Open(file, &kMaxCheckStartPosition, nullptr);
        if (hr != S_OK) {
            return res;
        }

        res.archive = arc;
        res.inStream = file;
        res.ok = true;
        return res;
    }
}

ISOReader::ISOReader() {}

ISOReader::~ISOReader() {}

std::vector<std::string> ISOReader::listFiles(const std::string &isoPath) {
    std::vector<std::string> files;
    const std::wstring wIso = Utf8ToWide(isoPath);
    auto opened = OpenIsoArchive(wIso);
    if (!opened.ok) return files;

    UInt32 numItems = 0;
    if (opened.archive->GetNumberOfItems(&numItems) != S_OK) return files;
    files.reserve(numItems);

    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            files.emplace_back(WideToUtf8(ws));
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }
    opened.archive->Close();
    return files;
}

bool ISOReader::fileExists(const std::string &isoPath, const std::string &filePath) {
    auto files = listFiles(isoPath);
    std::string lowerFilePath = Utils::toLower(filePath);
    for (const auto &file : files) {
        if (Utils::toLower(file) == lowerFilePath) {
            return true;
        }
    }
    return false;
}

bool ISOReader::extractFile(const std::string &isoPath, const std::string &filePathInISO, const std::string &destPath) {
    const std::wstring wIso = Utf8ToWide(isoPath);
    auto opened = OpenIsoArchive(wIso);
    if (!opened.ok) return false;

    // Find index of the file
    UInt32 numItems = 0;
    if (opened.archive->GetNumberOfItems(&numItems) != S_OK) return false;
    std::wstring target = Utf8ToWide(filePathInISO);
    Int32 found = -1;
    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            if (_wcsicmp(ws.c_str(), target.c_str()) == 0) {
                found = (Int32)i;
                NWindows::NCOM::PropVariant_Clear(&prop);
                break;
            }
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }
    if (found < 0) return false;

    // Prepare destination directory
    createDirectories(destPath);

    // Extract only that item
    CMyComPtr<IArchiveExtractCallback> cb = new ExtractCallback(opened.archive, Utf8ToWide(destPath));
    UInt32 index = (UInt32)found;
    HRESULT hr = opened.archive->Extract(&index, 1, 0, cb);
    opened.archive->Close();
    return hr == S_OK;
}

bool ISOReader::extractFiles(const std::string &isoPath, const std::vector<std::string> &filesInISO, const std::string &destDir) {
    const std::wstring wIso = Utf8ToWide(isoPath);
    auto opened = OpenIsoArchive(wIso);
    if (!opened.ok) return false;

    UInt32 numItems = 0;
    if (opened.archive->GetNumberOfItems(&numItems) != S_OK) return false;
    std::vector<UInt32> indices;
    indices.reserve(filesInISO.size());

    // Build lookup set in lowercase
    std::vector<std::wstring> wanted;
    for (const auto &s : filesInISO) {
        std::wstring ws = Utf8ToWide(Utils::toLower(s));
        wanted.push_back(ws);
    }

    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            std::wstring low;
            low.resize(ws.size());
            std::transform(ws.begin(), ws.end(), low.begin(), ::towlower);
            for (const auto &w : wanted) {
                if (low == w) {
                    indices.push_back(i);
                    break;
                }
            }
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }
    if (indices.empty()) return false;

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    createDirectories(destDir);
    CMyComPtr<IArchiveExtractCallback> cb = new ExtractCallback(opened.archive, Utf8ToWide(destDir));
    HRESULT hr = opened.archive->Extract(indices.data(), (UInt32)indices.size(), 0, cb);
    opened.archive->Close();
    return hr == S_OK;
}

bool ISOReader::extractAll(const std::string &isoPath, const std::string &destDir, const std::vector<std::string> &excludePatterns) {
    (void)excludePatterns; // TODO: add pattern filtering if needed
    const std::wstring wIso = Utf8ToWide(isoPath);
    auto opened = OpenIsoArchive(wIso);
    if (!opened.ok) return false;

    createDirectories(destDir);
    CMyComPtr<IArchiveExtractCallback> cb = new ExtractCallback(opened.archive, Utf8ToWide(destDir));
    HRESULT hr = opened.archive->Extract(nullptr, (UInt32)(Int32)-1, 0, cb);
    opened.archive->Close();
    return hr == S_OK;
}

bool ISOReader::extractDirectory(const std::string &isoPath, const std::string &dirPathInISO, const std::string &destDir) {
    const std::wstring wIso = Utf8ToWide(isoPath);
    auto opened = OpenIsoArchive(wIso);
    if (!opened.ok) return false;

    UInt32 numItems = 0;
    if (opened.archive->GetNumberOfItems(&numItems) != S_OK) return false;
    std::wstring dir = Utf8ToWide(Utils::toLower(dirPathInISO));
    if (!dir.empty() && dir.back() != L'/' && dir.back() != L'\\') dir.push_back(L'/');

    std::vector<UInt32> indices;
    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            std::wstring low;
            low.resize(ws.size());
            std::transform(ws.begin(), ws.end(), low.begin(), ::towlower);
            if (low.rfind(dir, 0) == 0) { // starts with dir
                indices.push_back(i);
            }
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }
    if (indices.empty()) return false;

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    createDirectories(destDir);
    CMyComPtr<IArchiveExtractCallback> cb = new ExtractCallback(opened.archive, Utf8ToWide(destDir));
    HRESULT hr = opened.archive->Extract(indices.data(), (UInt32)indices.size(), 0, cb);
    opened.archive->Close();
    return hr == S_OK;
}

void ISOReader::createDirectories(const std::string &path) {
    std::filesystem::path p(path);
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
}
