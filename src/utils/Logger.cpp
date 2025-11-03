#include "Logger.h"

#include "Utils.h"
#include "constants.h"

#include <filesystem>
#include <fstream>

namespace {
std::vector<std::string> defaultProcessLogs() {
    return {GENERAL_LOG_FILE,       BCD_CONFIG_LOG_FILE,    ISO_EXTRACT_LOG_FILE,      ISO_FILE_COPY_LOG_FILE,
            ISO_CONTENT_LOG_FILE,   COPY_ERROR_LOG_FILE,    DEBUG_DRIVES_EFI_LOG_FILE, DEBUG_DRIVES_LOG_FILE,
            DISKPART_LOG_FILE,      REFORMAT_LOG_FILE,      REFORMAT_EXIT_LOG_FILE,    CHKDSK_LOG_FILE,
            CHKDSK_F_LOG_FILE,      START_PROCESS_LOG_FILE, UNATTENDED_DEBUG_LOG_FILE, GENERAL_ALT_LOG_FILE,
            DISKPART_LIST_LOG_FILE, ISO_TYPE_DETECTION_LOG};
}
} // namespace

Logger &Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setBaseDirectory(const std::string &directory) {
    std::lock_guard<std::mutex> guard(directoryMutex);
    baseDirectory    = directory;
    directoryCreated = false;
}

void Logger::append(const std::string &fileName, const std::string &message) {
    ensureDirectory();
    std::lock_guard<std::mutex> guard(writeMutex);
    const std::filesystem::path path = std::filesystem::u8path(baseDirectory) / fileName;
    std::ofstream               logFile(path, std::ios::app | std::ios::binary);
    if (logFile) {
        logFile << message;
    }
}

void Logger::resetLogs(const std::vector<std::string> &fileNames) {
    ensureDirectory();
    std::lock_guard<std::mutex> guard(writeMutex);
    for (const auto &file : fileNames) {
        const std::filesystem::path path = std::filesystem::u8path(baseDirectory) / file;
        std::ofstream               ofs(path, std::ios::trunc | std::ios::binary);
    }
}

void Logger::resetProcessLogs() {
    resetLogs(defaultProcessLogs());
}

std::string Logger::logDirectory() const {
    std::lock_guard<std::mutex> guard(directoryMutex);
    if (baseDirectory.empty()) {
        return Utils::getExeDirectory() + "logs";
    }
    return baseDirectory;
}

void Logger::ensureDirectory() {
    std::lock_guard<std::mutex> guard(directoryMutex);
    if (baseDirectory.empty()) {
        baseDirectory = Utils::getExeDirectory() + "logs";
    }
    if (!directoryCreated) {
        std::filesystem::create_directories(std::filesystem::u8path(baseDirectory));
        directoryCreated = true;
    }
}
