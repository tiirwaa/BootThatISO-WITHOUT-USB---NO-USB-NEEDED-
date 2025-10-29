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
        if (mode == "RAMDISK") {
            return std::make_unique<RamdiskBootStrategy>();
        } else if (mode == "EXTRACTED") {
            return std::make_unique<ExtractedBootStrategy>();
        }
        return nullptr;
    }
};

#endif // BOOTSTRATEGYFACTORY_H