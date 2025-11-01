#include "IniConfigurator.h"
#include <iostream>

IniConfigurator::IniConfigurator() {
}

IniConfigurator::~IniConfigurator() {
}

bool IniConfigurator::configureIniFile(const std::string& filePath, const std::string& driveLetter) {
    std::string content = readIniContent(filePath);
    if (content.empty()) {
        return false;
    }
    removeUtf8Bom(content);
    replacePaths(content, driveLetter);
    addExtProgramsComments(content);
    writeIniContent(filePath, content);
    return true;
}

bool IniConfigurator::processIniFile(const std::string& inputPath, const std::string& outputPath, const std::string& driveLetter) {
    std::string content = readIniContent(inputPath);
    if (content.empty()) {
        return false;
    }
    removeUtf8Bom(content);
    replacePaths(content, driveLetter);
    addExtProgramsComments(content);
    writeIniContent(outputPath, content);
    return true;
}

void IniConfigurator::configureIniFilesInDirectory(const std::string& dirPath, std::ofstream& logFile, const char* timestampFunc(), const std::string& driveLetter) {
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((dirPath + "*.ini").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        bool moreFiles = true;
        while (moreFiles) {
            std::string iniName = findData.cFileName;
            std::string iniPath = dirPath + iniName;
            if (configureIniFile(iniPath, driveLetter)) {
                logFile << timestampFunc() << iniName << " reconfigured in destination directory" << std::endl;
            }
            moreFiles = FindNextFileA(hFind, &findData);
        }
        FindClose(hFind);
    }
}

std::string IniConfigurator::readIniContent(const std::string& filePath) {
    std::ifstream iniFile(filePath, std::ios::binary);
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

void IniConfigurator::replacePaths(std::string& content, const std::string& driveLetter) {
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
    // No longer adding comments
}

void IniConfigurator::writeIniContent(const std::string& filePath, const std::string& content) {
    std::ofstream outIniFile(filePath, std::ios::binary);
    outIniFile << content;
    outIniFile.close();
}