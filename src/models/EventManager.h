#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include "EventObserver.h"
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>
#include <windows.h>
#include <mutex>
#include "../utils/Logger.h"
#include "../utils/constants.h"

class EventManager {
private:
    std::vector<EventObserver*> observers;
    std::atomic<bool> cancelRequested{false};
    mutable std::mutex observersMutex;

public:
    void addObserver(EventObserver* observer) {
        std::lock_guard<std::mutex> lock(observersMutex);
        observers.push_back(observer);
    }

    void removeObserver(EventObserver* observer) {
        std::lock_guard<std::mutex> lock(observersMutex);
        observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
    }

    void notifyProgressUpdate(int progress) {
        auto snapshot = snapshotObservers();
        for (auto* observer : snapshot) {
            observer->onProgressUpdate(progress);
        }
    }

    void notifyLogUpdate(const std::string& message) {
        Logger::instance().append(GENERAL_LOG_FILE, message);

        auto snapshot = snapshotObservers();
        for (auto* observer : snapshot) {
            observer->onLogUpdate(message);
        }
    }

    void notifyButtonEnable() {
        auto snapshot = snapshotObservers();
        for (auto* observer : snapshot) {
            observer->onButtonEnable();
        }
    }

    void notifyAskRestart() {
        auto snapshot = snapshotObservers();
        for (auto* observer : snapshot) {
            observer->onAskRestart();
        }
    }

    void notifyError(const std::string& message) {
        auto snapshot = snapshotObservers();
        for (auto* observer : snapshot) {
            observer->onError(message);
        }
    }

    void notifyDetailedProgress(long long copied, long long total, const std::string& operation) {
        auto snapshot = snapshotObservers();
        for (auto* observer : snapshot) {
            observer->onDetailedProgress(copied, total, operation);
        }
    }

    void notifyRecoverComplete(bool success) {
        auto snapshot = snapshotObservers();
        for (auto* observer : snapshot) {
            observer->onRecoverComplete(success);
        }
    }

    // Cancellation control (thread-safe)
    void requestCancel() { cancelRequested.store(true); }
    void clearCancel() { cancelRequested.store(false); }
    bool isCancelRequested() const { return cancelRequested.load(); }

private:
    std::vector<EventObserver*> snapshotObservers() const {
        std::lock_guard<std::mutex> lock(observersMutex);
        return observers;
    }
};

#endif // EVENTMANAGER_H
