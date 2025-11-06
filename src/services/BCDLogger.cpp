#include "BCDLogger.h"
#include <windows.h>

BCDLogger::BCDLogger(const std::string &logDir, const std::string &logFileName) {
    CreateDirectoryA(logDir.c_str(), NULL);
    std::string fullPath = logDir + "\\" + logFileName;
    logFile_.open(fullPath, std::ios::app);
}

BCDLogger::~BCDLogger() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void BCDLogger::log(const std::string &message) {
    if (logFile_.is_open()) {
        logFile_ << message << std::endl;
    }
}

void BCDLogger::logTimestamped(const std::string &message) {
    if (logFile_.is_open()) {
        logFile_ << getTimestamp() << " " << message << std::endl;
    }
}

std::string BCDLogger::getTimestamp() const {
    char        buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm     localTime;
    localtime_s(&localTime, &now);
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", &localTime);
    return std::string(buffer);
}