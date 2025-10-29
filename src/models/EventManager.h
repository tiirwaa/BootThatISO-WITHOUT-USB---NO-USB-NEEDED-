#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include "EventObserver.h"
#include <vector>
#include <memory>

class EventManager {
private:
    std::vector<EventObserver*> observers;

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

    void notifyDetailedProgress(long long copied, long long total, const std::string& operation) {
        for (auto observer : observers) {
            observer->onDetailedProgress(copied, total, operation);
        }
    }
};

#endif // EVENTMANAGER_H