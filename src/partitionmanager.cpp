#include "partitionmanager.h"
#include <QStorageInfo>

PartitionManager::PartitionManager()
{
}

PartitionManager::~PartitionManager()
{
}

SpaceValidationResult PartitionManager::validateAvailableSpace()
{
    qint64 availableGB = getAvailableSpaceGB();
    
    SpaceValidationResult result;
    result.availableGB = availableGB;
    result.isValid = availableGB >= 10;
    
    if (!result.isValid) {
        result.errorMessage = QString("No hay suficiente espacio disponible. Se requieren al menos 10 GB, pero solo hay %1 GB disponibles.").arg(availableGB);
    }
    
    return result;
}

qint64 PartitionManager::getAvailableSpaceGB()
{
    QStorageInfo storage("C:/");
    return storage.bytesAvailable() / (1024 * 1024 * 1024);
}

bool PartitionManager::createPartition()
{
    // TODO: Implement actual partition creation logic
    return true;
}