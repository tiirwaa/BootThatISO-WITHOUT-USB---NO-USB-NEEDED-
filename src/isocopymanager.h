#ifndef ISOCOPYMANAGER_H
#define ISOCOPYMANAGER_H

#include <QString>

class ISOCopyManager
{
public:
    ISOCopyManager();
    ~ISOCopyManager();

    bool copyISO(const QString& isoPath);
};

#endif // ISOCOPYMANAGER_H