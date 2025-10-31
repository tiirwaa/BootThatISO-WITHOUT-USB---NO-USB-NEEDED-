#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include "EventObserver.h"
#include <vector>
#include <memory>
#include <atomic>
#include <fstream>
#include <string>
#include <windows.h>
#include "../utils/Utils.h"
#include "../utils/constants.h"

class EventManager {
private:
    std::vector<EventObserver*> observers;
    std::atomic<bool> cancelRequested{false};

public:
    void addObserver(EventObserver* observer) {
        observers.push_back(observer);
    }

    void removeObserver(EventObserver* observer) {
        observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
    }

    void notifyProgressUpdate(int progress) {
        for (auto observer : observers) {
            observer->onProgressUpdate(progress);
        }
    }

    void notifyLogUpdate(const std::string& message) {
        // Write to log file
        std::string logDir = Utils::getExeDirectory() + "logs";
        CreateDirectoryA(logDir.c_str(), NULL);
        std::ofstream logFile((logDir + "\\" + GENERAL_LOG_FILE).c_str(), std::ios::app);
        if (logFile) {
            logFile << message;
            logFile.close();
        }

        for (auto observer : observers) {
            observer->onLogUpdate(message);
        }
    }

    void notifyButtonEnable() {
        for (auto observer : observers) {
            observer->onButtonEnable();
        }
    }

    void notifyAskRestart() {
        for (auto observer : observers) {
            observer->onAskRestart();
        }
    }

    void notifyError(const std::string& message) {
        for (auto observer : observers) {
            observer->onError(message);
        }
    }

    void notifyDetailedProgress(long long copied, long long total, const std::string& operation) {
        for (auto observer : observers) {
            observer->onDetailedProgress(copied, total, operation);
        }
    }

    void notifyRecoverComplete(bool success) {
        for (auto observer : observers) {
            observer->onRecoverComplete(success);
        }
    }

    // Cancellation control (thread-safe)
    void requestCancel() { cancelRequested.store(true); }
    void clearCancel() { cancelRequested.store(false); }
    bool isCancelRequested() const { return cancelRequested.load(); }
};

#endif // EVENTMANAGER_H
