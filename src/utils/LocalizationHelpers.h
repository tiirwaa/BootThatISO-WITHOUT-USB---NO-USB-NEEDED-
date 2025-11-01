#ifndef LOCALIZATION_HELPERS_H
#define LOCALIZATION_HELPERS_H

#include "LocalizationManager.h"
#include "Utils.h"
#include <string>
#include <vector>

inline std::wstring LocalizedOrW(const std::string &key, const wchar_t *fallback) {
    std::wstring value = LocalizationManager::getInstance().getWString(key);
    if (value.empty() && fallback) {
        return std::wstring(fallback);
    }
    return value;
}

inline std::string LocalizedOrUtf8(const std::string &key, const char *fallback) {
    std::wstring value = LocalizationManager::getInstance().getWString(key);
    if (value.empty()) {
        return fallback ? std::string(fallback) : std::string();
    }
    return Utils::wstring_to_utf8(value);
}

inline std::wstring LocalizedFormatW(const std::string &key, const std::vector<std::wstring> &args,
                                     const wchar_t *fallback = nullptr) {
    std::wstring formatted = LocalizationManager::getInstance().format(key, args);
    if (formatted.empty() && fallback) {
        return std::wstring(fallback);
    }
    return formatted;
}

inline std::wstring LocalizedFormatW(const std::string &key, const std::initializer_list<std::wstring> &args,
                                     const wchar_t *fallback = nullptr) {
    return LocalizedFormatW(key, std::vector<std::wstring>(args), fallback);
}

inline std::string LocalizedFormatUtf8(const std::string &key, const std::vector<std::wstring> &args,
                                       const char *fallback = nullptr) {
    std::wstring formatted = LocalizationManager::getInstance().format(key, args);
    if (formatted.empty()) {
        return fallback ? std::string(fallback) : std::string();
    }
    return Utils::wstring_to_utf8(formatted);
}

inline std::string LocalizedFormatUtf8(const std::string &key, const std::initializer_list<std::wstring> &args,
                                       const char *fallback = nullptr) {
    return LocalizedFormatUtf8(key, std::vector<std::wstring>(args), fallback);
}

#endif // LOCALIZATION_HELPERS_H
