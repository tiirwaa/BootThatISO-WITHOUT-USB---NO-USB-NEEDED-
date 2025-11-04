#include "ContentExtractor.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationHelpers.h"
#include "IniConfigurator.h"
#include "../services/ISOCopyManager.h"
#include <set>

ContentExtractor::ContentExtractor(EventManager &eventManager, FileCopyManager &fileCopyManager)
    : eventManager_(eventManager), fileCopyManager_(fileCopyManager) {}

ContentExtractor::~ContentExtractor() {}

bool ContentExtractor::extractContent(const std::string &sourcePath, const std::string &destPath, long long isoSize,
                                      long long &copiedSoFar, bool extractContent, bool isWindowsISO,
                                      const std::string &mode, std::ofstream &logFile) {
    if (!extractContent) {
        return true;
    }

    std::string message = LocalizedOrUtf8("log.content.copying", "Copiando contenido del ISO a {0}...");
    size_t      pos     = message.find("{0}");
    if (pos != std::string::npos) {
        message.replace(pos, 3, "ISOBOOT");
    }
    eventManager_.notifyLogUpdate(message + " Esto puede tardar varios minutos...\r\n");

    std::string progressMsg = LocalizedOrUtf8("log.content.copying", "Copiando contenido del ISO a {0}...");
    pos                     = progressMsg.find("{0}");
    if (pos != std::string::npos) {
        progressMsg.replace(pos, 3, "");
    }
    if (isoSize > 0) {
        eventManager_.notifyDetailedProgress(15, 100, progressMsg);
    } else {
        eventManager_.notifyDetailedProgress(15, 100, progressMsg);
    }
    std::set<std::string> excludeDirs = {"efi", "EFI"};
    if (!fileCopyManager_.copyDirectoryWithProgress(sourcePath, destPath, isoSize, copiedSoFar, excludeDirs,
                                                    progressMsg)) {
        logFile << ISOCopyManager::getTimestamp() << "Failed to copy content or cancelled" << std::endl;
        return false;
    }
    eventManager_.notifyLogUpdate(LocalizedOrUtf8("log.content.copied", "Contenido del ISO copiado correctamente.") +
                                  "\r\n");

    // Reconfigure .ini files in the destination directory
    IniConfigurator iniConfigurator;
    std::string     driveLetter = destPath.substr(0, 2);
    iniConfigurator.configureIniFilesInDirectory(destPath, logFile, ISOCopyManager::getTimestamp, driveLetter);

    return true;
}