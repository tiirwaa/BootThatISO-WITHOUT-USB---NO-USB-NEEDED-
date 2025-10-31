#include "LocalizationManager.h"
#include "Utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cwctype>
#include <cwchar>
#include <algorithm>
#include "../resource.h"

namespace {
    constexpr wchar_t LANGUAGE_TAG_START[] = L"<language";
    constexpr wchar_t STRING_TAG_START[] = L"<string";
    constexpr wchar_t STRING_TAG_END[] = L"</string>";
}

LocalizationManager& LocalizationManager::getInstance() {
    static LocalizationManager instance;
    return instance;
}

bool LocalizationManager::initialize(const std::wstring& directory) {
    languageDirectory = directory;
    languages.clear();
    if (!std::filesystem::exists(directory)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() != L".xml") {
            continue;
        }

        std::unordered_map<std::string, std::wstring> tmpStrings;
        LanguageInfo info;
        if (parseLanguageFile(path.wstring(), tmpStrings, info)) {
            info.filePath = path.wstring();
            languages.push_back(info);
        }
    }
    return !languages.empty();
}

bool LocalizationManager::hasLanguages() const {
    return !languages.empty();
}

const std::vector<LanguageInfo>& LocalizationManager::getAvailableLanguages() const {
    return languages;
}

bool LocalizationManager::loadLanguageByIndex(size_t index) {
    if (index >= languages.size()) {
        return false;
    }
    return loadLanguageByCode(languages[index].code);
}

bool LocalizationManager::loadLanguageByCode(const std::wstring& code) {
    std::wstring normalized = code;
    std::replace(normalized.begin(), normalized.end(), L'-', L'_');
    for (const auto& language : languages) {
        std::wstring languageCode = language.code;
        std::replace(languageCode.begin(), languageCode.end(), L'-', L'_');
        if (_wcsicmp(languageCode.c_str(), normalized.c_str()) == 0) {
            std::unordered_map<std::string, std::wstring> loadedStrings;
            LanguageInfo info;
            if (parseLanguageFile(language.filePath, loadedStrings, info)) {
                strings = std::move(loadedStrings);
                currentLanguage = language;
                return true;
            }
            return false;
        }
    }
    return false;
}

const std::wstring& LocalizationManager::getWString(const std::string& key) const {
    auto it = strings.find(key);
    if (it != strings.end()) {
        return it->second;
    }
    static const std::wstring empty;
    return empty;
}

std::wstring LocalizationManager::format(const std::string& key, const std::vector<std::wstring>& args) const {
    std::wstring result = getWString(key);
    for (size_t i = 0; i < args.size(); ++i) {
        std::wstring placeholder = L"{" + std::to_wstring(i) + L"}";
        replaceAll(result, placeholder, args[i]);
    }
    return result;
}

std::wstring LocalizationManager::format(const std::string& key, const std::initializer_list<std::wstring>& args) const {
    return format(key, std::vector<std::wstring>(args));
}

std::string LocalizationManager::getUtf8String(const std::string& key) const {
    return Utils::wstring_to_utf8(getWString(key));
}

std::string LocalizationManager::formatUtf8(const std::string& key, const std::vector<std::wstring>& args) const {
    return Utils::wstring_to_utf8(format(key, args));
}

std::string LocalizationManager::formatUtf8(const std::string& key, const std::initializer_list<std::wstring>& args) const {
    return Utils::wstring_to_utf8(format(key, args));
}

const LanguageInfo* LocalizationManager::getCurrentLanguage() const {
    if (currentLanguage.code.empty()) {
        return nullptr;
    }
    return &currentLanguage;
}

namespace {
    struct DialogState {
        LocalizationManager* manager;
        int selectedIndex;
    };

    INT_PTR CALLBACK LanguageDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_INITDIALOG: {
            auto* state = reinterpret_cast<DialogState*>(lParam);
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            if (state && state->manager) {
                const auto& langs = state->manager->getAvailableLanguages();
                const LanguageInfo* current = state->manager->getCurrentLanguage();
                int selectedIndex = 0;
                for (size_t i = 0; i < langs.size(); ++i) {
                    SendDlgItemMessageW(hDlg, IDC_LANGUAGE_COMBO, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(langs[i].name.c_str()));
                    if (current && _wcsicmp(langs[i].code.c_str(), current->code.c_str()) == 0) {
                        selectedIndex = static_cast<int>(i);
                    }
                }
                SendDlgItemMessageW(hDlg, IDC_LANGUAGE_COMBO, CB_SETCURSEL, selectedIndex, 0);
                state->selectedIndex = selectedIndex;
            }
            return TRUE;
        }
        case WM_COMMAND: {
            auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
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
        default:
            break;
        }
        return FALSE;
    }
}

bool LocalizationManager::promptForLanguageSelection(HINSTANCE hInstance, HWND parent) {
    if (languages.empty()) {
        return false;
    }

    DialogState state;
    state.manager = this;
    state.selectedIndex = 0;

    INT_PTR result = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_LANGUAGE_DIALOG), parent, LanguageDialogProc, reinterpret_cast<LPARAM>(&state));
    if (result == IDOK) {
        return loadLanguageByIndex(static_cast<size_t>(state.selectedIndex));
    }
    return false;
}

bool LocalizationManager::parseLanguageFile(const std::wstring& path, std::unordered_map<std::string, std::wstring>& outStrings, LanguageInfo& metadata) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    std::string utf8 = oss.str();
    if (utf8.size() >= 3 && static_cast<unsigned char>(utf8[0]) == 0xEF &&
        static_cast<unsigned char>(utf8[1]) == 0xBB &&
        static_cast<unsigned char>(utf8[2]) == 0xBF) {
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

    auto extractAttribute = [](const std::wstring& input, const std::wstring& attr) -> std::wstring {
        std::wstring pattern = attr + L"=\"";
        size_t pos = input.find(pattern);
        if (pos == std::wstring::npos) return L"";
        pos += pattern.size();
        size_t end = input.find(L"\"", pos);
        if (end == std::wstring::npos) return L"";
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
        if (idStart == std::wstring::npos) break;
        idStart += 4;
        size_t idEnd = content.find(L"\"", idStart);
        if (idEnd == std::wstring::npos) break;
        std::wstring id = content.substr(idStart, idEnd - idStart);

        size_t valueStart = content.find(L">", idEnd);
        if (valueStart == std::wstring::npos) break;
        ++valueStart;
        size_t valueEnd = content.find(STRING_TAG_END, valueStart);
        if (valueEnd == std::wstring::npos) break;
        std::wstring value = content.substr(valueStart, valueEnd - valueStart);
        value = trim(value);
        value = decodeEntities(value);

        outStrings[Utils::wstring_to_utf8(id)] = value;

        pos = content.find(STRING_TAG_START, valueEnd);
    }

    return !outStrings.empty();
}

std::wstring LocalizationManager::decodeEntities(const std::wstring& input) {
    std::wstring output = input;
    replaceAll(output, L"&lt;", L"<");
    replaceAll(output, L"&gt;", L">");
    replaceAll(output, L"&amp;", L"&");
    replaceAll(output, L"&quot;", L"\"");
    replaceAll(output, L"&apos;", L"'");
    return output;
}

std::wstring LocalizationManager::trim(const std::wstring& input) {
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

void LocalizationManager::replaceAll(std::wstring& target, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) {
        return;
    }
    size_t pos = 0;
    while ((pos = target.find(from, pos)) != std::wstring::npos) {
        target.replace(pos, from.length(), to);
        pos += to.length();
    }
}
