#pragma once

#include <string>
#include <fstream>
#include <ctime>

class BCDLogger {
public:
    BCDLogger(const std::string &logDir, const std::string &logFileName);
    ~BCDLogger();

    void log(const std::string &message);
    void logTimestamped(const std::string &message);

private:
    std::ofstream logFile_;
    std::string   getTimestamp() const;
};