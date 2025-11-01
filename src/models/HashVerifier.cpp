#include "HashVerifier.h"
#include "../utils/Utils.h"
#include "../../build/version.h"
#include <fstream>

HashVerifier::HashVerifier() {
}

HashVerifier::~HashVerifier() {
}

bool HashVerifier::shouldSkipCopy(const std::string& isoPath, const std::string& hashFilePath, const std::string& mode, const std::string& format) {
    std::string md5 = calculateMD5(isoPath);
    HashInfo existing = readHashInfo(hashFilePath);
    return (existing.hash == md5 && existing.version == APP_VERSION && existing.mode == mode && existing.format == format && !existing.hash.empty());
}

void HashVerifier::saveHashInfo(const std::string& hashFilePath, const std::string& md5, const std::string& mode, const std::string& format) {
    std::ofstream hashFile(hashFilePath);
    if (hashFile.is_open()) {
        hashFile << md5 << std::endl;
        hashFile << APP_VERSION << std::endl;
        hashFile << mode << std::endl;
        hashFile << format << std::endl;
        hashFile.close();
    }
}

HashInfo HashVerifier::readHashInfo(const std::string& path) {
    HashInfo info = {"", "", "", ""};
    std::ifstream file(path);
    if (file.is_open()) {
        std::getline(file, info.hash);
        std::getline(file, info.version);
        std::getline(file, info.mode);
        std::getline(file, info.format);
    }
    return info;
}

std::string HashVerifier::calculateMD5(const std::string& filePath) {
    return Utils::calculateMD5(filePath);
}