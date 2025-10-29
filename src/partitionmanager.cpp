#include "partitionmanager.h"
#include <QStorageInfo>
#include <QProcess>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>

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
    out << "select volume C\n";
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
        // Log timeout
        QFile logFile("log.txt");
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream logOut(&logFile);
            logOut << "Diskpart command timed out.\n";
            logOut << "Script content:\n";
            logOut << "select disk 0\n";
            logOut << "select volume C\n";
            logOut << "shrink desired=10240 minimum=10240\n";
            logOut << "create partition primary size=10240\n";
            logOut << "assign letter=Z\n";
            logOut << "format fs=ntfs quick label=\"EasyISOBoot\"\n";
            logOut << "exit\n";
            logFile.close();
        }
        return false;
    }

    // Read output and error
    QByteArray outputStdout = process.readAllStandardOutput();
    QByteArray outputStderr = process.readAllStandardError();
    int exitCode = process.exitCode();

    // Write to log.txt
    QFile logFile("log.txt");
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream logOut(&logFile);
        logOut << "Diskpart script executed.\n";
        logOut << "Script content:\n";
        logOut << "select disk 0\n";
        logOut << "select volume C\n";
        logOut << "shrink desired=10240 minimum=10240\n";
        logOut << "create partition primary size=10240\n";
        logOut << "assign letter=Z\n";
        logOut << "format fs=ntfs quick label=\"EasyISOBoot\"\n";
        logOut << "exit\n";
        logOut << "\nExit code: " << exitCode << "\n";
        logOut << "Standard output:\n" << QString(outputStdout) << "\n";
        logOut << "Standard error:\n" << QString(outputStderr) << "\n";
        logFile.close();
    }

    if (exitCode != 0) {
        return false;
    }

    return true;
}

bool PartitionManager::partitionExists()
{
    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &storage : volumes) {
        if (storage.displayName() == "EasyISOBoot") {
            return true;
        }
    }
    return false;
}