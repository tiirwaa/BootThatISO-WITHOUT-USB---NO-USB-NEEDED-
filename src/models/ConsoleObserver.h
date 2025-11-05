#ifndef CONSOLEOBSERVER_H
#define CONSOLEOBSERVER_H

#include "EventObserver.h"
#include <iostream>
#include <string>

class ConsoleObserver : public EventObserver {
public:
    void onProgressUpdate(int progress) override {
        std::cout << "Progreso: " << progress << "%" << std::endl;
    }

    void onLogUpdate(const std::string &message) override {
        std::cout << message << std::endl;
    }

    void onButtonEnable() override {
        // No aplicable en modo consola
    }

    void onAskRestart() override {
        std::cout << "Reinicio solicitado." << std::endl;
    }

    void onError(const std::string &message) override {
        std::cerr << "ERROR: " << message << std::endl;
    }

    void onDetailedProgress(long long copied, long long total, const std::string &operation) override {
        if (total > 0) {
            int percentage = static_cast<int>((copied * 100) / total);
            std::cout << operation << ": " << copied << "/" << total << " (" << percentage << "%)" << std::endl;
        } else {
            std::cout << operation << ": " << copied << " bytes" << std::endl;
        }
    }

    void onRecoverComplete(bool success) override {
        if (success) {
            std::cout << "Recuperacion de espacio completada exitosamente." << std::endl;
        } else {
            std::cout << "Error durante la recuperacion de espacio." << std::endl;
        }
    }
};

#endif // CONSOLEOBSERVER_H