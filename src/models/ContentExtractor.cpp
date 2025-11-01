#include "ContentExtractor.h"
#include "../utils/Utils.h"
#include "IniConfigurator.h"
#include "../services/ISOCopyManager.h"
#include <set>

ContentExtractor::ContentExtractor(EventManager& eventManager, FileCopyManager& fileCopyManager)
    : eventManager_(eventManager), fileCopyManager_(fileCopyManager) {
}

ContentExtractor::~ContentExtractor() {
}

bool ContentExtractor::extractContent(const std::string& sourcePath, const std::string& destPath, long long isoSize, long long& copiedSoFar,
                                      bool extractContent, bool isWindowsISO, const std::string& mode, std::ofstream& logFile) {
    if (!extractContent) {
        return true;
    }

    eventManager_.notifyLogUpdate("Copiando contenido del ISO hacia la particion ISOBOOT. Esto puede tardar varios minutos...\r\n");
    if (isoSize > 0) {
        eventManager_.notifyDetailedProgress(15, 100, "Copiando contenido del ISO");
    } else {
        eventManager_.notifyDetailedProgress(15, 100, "Copiando contenido del ISO");
    }
    std::set<std::string> excludeDirs = {"efi", "EFI"};
    if (!fileCopyManager_.copyDirectoryWithProgress(sourcePath, destPath, isoSize, copiedSoFar, excludeDirs, "Copiando contenido del ISO")) {
        logFile << ISOCopyManager::getTimestamp() << "Failed to copy content or cancelled" << std::endl;
        return false;
    }
    eventManager_.notifyLogUpdate("Contenido del ISO copiado correctamente.\r\n");

    // Reconfigure .ini files in the destination directory
    IniConfigurator iniConfigurator;
    iniConfigurator.configureIniFilesInDirectory(destPath, logFile, ISOCopyManager::getTimestamp);

    return true;
}