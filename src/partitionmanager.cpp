#include "partitionmanager.h"
#include <QStorageInfo>
#include <QProcess>
#include <QTemporaryFile>
#include <QTextStream>

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
    // Create a temporary script file for diskpart
    QTemporaryFile scriptFile;
    if (!scriptFile.open()) {
        return false;
    }

    QTextStream out(&scriptFile);
    out << "select disk 0\n";
    out << "shrink desired=10240 minimum=10240\n";
    out << "create partition primary size=10240\n";
    out << "assign letter=Z\n";
    out << "format fs=ntfs quick label=\"EasyISOBoot\"\n";
    out << "exit\n";
    scriptFile.close();

    // Execute diskpart with the script
    QProcess process;
    process.start("diskpart", QStringList() << "/s" << scriptFile.fileName());
    if (!process.waitForFinished(300000)) { // 5 minutes timeout
        return false;
    }

    if (process.exitCode() != 0) {
        return false;
    }

    return true;
}