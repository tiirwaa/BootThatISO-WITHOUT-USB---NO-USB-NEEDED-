/**
 * Herramienta de validación de archivos de traducción
 * 
 * Verifica que:
 * 1. Todas las claves en el código existen en los archivos de idioma
 * 2. Todas las claves tienen traducciones en todos los idiomas
 * 3. No hay claves duplicadas
 * 4. No hay claves huérfanas (en XML pero no en código)
 */

#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <regex>
#include <algorithm>

namespace fs = std::filesystem;

struct ValidationResult {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> info;
    
    bool hasErrors() const { return !errors.empty(); }
    bool hasWarnings() const { return !warnings.empty(); }
    
    void addError(const std::string& msg) { errors.push_back(msg); }
    void addWarning(const std::string& msg) { warnings.push_back(msg); }
    void addInfo(const std::string& msg) { info.push_back(msg); }
};

// Estructura para almacenar información de un archivo de idioma
struct LanguageFile {
    std::string path;
    std::string code;
    std::string name;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_set<std::string> duplicates;
};

// Lee un archivo XML de idioma
bool parseLanguageXml(const std::string& filePath, LanguageFile& langFile) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Remover BOM UTF-8 si existe
    if (content.size() >= 3 && 
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }
    
    langFile.path = filePath;
    
    // Extraer code y name del tag <language>
    std::regex langRegex("<language\\s+code=\"([^\"]+)\"\\s+name=\"([^\"]+)\"");
    std::smatch langMatch;
    if (std::regex_search(content, langMatch, langRegex)) {
        langFile.code = langMatch[1];
        langFile.name = langMatch[2];
    }
    
    // Extraer todas las strings
    std::regex stringRegex("<string\\s+id=\"([^\"]+)\">([^<]*)</string>");
    auto stringsBegin = std::sregex_iterator(content.begin(), content.end(), stringRegex);
    auto stringsEnd = std::sregex_iterator();
    
    for (auto it = stringsBegin; it != stringsEnd; ++it) {
        std::string id = (*it)[1];
        std::string value = (*it)[2];
        
        // Detectar duplicados
        if (langFile.strings.find(id) != langFile.strings.end()) {
            langFile.duplicates.insert(id);
        }
        
        langFile.strings[id] = value;
    }
    
    return true;
}

// Busca todas las llamadas a funciones de localización en el código
std::unordered_set<std::string> findKeysInCode(const std::string& rootPath) {
    std::unordered_set<std::string> keys;
    
    // Regex para capturar múltiples formas de acceso a traducciones:
    // - LocalizedOrUtf8("key", ...)
    // - LocalizedOrW("key", ...)
    // - LocalizedFormatW("key", ...)
    // - LocalizedFormatUtf8("key", ...)
    // - getWString("key")
    // - getUtf8String("key")
    // - format("key", ...)
    // - formatUtf8("key", ...)
    std::regex keyRegex("(?:LocalizedOr(?:Utf8|W)|LocalizedFormat(?:W|Utf8)|get(?:W|Utf8)String|format(?:Utf8)?)\\s*\\(\\s*\"([a-zA-Z0-9_.]+)\"");
    
    // Buscar en todos los archivos .cpp y .h
    for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
        if (!entry.is_regular_file()) continue;
        
        std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        
        // Excluir ciertos directorios
        std::string pathStr = entry.path().string();
        if (pathStr.find("build") != std::string::npos ||
            pathStr.find("third-party") != std::string::npos ||
            pathStr.find(".git") != std::string::npos) {
            continue;
        }
        
        std::ifstream file(entry.path());
        if (!file.is_open()) continue;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        auto keysBegin = std::sregex_iterator(content.begin(), content.end(), keyRegex);
        auto keysEnd = std::sregex_iterator();
        
        for (auto it = keysBegin; it != keysEnd; ++it) {
            keys.insert((*it)[1]);
        }
    }
    
    return keys;
}

// Valida la consistencia entre archivos de idioma
void validateLanguageFiles(const std::vector<LanguageFile>& langFiles, 
                          const std::unordered_set<std::string>& codeKeys,
                          ValidationResult& result) {
    if (langFiles.empty()) {
        result.addError("No se encontraron archivos de idioma");
        return;
    }
    
    result.addInfo("Archivos de idioma encontrados: " + std::to_string(langFiles.size()));
    for (const auto& lang : langFiles) {
        result.addInfo("  - " + lang.name + " (" + lang.code + "): " + 
                      std::to_string(lang.strings.size()) + " strings");
    }
    
    // Verificar duplicados en cada archivo
    for (const auto& lang : langFiles) {
        if (!lang.duplicates.empty()) {
            result.addError("Archivo " + lang.code + " tiene claves duplicadas:");
            for (const auto& dup : lang.duplicates) {
                result.addError("  - " + dup);
            }
        }
    }
    
    // Tomar el primer idioma como referencia
    const auto& referenceLang = langFiles[0];
    std::unordered_set<std::string> referenceKeys;
    for (const auto& pair : referenceLang.strings) {
        referenceKeys.insert(pair.first);
    }
    
    // Verificar que todos los idiomas tengan las mismas claves
    for (size_t i = 1; i < langFiles.size(); ++i) {
        const auto& lang = langFiles[i];
        
        // Claves faltantes
        for (const auto& key : referenceKeys) {
            if (lang.strings.find(key) == lang.strings.end()) {
                result.addError("Clave '" + key + "' falta en " + lang.code);
            }
        }
        
        // Claves extras
        for (const auto& pair : lang.strings) {
            if (referenceKeys.find(pair.first) == referenceKeys.end()) {
                result.addError("Clave '" + pair.first + "' en " + lang.code + 
                              " pero no en " + referenceLang.code);
            }
        }
    }
    
    // Verificar strings vacías
    for (const auto& lang : langFiles) {
        for (const auto& pair : lang.strings) {
            std::string trimmed = pair.second;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
            trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
            
            if (trimmed.empty()) {
                result.addWarning("Traducción vacía en " + lang.code + ": " + pair.first);
            }
        }
    }
    
    // Verificar que todas las claves del código existan en los archivos de idioma
    result.addInfo("\nClaves encontradas en código: " + std::to_string(codeKeys.size()));
    for (const auto& key : codeKeys) {
        bool found = false;
        for (const auto& lang : langFiles) {
            if (lang.strings.find(key) != lang.strings.end()) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            result.addError("Clave '" + key + "' usada en código pero no definida en archivos de idioma");
        }
    }
    
    // Verificar claves huérfanas (en XML pero no en código)
    for (const auto& key : referenceKeys) {
        if (codeKeys.find(key) == codeKeys.end()) {
            result.addWarning("Clave '" + key + "' definida en XML pero no usada en código");
        }
    }
}

void printResults(const ValidationResult& result) {
    std::cout << "\n=== RESULTADOS DE VALIDACION ===\n\n";
    
    if (!result.info.empty()) {
        std::cout << "INFORMACION:\n";
        for (const auto& msg : result.info) {
            std::cout << msg << "\n";
        }
        std::cout << "\n";
    }
    
    if (!result.warnings.empty()) {
        std::cout << "ADVERTENCIAS (" << result.warnings.size() << "):\n";
        for (const auto& msg : result.warnings) {
            std::cout << "  [!] " << msg << "\n";
        }
        std::cout << "\n";
    }
    
    if (!result.errors.empty()) {
        std::cout << "ERRORES (" << result.errors.size() << "):\n";
        for (const auto& msg : result.errors) {
            std::cout << "  [X] " << msg << "\n";
        }
        std::cout << "\n";
    }
    
    if (!result.hasErrors() && !result.hasWarnings()) {
        std::cout << "[OK] Todas las validaciones pasaron exitosamente!\n\n";
    } else {
        std::cout << "RESUMEN:\n";
        std::cout << "  Errores: " << result.errors.size() << "\n";
        std::cout << "  Advertencias: " << result.warnings.size() << "\n\n";
    }
}

int main(int argc, char* argv[]) {
    // Set console to UTF-8 on Windows
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    #endif
    
    std::string rootPath = argc > 1 ? argv[1] : "..";
    
    std::cout << "Validador de Traducciones - EasyISOBoot\n";
    std::cout << "========================================\n\n";
    std::cout << "Directorio raiz: " << fs::absolute(rootPath) << "\n\n";
    
    ValidationResult result;
    
    // Buscar archivos de idioma
    std::vector<LanguageFile> langFiles;
    std::string langDir = rootPath + "/lang";
    
    if (!fs::exists(langDir)) {
        std::cerr << "Error: No se encontró el directorio 'lang' en " << rootPath << "\n";
        return 1;
    }
    
    for (const auto& entry : fs::directory_iterator(langDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".xml") {
            LanguageFile langFile;
            if (parseLanguageXml(entry.path().string(), langFile)) {
                langFiles.push_back(langFile);
            } else {
                result.addError("No se pudo parsear: " + entry.path().string());
            }
        }
    }
    
    // Buscar claves en el código
    std::cout << "Escaneando archivos de código fuente...\n";
    std::string srcDir = rootPath + "/src";
    auto codeKeys = findKeysInCode(srcDir);
    
    // Validar
    std::cout << "Validando traducciones...\n";
    validateLanguageFiles(langFiles, codeKeys, result);
    
    // Imprimir resultados
    printResults(result);
    
    return result.hasErrors() ? 1 : 0;
}
