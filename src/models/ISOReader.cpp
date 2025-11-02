// Real implementation using 7-Zip SDK ISO handler
#include "ISOReader.h"
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
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

    inline void NormalizeSlashes(std::wstring &ws) {
        for (auto &ch : ws) if (ch == L'\\') ch = L'/';
    }
    inline void NormalizeSlashes(std::string &s) {
        for (auto &ch : s) if (ch == '\\') ch = '/';
    }

    // Obtain CLSID for the most suitable handler for optical images: prefer Udf, then Iso, then Ext
    bool GetIsoHandlerClsid(GUID &outClsid) {
        UInt32 num = 0;
        if (GetNumberOfFormats(&num) != S_OK) return false;

        auto findByName = [&](const wchar_t* wantName, GUID &clsidOut) -> bool {
            for (UInt32 i = 0; i < num; ++i) {
                NWindows::NCOM::CPropVariant prop;
                if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kName, &prop) != S_OK) {
                    continue;
                }
                bool match = false;
                if (prop.vt == VT_BSTR && prop.bstrVal) {
                    std::wstring name(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
                    if (name == wantName) match = true;
                }
                NWindows::NCOM::PropVariant_Clear(&prop);
                if (!match) continue;

                if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kClassID, &prop) != S_OK) {
                    continue;
                }
                if (prop.vt == VT_BSTR && prop.bstrVal && SysStringByteLen(prop.bstrVal) == sizeof(GUID)) {
                    memcpy(&clsidOut, prop.bstrVal, sizeof(GUID));
                    NWindows::NCOM::PropVariant_Clear(&prop);
                    return true;
                }
                NWindows::NCOM::PropVariant_Clear(&prop);
            }
            return false;
        };

        if (findByName(L"Udf", outClsid)) return true;
        if (findByName(L"Iso", outClsid)) return true;
        if (findByName(L"Ext", outClsid)) return true; // last resort
        return false;
    }

    // Extract callback implementation: writes files under baseDir
    class ExtractCallback : public IArchiveExtractCallback {
    public:
        explicit ExtractCallback(IInArchive *arc, const std::wstring &base,
                                 const std::unordered_map<UInt32, std::wstring> *overridePaths = nullptr)
            : _ref(1), _archive(arc), _baseDir(base), _overridePaths(overridePaths) {
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

            std::filesystem::path outPath;
            bool forcePath = false;
            if (_overridePaths) {
                auto it = _overridePaths->find(index);
                if (it != _overridePaths->end()) {
                    outPath = std::filesystem::path(it->second);
                    forcePath = true;
                }
            }
            if (!forcePath) {
                outPath = std::filesystem::path(_baseDir);
                if (!relPath.empty()) {
                    outPath /= relPath;
                }
            }

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
        const std::unordered_map<UInt32, std::wstring> *_overridePaths;
    };

    struct OpenResult {
        CMyComPtr<IInArchive> archive;     // the actual filesystem archive (UDF/ISO)
        CMyComPtr<IInStream> inStream;     // stream backing 'archive'
        CMyComPtr<IUnknown> outerArchive;  // holds outer Ext archive alive if substream is used
        bool ok = false;
    };

    OpenResult OpenIsoArchive(const std::wstring &isoPath) {
        OpenResult res;
        const UInt64 kMaxCheckStartPosition = 1 << 20; // 1 MiB scan window

        // Open base file stream
        CInFileStream *fileSpec = new CInFileStream();
        CMyComPtr<IInStream> file = fileSpec;
        if (!fileSpec->Open(isoPath.c_str())) {
            return res;
        }

        auto tryOpenWithClsid = [&](const GUID &clsid, CMyComPtr<IInArchive> &outArc, CMyComPtr<IInStream> &in) -> HRESULT {
            outArc.Release();
            if (CreateArchiver(&clsid, &IID_IInArchive, (void**)&outArc) != S_OK || !outArc) return E_FAIL;
            return outArc->Open(in, &kMaxCheckStartPosition, nullptr);
        };

        // no-op helper removed; we resolve CLSIDs below directly

        // 1) Try opening via Ext, then unwrap main substream as Udf or Iso
        GUID extClsid{}; GUID udfClsid{}; GUID isoClsid{};
        bool haveExt=false, haveUdf=false, haveIso=false;
        {
            // We reuse GetIsoHandlerClsid to fetch specific names
            UInt32 num=0; if (GetNumberOfFormats(&num)==S_OK) {
                for (UInt32 i=0;i<num;++i){
                    NWindows::NCOM::CPropVariant prop;
                    if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kName, &prop)==S_OK && prop.vt==VT_BSTR && prop.bstrVal){
                        std::wstring n(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
                        NWindows::NCOM::PropVariant_Clear(&prop);
                        if (n==L"Ext" || n==L"Udf" || n==L"Iso"){
                            if (GetHandlerProperty2(i, NArchive::NHandlerPropID::kClassID, &prop)==S_OK && prop.vt==VT_BSTR && prop.bstrVal && SysStringByteLen(prop.bstrVal)==sizeof(GUID)){
                                if (n==L"Ext") { memcpy(&extClsid, prop.bstrVal, sizeof(GUID)); haveExt=true; }
                                if (n==L"Udf") { memcpy(&udfClsid, prop.bstrVal, sizeof(GUID)); haveUdf=true; }
                                if (n==L"Iso") { memcpy(&isoClsid, prop.bstrVal, sizeof(GUID)); haveIso=true; }
                            }
                        }
                    }
                    NWindows::NCOM::PropVariant_Clear(&prop);
                }
            }
        }

        if (haveExt) {
            CMyComPtr<IInArchive> extArc;
            if (tryOpenWithClsid(extClsid, extArc, file) == S_OK && extArc) {
                // Query main subfile index
                NWindows::NCOM::CPropVariant prop;
                UInt32 mainSub = (UInt32)(Int32)-1;
                if (extArc->GetArchiveProperty(kpidMainSubfile, &prop) == S_OK && prop.vt == VT_UI4) {
                    mainSub = prop.ulVal;
                }
                NWindows::NCOM::PropVariant_Clear(&prop);

                if (mainSub != (UInt32)(Int32)-1) {
                    CMyComPtr<IInArchiveGetStream> getStream;
                    if (extArc->QueryInterface(IID_IInArchiveGetStream, (void**)&getStream) == S_OK && getStream) {
                        CMyComPtr<ISequentialInStream> subSeq;
                        if (getStream->GetStream(mainSub, &subSeq) == S_OK && subSeq) {
                            CMyComPtr<IInStream> sub;
                            if (subSeq.QueryInterface(IID_IInStream, &sub) == S_OK && sub) {
                                // Prefer UDF, then ISO
                                if (haveUdf && tryOpenWithClsid(udfClsid, res.archive, sub) == S_OK) {
                                    res.inStream = sub;
                                    res.outerArchive = extArc; // keep ext alive
                                    res.ok = true;
                                    return res;
                                }
                                if (haveIso && tryOpenWithClsid(isoClsid, res.archive, sub) == S_OK) {
                                    res.inStream = sub;
                                    res.outerArchive = extArc;
                                    res.ok = true;
                                    return res;
                                }
                            }
                        }
                    }
                }
                // If we couldn't unwrap, we still can fallback to direct open below
            }
        }

        // 2) Fallback: open directly as Udf, then Iso
        if (haveUdf) {
            CMyComPtr<IInArchive> arc;
            if (tryOpenWithClsid(udfClsid, arc, file) == S_OK) {
                res.archive = arc;
                res.inStream = file;
                res.ok = true;
                return res;
            }
        }
        if (haveIso) {
            CMyComPtr<IInArchive> arc;
            if (tryOpenWithClsid(isoClsid, arc, file) == S_OK) {
                res.archive = arc;
                res.inStream = file;
                res.ok = true;
                return res;
            }
        }

        return res;
    }
}

ISOReader::ISOReader() {}

ISOReader::~ISOReader() {}

std::vector<std::string> ISOReader::listFiles(const std::string &isoPath) {
    std::vector<std::string> files;
    const std::wstring wIso = Utf8ToWide(isoPath);
    auto opened = OpenIsoArchive(wIso);
    if (!opened.ok) {
        return files;
    }

    UInt32 numItems = 0;
    if (opened.archive->GetNumberOfItems(&numItems) != S_OK) {
        return files;
    }
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
    std::string target = filePath;
    NormalizeSlashes(target);
    target = Utils::toLower(target);
    for (auto file : files) {
        NormalizeSlashes(file);
        if (Utils::toLower(file) == target) return true;
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
    NormalizeSlashes(target);
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);
    Int32 found = -1;
    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            NormalizeSlashes(ws);
            std::transform(ws.begin(), ws.end(), ws.begin(), ::towlower);
            if (ws == target) {
                found = (Int32)i;
                NWindows::NCOM::PropVariant_Clear(&prop);
                break;
            }
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }
    if (found < 0) return false;

    // Prepare destination directory for the target file
    std::filesystem::path destFs(destPath);
    std::string destDir = destFs.parent_path().u8string();
    if (destDir.empty()) {
        // If there is no parent (file in current dir), ensure at least base dir is set
        destDir = destFs.has_root_path() ? destFs.root_path().u8string() : std::string(".");
    }
    createDirectories(destDir);

    // Extract only that item into the requested destination path
    std::unordered_map<UInt32, std::wstring> overrides;
    overrides.emplace(static_cast<UInt32>(found), Utf8ToWide(destPath));
    CMyComPtr<IArchiveExtractCallback> cb =
        new ExtractCallback(opened.archive, Utf8ToWide(destDir), &overrides);
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

    // Build lookup set in lowercase with normalized slashes
    std::vector<std::wstring> wanted;
    for (const auto &s : filesInISO) {
        std::string norm = s;
        NormalizeSlashes(norm);
        std::wstring ws = Utf8ToWide(Utils::toLower(norm));
        wanted.push_back(ws);
    }

    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            NormalizeSlashes(ws);
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
    std::string normDir = dirPathInISO;
    NormalizeSlashes(normDir);
    std::wstring dir = Utf8ToWide(Utils::toLower(normDir));
    if (!dir.empty() && dir.back() != L'/' && dir.back() != L'\\') dir.push_back(L'/');

    std::vector<UInt32> indices;
    std::unordered_map<UInt32, std::wstring> overrides;
    std::filesystem::path base = std::filesystem::u8path(destDir);

    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            NormalizeSlashes(ws);
            std::wstring low;
            low.resize(ws.size());
            std::transform(ws.begin(), ws.end(), low.begin(), ::towlower);
            if (low.rfind(dir, 0) == 0) { // starts with dir
                indices.push_back(i);
                std::wstring rel = ws.substr(dir.size());
                while (!rel.empty() && (rel.front() == L'/' || rel.front() == L'\\')) {
                    rel.erase(rel.begin());
                }
                std::filesystem::path relPath(rel);
                std::filesystem::path outPath = base;
                if (!rel.empty()) {
                    outPath /= relPath;
                }
                overrides.emplace(i, outPath.native());
            }
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }
    if (indices.empty()) return false;

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    createDirectories(destDir);
    CMyComPtr<IArchiveExtractCallback> cb =
        new ExtractCallback(opened.archive, Utf8ToWide(destDir), overrides.empty() ? nullptr : &overrides);
    HRESULT hr = opened.archive->Extract(indices.data(), (UInt32)indices.size(), 0, cb);
    opened.archive->Close();
    return hr == S_OK;
}

void ISOReader::createDirectories(const std::string &path) {
    std::filesystem::path p(path);
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
}

bool ISOReader::getFileSize(const std::string &isoPath, const std::string &filePathInISO, unsigned long long &sizeOut) {
    sizeOut = 0ULL;
    const std::wstring wIso = Utf8ToWide(isoPath);
    auto opened = OpenIsoArchive(wIso);
    if (!opened.ok) return false;

    UInt32 numItems = 0;
    if (opened.archive->GetNumberOfItems(&numItems) != S_OK) return false;
    std::wstring target = Utf8ToWide(filePathInISO);
    NormalizeSlashes(target);
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);
    Int32 found = -1;
    for (UInt32 i = 0; i < numItems; ++i) {
        NWindows::NCOM::CPropVariant prop;
        if (opened.archive->GetProperty(i, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal) {
            std::wstring ws(prop.bstrVal, prop.bstrVal + SysStringLen(prop.bstrVal));
            NormalizeSlashes(ws);
            std::transform(ws.begin(), ws.end(), ws.begin(), ::towlower);
            if (ws == target) {
                found = (Int32)i;
                NWindows::NCOM::PropVariant_Clear(&prop);
                break;
            }
        }
        NWindows::NCOM::PropVariant_Clear(&prop);
    }
    if (found < 0) { opened.archive->Close(); return false; }

    NWindows::NCOM::CPropVariant sizeProp;
    if (opened.archive->GetProperty((UInt32)found, kpidSize, &sizeProp) == S_OK) {
        if (sizeProp.vt == VT_UI8) {
            sizeOut = static_cast<unsigned long long>(sizeProp.uhVal.QuadPart);
        } else if (sizeProp.vt == VT_EMPTY) {
            sizeOut = 0ULL;
        }
    }
    NWindows::NCOM::PropVariant_Clear(&sizeProp);
    opened.archive->Close();
    return (found >= 0);
}
