#ifndef LOCALIZATION_MANAGER_H
#define LOCALIZATION_MANAGER_H

#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>

struct LanguageInfo {
    std::wstring code;
    std::wstring name;
    int          resourceId;
};

class LocalizationManager {
public:
    static LocalizationManager &getInstance();

    bool                             initialize();
    bool                             hasLanguages() const;
    const std::vector<LanguageInfo> &getAvailableLanguages() const;

    bool loadLanguageByIndex(size_t index);
    bool loadLanguageByCode(const std::wstring &code);

    const std::wstring &getWString(const std::string &key) const;
    std::wstring        format(const std::string &key, const std::vector<std::wstring> &args) const;
    std::wstring        format(const std::string &key, const std::initializer_list<std::wstring> &args) const;
    std::string         getUtf8String(const std::string &key) const;
    std::string         formatUtf8(const std::string &key, const std::vector<std::wstring> &args) const;
    std::string         formatUtf8(const std::string &key, const std::initializer_list<std::wstring> &args) const;

    const LanguageInfo *getCurrentLanguage() const;

    bool promptForLanguageSelection(HINSTANCE hInstance, HWND parent = NULL);

    // Cache management
    void   clearCache();
    size_t getCacheSize() const;

private:
    LocalizationManager()                                       = default;
    LocalizationManager(const LocalizationManager &)            = delete;
    LocalizationManager &operator=(const LocalizationManager &) = delete;

    bool                parseLanguageFile(int resourceId, std::unordered_map<std::string, std::wstring> &outStrings,
                                          LanguageInfo &metadata);
    static std::wstring decodeEntities(const std::wstring &input);
    static std::wstring trim(const std::wstring &input);
    static void         replaceAll(std::wstring &target, const std::wstring &from, const std::wstring &to);

    // Current active strings
    std::unordered_map<std::string, std::wstring> strings;

    // Cache: language code -> parsed strings
    std::unordered_map<std::wstring, std::unordered_map<std::string, std::wstring>> languageCache;

    std::vector<LanguageInfo> languages;
    LanguageInfo              currentLanguage;
};

#endif // LOCALIZATION_MANAGER_H
