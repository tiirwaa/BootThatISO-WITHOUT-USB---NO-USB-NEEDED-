#include "LocalizationManager.h"
#include "LocalizationManager.h"
#include "Utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cwctype>
#include <cwchar>
#include <algorithm>
#include <memory>
#include <gdiplus.h>
#include <objidl.h>
#include "../resource.h"

namespace {
constexpr wchar_t LANGUAGE_TAG_START[] = L"<language";
constexpr wchar_t STRING_TAG_START[]   = L"<string";
constexpr wchar_t STRING_TAG_END[]     = L"</string>";

Gdiplus::Bitmap *LoadBitmapFromResource(int resourceId) {
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hRes)
        return nullptr;
    HGLOBAL hGlob = LoadResource(NULL, hRes);
    if (!hGlob)
        return nullptr;
    LPVOID  pData = LockResource(hGlob);
    DWORD   size  = SizeofResource(NULL, hRes);
    HGLOBAL hMem  = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem)
        return nullptr;
    LPVOID pMem = GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return nullptr;
    }
    memcpy(pMem, pData, size);
    GlobalUnlock(hMem);
    IStream *pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
        GlobalFree(hMem);
        return nullptr;
    }
    Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    return bitmap;
}

HBITMAP CreateScaledBitmapFromResource(int resourceId, int targetWidth, int targetHeight) {
    std::unique_ptr<Gdiplus::Bitmap> bitmap(LoadBitmapFromResource(resourceId));
    if (!bitmap) {
        return nullptr;
    }

    int destWidth  = targetWidth;
    int destHeight = targetHeight;

    if (destWidth <= 0)
        destWidth = static_cast<int>(bitmap->GetWidth());
    if (destHeight <= 0)
        destHeight = static_cast<int>(bitmap->GetHeight());

    if (bitmap->GetWidth() != 0 && bitmap->GetHeight() != 0) {
        double aspect = static_cast<double>(bitmap->GetWidth()) / static_cast<double>(bitmap->GetHeight());
        destHeight    = static_cast<int>(destWidth / aspect);
        if (destHeight > targetHeight && targetHeight > 0) {
            destHeight = targetHeight;
            destWidth  = static_cast<int>(destHeight * aspect);
        }
    }

    destWidth  = (std::max)(destWidth, 1);
    destHeight = (std::max)(destHeight, 1);
    if (targetWidth > 0)
        destWidth = (std::min)(destWidth, targetWidth);
    if (targetHeight > 0)
        destHeight = (std::min)(destHeight, targetHeight);

    Gdiplus::Bitmap   scaled(destWidth, destHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&scaled);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.DrawImage(bitmap.get(), 0, 0, destWidth, destHeight);

    HBITMAP hBitmap = nullptr;
    scaled.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBitmap);
    return hBitmap;
}
} // namespace

LocalizationManager &LocalizationManager::getInstance() {
    static LocalizationManager instance;
    return instance;
}

bool LocalizationManager::initialize() {
    languages.clear();
    languages.push_back({L"en_us", L"English (US)", IDR_EN_US});
    languages.push_back({L"es_cr", L"Español (CR)", IDR_ES_CR});
    return true;
}

bool LocalizationManager::hasLanguages() const {
    return !languages.empty();
}

const std::vector<LanguageInfo> &LocalizationManager::getAvailableLanguages() const {
    return languages;
}

bool LocalizationManager::loadLanguageByIndex(size_t index) {
    if (index >= languages.size()) {
        return false;
    }
    return loadLanguageByCode(languages[index].code);
}

bool LocalizationManager::loadLanguageByCode(const std::wstring &code) {
    std::wstring normalized = code;
    std::replace(normalized.begin(), normalized.end(), L'-', L'_');
    for (const auto &language : languages) {
        std::wstring languageCode = language.code;
        std::replace(languageCode.begin(), languageCode.end(), L'-', L'_');
        if (_wcsicmp(languageCode.c_str(), normalized.c_str()) == 0) {
            std::unordered_map<std::string, std::wstring> loadedStrings;
            LanguageInfo                                  info;
            if (parseLanguageFile(language.resourceId, loadedStrings, info)) {
                strings         = std::move(loadedStrings);
                currentLanguage = language;
                return true;
            }
            return false;
        }
    }
    return false;
}

const std::wstring &LocalizationManager::getWString(const std::string &key) const {
    auto it = strings.find(key);
    if (it != strings.end()) {
        return it->second;
    }
    static const std::wstring empty;
    return empty;
}

std::wstring LocalizationManager::format(const std::string &key, const std::vector<std::wstring> &args) const {
    std::wstring result = getWString(key);
    for (size_t i = 0; i < args.size(); ++i) {
        std::wstring placeholder = L"{" + std::to_wstring(i) + L"}";
        replaceAll(result, placeholder, args[i]);
    }
    return result;
}

std::wstring LocalizationManager::format(const std::string                         &key,
                                         const std::initializer_list<std::wstring> &args) const {
    return format(key, std::vector<std::wstring>(args));
}

std::string LocalizationManager::getUtf8String(const std::string &key) const {
    return Utils::wstring_to_utf8(getWString(key));
}

std::string LocalizationManager::formatUtf8(const std::string &key, const std::vector<std::wstring> &args) const {
    return Utils::wstring_to_utf8(format(key, args));
}

std::string LocalizationManager::formatUtf8(const std::string                         &key,
                                            const std::initializer_list<std::wstring> &args) const {
    return Utils::wstring_to_utf8(format(key, args));
}

const LanguageInfo *LocalizationManager::getCurrentLanguage() const {
    if (currentLanguage.code.empty()) {
        return nullptr;
    }
    return &currentLanguage;
}

namespace {
struct DialogState {
    LocalizationManager *manager;
    int                  selectedIndex;
    HBITMAP              logoBitmap;
};

INT_PTR CALLBACK LanguageDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        auto *state = reinterpret_cast<DialogState *>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        // Center the dialog on the screen
        RECT rc;
        GetWindowRect(hDlg, &rc);
        int width        = rc.right - rc.left;
        int height       = rc.bottom - rc.top;
        int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x            = (screenWidth - width) / 2;
        int y            = (screenHeight - height) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        if (state && state->manager) {
            const auto         &langs         = state->manager->getAvailableLanguages();
            const LanguageInfo *current       = state->manager->getCurrentLanguage();
            int                 selectedIndex = 0;
            for (size_t i = 0; i < langs.size(); ++i) {
                SendDlgItemMessageW(hDlg, IDC_LANGUAGE_COMBO, CB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(langs[i].name.c_str()));
                if (current && _wcsicmp(langs[i].code.c_str(), current->code.c_str()) == 0) {
                    selectedIndex = static_cast<int>(i);
                }
            }
            SendDlgItemMessageW(hDlg, IDC_LANGUAGE_COMBO, CB_SETCURSEL, selectedIndex, 0);
            state->selectedIndex = selectedIndex;
        }

        HICON hIcon = static_cast<HICON>(
            LoadImageW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_SHARED));
        if (hIcon) {
            SendDlgItemMessageW(hDlg, IDC_LANGUAGE_ICON, STM_SETICON, reinterpret_cast<WPARAM>(hIcon), 0);
        }

        if (state) {
            state->logoBitmap = CreateScaledBitmapFromResource(IDR_AG_LOGO, 36, 36);
            if (state->logoBitmap) {
                SendDlgItemMessageW(hDlg, IDC_LANGUAGE_LOGO, STM_SETIMAGE, IMAGE_BITMAP,
                                    reinterpret_cast<LPARAM>(state->logoBitmap));
            }
        }
        return TRUE;
    }
    case WM_COMMAND: {
        auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
        switch (LOWORD(wParam)) {
        case IDOK: {
            if (state && state->manager) {
                LRESULT sel = SendDlgItemMessageW(hDlg, IDC_LANGUAGE_COMBO, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    state->selectedIndex = static_cast<int>(sel);
                    EndDialog(hDlg, IDOK);
                } else {
                    MessageBeep(MB_ICONWARNING);
                }
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    }
    case WM_DESTROY: {
        auto *state = reinterpret_cast<DialogState *>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
        if (state && state->logoBitmap) {
            DeleteObject(state->logoBitmap);
            state->logoBitmap = nullptr;
        }
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
        return TRUE;
    }
    default:
        break;
    }
    return FALSE;
}
} // namespace

bool LocalizationManager::promptForLanguageSelection(HINSTANCE hInstance, HWND parent) {
    if (languages.empty()) {
        return false;
    }

    DialogState state{};
    state.manager       = this;
    state.selectedIndex = 0;
    state.logoBitmap    = nullptr;

    INT_PTR result = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_LANGUAGE_DIALOG), parent, LanguageDialogProc,
                                     reinterpret_cast<LPARAM>(&state));
    if (result == IDOK) {
        return loadLanguageByIndex(static_cast<size_t>(state.selectedIndex));
    }
    return false;
}

bool LocalizationManager::parseLanguageFile(int resourceId, std::unordered_map<std::string, std::wstring> &outStrings,
                                            LanguageInfo &metadata) const {
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hRes) {
        return false;
    }
    HGLOBAL hGlob = LoadResource(NULL, hRes);
    if (!hGlob) {
        return false;
    }
    LPVOID      pData = LockResource(hGlob);
    DWORD       size  = SizeofResource(NULL, hRes);
    std::string utf8(reinterpret_cast<char *>(pData), size);
    if (utf8.size() >= 3 && static_cast<unsigned char>(utf8[0]) == 0xEF &&
        static_cast<unsigned char>(utf8[1]) == 0xBB && static_cast<unsigned char>(utf8[2]) == 0xBF) {
        utf8.erase(0, 3);
    }

    std::wstring content = Utils::utf8_to_wstring(utf8);

    size_t languagePos = content.find(LANGUAGE_TAG_START);
    if (languagePos == std::wstring::npos) {
        return false;
    }

    size_t tagEnd = content.find(L'>', languagePos);
    if (tagEnd == std::wstring::npos) {
        return false;
    }
    std::wstring header = content.substr(languagePos, tagEnd - languagePos + 1);

    auto extractAttribute = [](const std::wstring &input, const std::wstring &attr) -> std::wstring {
        std::wstring pattern = attr + L"=\"";
        size_t       pos     = input.find(pattern);
        if (pos == std::wstring::npos)
            return L"";
        pos += pattern.size();
        size_t end = input.find(L"\"", pos);
        if (end == std::wstring::npos)
            return L"";
        return input.substr(pos, end - pos);
    };

    metadata.code = extractAttribute(header, L"code");
    metadata.name = extractAttribute(header, L"name");
    if (metadata.code.empty() || metadata.name.empty()) {
        return false;
    }

    size_t pos = content.find(STRING_TAG_START, tagEnd);
    while (pos != std::wstring::npos) {
        size_t idStart = content.find(L"id=\"", pos);
        if (idStart == std::wstring::npos)
            break;
        idStart += 4;
        size_t idEnd = content.find(L"\"", idStart);
        if (idEnd == std::wstring::npos)
            break;
        std::wstring id = content.substr(idStart, idEnd - idStart);

        size_t valueStart = content.find(L">", idEnd);
        if (valueStart == std::wstring::npos)
            break;
        ++valueStart;
        size_t valueEnd = content.find(STRING_TAG_END, valueStart);
        if (valueEnd == std::wstring::npos)
            break;
        std::wstring value = content.substr(valueStart, valueEnd - valueStart);
        value              = trim(value);
        value              = decodeEntities(value);

        outStrings[Utils::wstring_to_utf8(id)] = value;

        pos = content.find(STRING_TAG_START, valueEnd);
    }

    return !outStrings.empty();
}

std::wstring LocalizationManager::decodeEntities(const std::wstring &input) {
    std::wstring output = input;
    replaceAll(output, L"&lt;", L"<");
    replaceAll(output, L"&gt;", L">");
    replaceAll(output, L"&amp;", L"&");
    replaceAll(output, L"&quot;", L"\"");
    replaceAll(output, L"&apos;", L"'");
    return output;
}

std::wstring LocalizationManager::trim(const std::wstring &input) {
    size_t start = 0;
    while (start < input.size() && iswspace(input[start])) {
        ++start;
    }
    size_t end = input.size();
    while (end > start && iswspace(input[end - 1])) {
        --end;
    }
    return input.substr(start, end - start);
}

void LocalizationManager::replaceAll(std::wstring &target, const std::wstring &from, const std::wstring &to) {
    if (from.empty()) {
        return;
    }
    size_t pos = 0;
    while ((pos = target.find(from, pos)) != std::wstring::npos) {
        target.replace(pos, from.length(), to);
        pos += to.length();
    }
}
