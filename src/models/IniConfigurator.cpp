#include "IniConfigurator.h"
#include <iostream>

IniConfigurator::IniConfigurator() {
}

IniConfigurator::~IniConfigurator() {
}

bool IniConfigurator::configureIniFile(const std::string& filePath) {
    std::string content = readIniContent(filePath);
    if (content.empty()) {
        return false;
    }
    removeUtf8Bom(content);
    replacePaths(content);
    addExtProgramsComments(content);
    writeIniContent(filePath, content);
    return true;
}

void IniConfigurator::configureIniFilesInDirectory(const std::string& dirPath, std::ofstream& logFile, const char* timestampFunc()) {
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((dirPath + "*.ini").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        bool moreFiles = true;
        while (moreFiles) {
            std::string iniName = findData.cFileName;
            std::string iniPath = dirPath + iniName;
            if (configureIniFile(iniPath)) {
                logFile << timestampFunc() << iniName << " reconfigured in destination directory" << std::endl;
            }
            moreFiles = FindNextFileA(hFind, &findData);
        }
        FindClose(hFind);
    }
}

std::string IniConfigurator::readIniContent(const std::string& filePath) {
    std::ifstream iniFile(filePath);
    if (!iniFile.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << iniFile.rdbuf();
    iniFile.close();
    return buffer.str();
}

void IniConfigurator::removeUtf8Bom(std::string& content) {
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
    }
}

void IniConfigurator::replacePaths(std::string& content) {
    size_t pos = 0;
    while ((pos = content.find("Y:\\", pos)) != std::string::npos) {
        content.replace(pos, 3, "X:\\");
        pos += 3;
    }
    pos = 0;
    while ((pos = content.find("Y:/", pos)) != std::string::npos) {
        content.replace(pos, 3, "X:/");
        pos += 3;
    }
}

void IniConfigurator::addExtProgramsComments(std::string& content) {
    size_t extPos = content.find("_SUB ExtPrograms");
    if (extPos != std::string::npos) {
        size_t commentPos = content.find("// EXEC X:\\Programs\\Sysinternals_Process_Monitor\\procmon.exe", extPos);
        if (commentPos != std::string::npos) {
            size_t insertPos = content.find('\n', commentPos) + 1;
            std::string toInsert = "\t// Agrega aquí EXEC para programas de la carpeta Programs\n\t// Ejemplo: EXEC X:\\Programs\\TuPrograma.exe\n";
            content.insert(insertPos, toInsert);
        }
    }
}

void IniConfigurator::writeIniContent(const std::string& filePath, const std::string& content) {
    std::ofstream outIniFile(filePath);
    outIniFile << content;
    outIniFile.close();
}