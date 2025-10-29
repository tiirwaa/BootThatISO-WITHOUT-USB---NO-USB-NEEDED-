#ifndef BOOTSTRATEGY_H
#define BOOTSTRATEGY_H

#include <string>

class BootStrategy {
public:
    virtual ~BootStrategy() = default;
    virtual std::string getBCDLabel() const = 0;
    virtual void configureBCD(const std::string& guid, const std::string& dataDevice, const std::string& espDevice, const std::string& efiPath) = 0;
};

#endif // BOOTSTRATEGY_H