#include "isocopymanager.h"
#include <windows.h>

ISOCopyManager::ISOCopyManager()
{
}

ISOCopyManager::~ISOCopyManager()
{
}

bool ISOCopyManager::copyISO(const std::string& isoPath, const std::string& destPath)
{
    return CopyFileA(isoPath.c_str(), destPath.c_str(), FALSE) != 0;
}