#pragma once
#include <string>

class ISOTypeDetector {
public:
    ISOTypeDetector();
    ~ISOTypeDetector();

    bool isWindowsISO(const std::string& mountedIsoPath);
};