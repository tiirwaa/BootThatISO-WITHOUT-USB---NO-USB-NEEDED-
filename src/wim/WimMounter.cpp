#include "WimMounter.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

WimMounter::WimMounter() {}

WimMounter::~WimMounter() {}

int WimMounter::executeDism(const std::string &command, std::string &output) {
    return Utils::execWithExitCode(command.c_str(), output);
}

std::string WimMounter::normalizeString(const std::string &s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    // Light accent normalization for Spanish characters
    for (char &c : result) {
        unsigned char uc = (unsigned char)c;
        if (uc == 0xED || uc == 0xCD)
            c = 'i'; // í / Í
        if (uc == 0xF3 || uc == 0xD3)
            c = 'o'; // ó / Ó
    }
    return result;
}

std::vector<WimMounter::WimImageInfo> WimMounter::parseWimInfo(const std::string &dismOutput) {
    std::vector<WimImageInfo> images;
    std::string               normalized = normalizeString(dismOutput);

    // Parse DISM output using pattern matching that works in any language
    // DISM format is consistent: field : value, with "Index :" always being first
    size_t pos = 0;
    while ((pos = normalized.find("index :", pos)) != std::string::npos) {
        WimImageInfo info;
        info.index = (int)images.size() + 1;
        info.size  = 0;

        // Find the block for this index
        size_t      blockStart = pos;
        size_t      blockEnd   = normalized.find("index :", pos + 7);
        std::string block      = (blockEnd == std::string::npos) ? normalized.substr(blockStart)
                                                                 : normalized.substr(blockStart, blockEnd - blockStart);

        // Strategy: Look for pattern ": <value>" after index line
        // The second line after "Index :" is typically "Name :" (or localized equivalent)
        // We look for lines with ": " pattern to extract values
        std::istringstream blockStream(
            dismOutput.substr(blockStart, (blockEnd == std::string::npos) ? std::string::npos : blockEnd - blockStart));
        std::string line;
        int         lineNum = 0;

        while (std::getline(blockStream, line)) {
            lineNum++;
            size_t colonPos = line.find(" : ");
            if (colonPos == std::string::npos)
                continue;

            std::string value = line.substr(colonPos + 3);
            // Trim value
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            // Line 2 is typically Name (in any language)
            if (lineNum == 2 && info.name.empty()) {
                info.name = value;
            }
            // Line 3 is typically Description
            else if (lineNum == 3 && info.description.empty()) {
                info.description = value;
            }
            // Look for lines with large numbers (size in bytes)
            else if (value.length() > 5) {
                std::string numStr;
                for (char c : value) {
                    if (isdigit(c))
                        numStr += c;
                }
                if (numStr.length() >= 8) { // Size is typically 8+ digits
                    try {
                        long long size = std::stoll(numStr);
                        if (size > 100000000) { // > 100MB, likely the size field
                            info.size = size;
                        }
                    } catch (...) {
                    }
                }
            }
        }

        // Detect Windows Setup image by checking if name/description contains setup-related keywords
        // Use normalized (lowercase) version for case-insensitive matching
        std::string normalizedName = normalizeString(info.name);
        std::string normalizedDesc = normalizeString(info.description);
        info.isSetupImage          = (normalizedName.find("setup") != std::string::npos) ||
                            (normalizedName.find("instalacion") != std::string::npos) ||
                            (normalizedDesc.find("setup") != std::string::npos) ||
                            (normalizedDesc.find("instalacion") != std::string::npos);

        images.push_back(info);
        pos += 7;
    }

    return images;
}

std::vector<WimMounter::WimImageInfo> WimMounter::getWimImageInfo(const std::string &wimPath) {
    std::string dism = Utils::getDismPath();
    // Use standard DISM command - we parse by structure, not by language-specific keywords
    std::string command = "\"" + dism + "\" /Get-WimInfo /WimFile:\"" + wimPath + "\"";
    std::string output;

    int exitCode = executeDism(command, output);
    if (exitCode != 0) {
        lastError_      = "Failed to get WIM info";
        lastDismOutput_ = output;
        return {};
    }

    lastDismOutput_ = output;
    return parseWimInfo(output);
}

int WimMounter::selectBestImageIndex(const std::string &wimPath) {
    auto images = getWimImageInfo(wimPath);

    if (images.empty())
        return 1;

    // Prefer Windows Setup image
    for (const auto &img : images) {
        if (img.isSetupImage)
            return img.index;
    }

    // Fallback: if there are at least 2 images, choose index 2 (commonly Windows Setup)
    if (images.size() >= 2)
        return 2;

    return 1;
}

bool WimMounter::mountWim(const std::string &wimPath, const std::string &mountDir, int imageIndex,
                          ProgressCallback progressCallback) {
    // Clean and prepare mount directory
    cleanupMountDirectory(mountDir);
    CreateDirectoryA(mountDir.c_str(), NULL);

    // Remove read-only attribute from WIM file
    SetFileAttributesA(wimPath.c_str(), FILE_ATTRIBUTE_NORMAL);

    if (progressCallback)
        progressCallback(10, "Montando imagen WIM index " + std::to_string(imageIndex));

    std::string dism    = Utils::getDismPath();
    std::string command = "\"" + dism + "\" /Mount-Wim /WimFile:\"" + wimPath +
                          "\" /index:" + std::to_string(imageIndex) + " /MountDir:\"" + mountDir + "\"";

    std::string output;
    int         exitCode = executeDism(command, output);

    lastDismOutput_ = output;

    if (exitCode != 0) {
        lastError_ = "DISM mount failed with code " + std::to_string(exitCode);
        if (progressCallback)
            progressCallback(0, "Error al montar WIM");
        return false;
    }

    if (progressCallback)
        progressCallback(100, "WIM montado exitosamente");

    return true;
}

bool WimMounter::unmountWim(const std::string &mountDir, bool commit, ProgressCallback progressCallback) {
    if (progressCallback)
        progressCallback(10, commit ? "Guardando cambios en WIM" : "Desmontando WIM sin guardar");

    std::string dism    = Utils::getDismPath();
    std::string command = "\"" + dism + "\" /Unmount-Wim /MountDir:\"" + mountDir + "\"";

    if (commit)
        command += " /Commit";
    else
        command += " /Discard";

    std::string output;
    int         exitCode = executeDism(command, output);

    lastDismOutput_ = output;

    if (exitCode != 0) {
        lastError_ = "DISM unmount failed with code " + std::to_string(exitCode);
        if (progressCallback)
            progressCallback(0, "Error al desmontar WIM");
        return false;
    }

    if (progressCallback)
        progressCallback(100, commit ? "Cambios guardados exitosamente" : "WIM desmontado");

    // Cleanup mount directory
    cleanupMountDirectory(mountDir);

    return true;
}

void WimMounter::cleanupMountDirectory(const std::string &mountDir) {
    if (GetFileAttributesA(mountDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string rdCmd = "cmd /c rd /s /q \"" + mountDir + "\" 2>nul";
        Utils::exec(rdCmd.c_str());
    }
}

bool WimMounter::exportWimIndex(const std::string &sourceWim, int sourceIndex, const std::string &destWim,
                                int destIndex, ProgressCallback progressCallback) {
    // Remove read-only attribute from both files
    SetFileAttributesA(sourceWim.c_str(), FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesA(destWim.c_str(), FILE_ATTRIBUTE_NORMAL);

    if (progressCallback)
        progressCallback(5, "Exportando índice " + std::to_string(sourceIndex) + " de install.wim a boot.wim");

    std::string dism = Utils::getDismPath();

    // Use /Export-Image to add the install index to boot.wim
    // Note: For .esd files, DISM can read them but exports to .wim format
    std::string command = "\"" + dism + "\" /Export-Image /SourceImageFile:\"" + sourceWim +
                          "\" /SourceIndex:" + std::to_string(sourceIndex) + " /DestinationImageFile:\"" + destWim +
                          "\" /Compress:maximum /CheckIntegrity";

    std::string output;
    int         lastReportedPercent = 5;

    // Callback to monitor DISM output and extract progress
    auto dismCallback = [&](const std::string &line) {
        // DISM reports progress like: [==25.0%==] or [===50.0%===]
        size_t percentPos = line.find('%');
        if (percentPos != std::string::npos) {
            // Search backwards for a number
            size_t startPos = percentPos;
            while (startPos > 0 && (isdigit(line[startPos - 1]) || line[startPos - 1] == '.')) {
                startPos--;
            }

            if (startPos < percentPos) {
                try {
                    std::string percentStr = line.substr(startPos, percentPos - startPos);
                    double      percent    = std::stod(percentStr);
                    int         intPercent = static_cast<int>(percent);

                    // Only update if progress has increased by at least 5%
                    if (intPercent > lastReportedPercent && intPercent <= 100) {
                        lastReportedPercent = intPercent;
                        if (progressCallback) {
                            progressCallback(intPercent, "Exportando índice " + std::to_string(sourceIndex) + " (" +
                                                             std::to_string(intPercent) + "%)");
                        }
                    }
                } catch (...) {
                    // Ignore parsing errors
                }
            }
        }
    };

    int exitCode = Utils::execWithCallback(command.c_str(), output, dismCallback);

    lastDismOutput_ = output;

    if (exitCode != 0) {
        lastError_ = "DISM export failed with code " + std::to_string(exitCode);
        if (progressCallback)
            progressCallback(0, "Error al exportar índice WIM");
        return false;
    }

    if (progressCallback)
        progressCallback(100, "Índice exportado exitosamente a boot.wim");

    return true;
}
