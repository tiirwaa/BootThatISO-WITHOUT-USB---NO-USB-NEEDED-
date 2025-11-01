#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <windows.h>

class IniConfigurator {
public:
    IniConfigurator();
    ~IniConfigurator();

    // Configure a single .ini file
    bool configureIniFile(const std::string& filePath);

    // Configure all .ini files in a directory
    void configureIniFilesInDirectory(const std::string& dirPath, std::ofstream& logFile, const char* timestampFunc());

private:
    // Helper methods
    std::string readIniContent(const std::string& filePath);
    void removeUtf8Bom(std::string& content);
    void replacePaths(std::string& content);
    void addExtProgramsComments(std::string& content);
    void writeIniContent(const std::string& filePath, const std::string& content);
};