#ifndef ISOREADER_H
#define ISOREADER_H

#include <string>
#include <vector>
#include <functional>

class ISOReader {
public:
    ISOReader();
    ~ISOReader();

    // List all files in the ISO
    std::vector<std::string> listFiles(const std::string &isoPath);

    // Check if a file exists in the ISO
    bool fileExists(const std::string &isoPath, const std::string &filePath);

    // Extract a file from ISO to destination
    bool extractFile(const std::string &isoPath, const std::string &filePathInISO, const std::string &destPath);

    // Extract multiple files
    bool extractFiles(const std::string &isoPath, const std::vector<std::string> &filesInISO, const std::string &destDir);

    // Extract all files to destDir, with optional exclude patterns
    bool extractAll(const std::string &isoPath, const std::string &destDir, const std::vector<std::string> &excludePatterns = {});

    // Extract a directory from ISO to destination
    bool extractDirectory(const std::string &isoPath, const std::string &dirPathInISO, const std::string &destDir);

    // Get file size (bytes) for a specific entry inside the ISO
    bool getFileSize(const std::string &isoPath, const std::string &filePathInISO, unsigned long long &sizeOut);

private:
    // Helper to create directories
    void createDirectories(const std::string &path);
};

#endif // ISOREADER_H