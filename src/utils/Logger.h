#ifndef LOGGER_H
#define LOGGER_H

#include <mutex>
#include <string>
#include <vector>

class Logger {
public:
    static Logger& instance();

    void setBaseDirectory(const std::string& directory);
    void append(const std::string& fileName, const std::string& message);
    void resetLogs(const std::vector<std::string>& fileNames);
    void resetProcessLogs();
    std::string logDirectory() const;

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void ensureDirectory();

    mutable std::mutex directoryMutex;
    std::mutex writeMutex;
    std::string baseDirectory;
    bool directoryCreated{false};
};

#endif // LOGGER_H
