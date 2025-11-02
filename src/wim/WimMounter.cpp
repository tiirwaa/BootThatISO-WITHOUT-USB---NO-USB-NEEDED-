#include "WimMounter.h"
#include "../utils/Utils.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <sstream>

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
    
    // Count indices
    size_t pos = 0;
    while ((pos = normalized.find("index :", pos)) != std::string::npos) {
        WimImageInfo info;
        info.index = (int)images.size() + 1;
        
        // Find the block for this index
        size_t      blockStart = pos;
        size_t      blockEnd   = normalized.find("index :", pos + 7);
        std::string block = (blockEnd == std::string::npos) ? normalized.substr(blockStart) 
                                                             : normalized.substr(blockStart, blockEnd - blockStart);
        
        // Extract name
        size_t namePos = block.find("name :");
        if (namePos == std::string::npos)
            namePos = block.find("nombre :");
        if (namePos != std::string::npos) {
            size_t nameEnd = block.find_first_of("\r\n", namePos);
            info.name = dismOutput.substr(blockStart + namePos, 
                                         (nameEnd == std::string::npos) ? std::string::npos : nameEnd - namePos);
        }
        
        // Extract description
        size_t descPos = block.find("description :");
        if (descPos == std::string::npos)
            descPos = block.find("descripcion :");
        if (descPos != std::string::npos) {
            size_t descEnd = block.find_first_of("\r\n", descPos);
            info.description = dismOutput.substr(blockStart + descPos,
                                                (descEnd == std::string::npos) ? std::string::npos : descEnd - descPos);
        }
        
        // Detect if this is a Windows Setup image
        info.isSetupImage = (block.find("setup") != std::string::npos) || 
                           (block.find("instalacion") != std::string::npos);
        
        images.push_back(info);
        pos += 7;
    }
    
    return images;
}

std::vector<WimMounter::WimImageInfo> WimMounter::getWimImageInfo(const std::string &wimPath) {
    std::string dism    = Utils::getDismPath();
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
    std::string command = "\"" + dism + "\" /Mount-Wim /WimFile:\"" + wimPath + "\" /index:" +
                          std::to_string(imageIndex) + " /MountDir:\"" + mountDir + "\"";
    
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
