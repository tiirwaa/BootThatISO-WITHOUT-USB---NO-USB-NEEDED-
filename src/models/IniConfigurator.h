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
    bool configureIniFile(const std::string &filePath, const std::string &driveLetter);

    // Process a single .ini file from input to output
    bool processIniFile(const std::string &inputPath, const std::string &outputPath, const std::string &driveLetter);

    // Configure all .ini files in a directory
    void configureIniFilesInDirectory(const std::string &dirPath, std::ofstream &logFile, const char *timestampFunc(),
                                      const std::string &driveLetter);

private:
    // Helper methods
    std::string readIniContent(const std::string &filePath);
    void        writeIniContent(const std::string &filePath, const std::string &content);
};