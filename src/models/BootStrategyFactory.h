#ifndef BOOTSTRATEGYFACTORY_H
#define BOOTSTRATEGYFACTORY_H

#include "BootStrategy.h"
#include "RamdiskBootStrategy.h"
#include "ExtractedBootStrategy.h"
#include <memory>
#include <string>

class BootStrategyFactory {
public:
    static std::unique_ptr<BootStrategy> createStrategy(const std::string& mode) {
        if (mode == "Boot desde Memoria" || mode == "Instalación Completa") {
            return std::make_unique<RamdiskBootStrategy>();
        }
        return nullptr;
    }
};

#endif // BOOTSTRATEGYFACTORY_H