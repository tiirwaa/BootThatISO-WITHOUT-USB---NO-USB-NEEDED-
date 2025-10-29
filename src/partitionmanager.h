#ifndef PARTITIONMANAGER_H
#define PARTITIONMANAGER_H

#include <QString>

struct SpaceValidationResult {
    bool isValid;
    qint64 availableGB;
    QString errorMessage;
};

class PartitionManager
{
public:
    PartitionManager();
    ~PartitionManager();

    SpaceValidationResult validateAvailableSpace();
    qint64 getAvailableSpaceGB();
    bool createPartition();
};

#endif // PARTITIONMANAGER_H