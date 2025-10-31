#ifndef BOOTSTRATEGYFACTORY_H
#define BOOTSTRATEGYFACTORY_H

#include "BootStrategy.h"
#include "RamdiskBootStrategy.h"
#include "ExtractedBootStrategy.h"
#include <memory>
#include <string>
#include "../utils/AppKeys.h"

class BootStrategyFactory {
public:
    static std::unique_ptr<BootStrategy> createStrategy(const std::string& modeKey) {
        if (modeKey == AppKeys::BootModeRam) {
            return std::make_unique<RamdiskBootStrategy>();
        }
        if (modeKey == AppKeys::BootModeExtract) {
            return std::make_unique<ExtractedBootStrategy>();
        }
        return nullptr;
    }
};

#endif // BOOTSTRATEGYFACTORY_H
