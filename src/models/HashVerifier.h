#pragma once
#include <string>
#include "../../include/models/HashInfo.h"

class HashVerifier {
public:
    HashVerifier();
    ~HashVerifier();

    bool shouldSkipCopy(const std::string &isoPath, const std::string &hashFilePath, const std::string &mode,
                        const std::string &format, bool driversInjected);
    void saveHashInfo(const std::string &hashFilePath, const std::string &md5, const std::string &mode,
                      const std::string &format, bool driversInjected);

private:
    HashInfo    readHashInfo(const std::string &path);
    std::string calculateMD5(const std::string &filePath);
};