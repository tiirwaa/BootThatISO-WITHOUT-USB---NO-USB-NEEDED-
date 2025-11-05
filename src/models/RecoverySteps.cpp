#include "RecoverySteps.h"
#include "VolumeDetector.h"
#include "../services/bcdmanager.h"
#include "../utils/LocalizationHelpers.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include <windows.h>
#include <vds.h>
#include <Objbase.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <algorithm>
#include <algorithm>

VolumeEnumerator::VolumeEnumerator(EventManager* eventManager, std::vector<std::pair<std::string, std::string>>& volumesToDelete)
    : eventManager_(eventManager), volumesToDelete_(volumesToDelete) {}

bool VolumeEnumerator::execute() {
    if (eventManager_)
        eventManager_->notifyLogUpdate("Starting volume enumeration using diskpart...\r\n");

    // Check if Windows is using the ISOEFI partition
    VolumeDetector volumeDetector(eventManager_);
    bool windowsUsingEfi = volumeDetector.isWindowsUsingEfiPartition();

    if (windowsUsingEfi) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.partition.windows_using_efi",
                                                           "WARNING: Windows is using the ISOEFI partition. "
                                                           "Only ISOBOOT will be deleted, ISOEFI will be preserved.\r\n"));
    }

    // Use diskpart to list volumes and parse the output
    std::string listCmd = "diskpart /s " + Utils::getExeDirectory() + "list_volumes.txt";
    std::ofstream listScript((Utils::getExeDirectory() + "list_volumes.txt").c_str());
    if (listScript) {
        listScript << "list volume\n";
        listScript.close();
    }

    // Execute diskpart to get volume list
    std::string outputPath = Utils::getExeDirectory() + "volume_list.txt";
    std::string cmd = listCmd + " > \"" + outputPath + "\" 2>&1";
    system(cmd.c_str());

    // Parse the output
    std::ifstream outputFile(outputPath.c_str());
    std::vector<std::tuple<int, std::string, std::string>> isoVolumes; // volume number, label, letter (if any)

    if (outputFile) {
        std::string line;
        bool inVolumeList = false;
        while (std::getline(outputFile, line)) {
            // Look for the volume list section
            if (line.find("Ltr  Etiqueta") != std::string::npos) {
                inVolumeList = true;
                continue;
            }
            if (inVolumeList && line.find("---") != std::string::npos) {
                continue; // Skip separator
            }
            if (inVolumeList && !line.empty() && line.find("Volumen") != std::string::npos) {
                // Parse volume line: "  Volumen 1         ISOBOOT     FAT32  Partición      9 GB  Correcto"
                std::istringstream iss(line);
                std::string token;
                int volNum = -1;
                std::string letter = " ";
                std::string label;

                // Skip "Volumen"
                iss >> token; // "Volumen"
                if (token == "Volumen") {
                    iss >> volNum;
                    
                    // Next token might be letter or spaces then label
                    std::getline(iss, token);
                    // token now contains "     c   label..." or "         label..."
                    
                    // Find the position after volume number
                    size_t pos = line.find(std::to_string(volNum)) + std::to_string(volNum).length();
                    
                    // Skip spaces to find letter or label
                    while (pos < line.length() && line[pos] == ' ') pos++;
                    
                    if (pos < line.length() && isalpha(line[pos]) && pos + 1 < line.length() && line[pos + 1] == ' ') {
                        // There's a drive letter (single letter followed by space)
                        letter = line[pos];
                        pos += 2; // Skip letter and space
                        while (pos < line.length() && line[pos] == ' ') pos++;
                        // Now get label
                        size_t endPos = line.find_first_of(" \t", pos);
                        if (endPos != std::string::npos) {
                            label = line.substr(pos, endPos - pos);
                        } else {
                            label = line.substr(pos);
                        }
                    } else {
                        // No drive letter, this is the label
                        letter = " ";
                        size_t endPos = line.find_first_of(" \t", pos);
                        if (endPos != std::string::npos) {
                            label = line.substr(pos, endPos - pos);
                        } else {
                            label = line.substr(pos);
                        }
                    }
                }

                if (volNum != -1 && (label == VOLUME_LABEL || label == EFI_VOLUME_LABEL)) {
                    isoVolumes.push_back(std::make_tuple(volNum, label, letter));
                    if (eventManager_)
                        eventManager_->notifyLogUpdate("Found ISO volume: " + label + " (Volume " + std::to_string(volNum) + ", Letter: " + letter + ")\r\n");
                }
            }
        }
        outputFile.close();
    }

    // For volumes without letters, assign temporary ones
    for (auto& vol : isoVolumes) {
        int volNum;
        std::string label, letter;
        std::tie(volNum, label, letter) = vol;

        if (letter.empty() || letter[0] == ' ') {
            // Assign a temporary drive letter
            char tempLetter = 0;
            for (char l = 'Z'; l >= 'D'; --l) {
                std::string driveCandidate = std::string(1, l) + ":\\";
                if (GetDriveTypeA(driveCandidate.c_str()) == DRIVE_NO_ROOT_DIR) {
                    // Use diskpart to assign letter
                    std::string assignCmd = "diskpart /s " + Utils::getExeDirectory() + "assign_letter.txt";
                    std::ofstream assignScript((Utils::getExeDirectory() + "assign_letter.txt").c_str());
                    if (assignScript) {
                        assignScript << "select volume " << volNum << "\n";
                        assignScript << "assign letter=" << l << "\n";
                        assignScript.close();

                        STARTUPINFOA si = {sizeof(si)};
                        PROCESS_INFORMATION pi;
                        si.dwFlags = STARTF_USESHOWWINDOW;
                        si.wShowWindow = SW_HIDE;
                        if (CreateProcessA(NULL, const_cast<char*>(assignCmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                            WaitForSingleObject(pi.hProcess, INFINITE);
                            DWORD exitCode;
                            GetExitCodeProcess(pi.hProcess, &exitCode);
                            CloseHandle(pi.hProcess);
                            CloseHandle(pi.hThread);

                            if (exitCode == 0) {
                                tempLetter = l;
                                if (eventManager_)
                                    eventManager_->notifyLogUpdate("Assigned drive letter " + std::string(1, l) + ": to volume " + std::to_string(volNum) + "\r\n");
                                break;
                            }
                        }
                    }
                }
            }

            if (tempLetter != 0) {
                std::get<2>(vol) = std::string(1, tempLetter);
            } else {
                if (eventManager_)
                    eventManager_->notifyLogUpdate("Could not assign drive letter to volume " + std::to_string(volNum) + "\r\n");
            }
        }
    }

    // Check for system EFI
    for (auto& vol : isoVolumes) {
        int volNum;
        std::string label, letter;
        std::tie(volNum, label, letter) = vol;

        if (label == EFI_VOLUME_LABEL && !letter.empty()) {
            std::string bootmgfwPath = letter + ":\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
            if (GetFileAttributesA(bootmgfwPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (eventManager_)
                    eventManager_->notifyLogUpdate("System EFI detected on " + letter + ":, skipping\r\n");
                continue; // Don't add to delete list
            }
        }

        if (!letter.empty()) {
            volumesToDelete_.push_back(std::make_pair(label, letter));
            if (eventManager_)
                eventManager_->notifyLogUpdate("Added to delete list: " + label + " at " + letter + ":\r\n");
        }
    }

    if (eventManager_)
        eventManager_->notifyLogUpdate("Volume enumeration complete. Found " + std::to_string(volumesToDelete_.size()) + " volumes to delete.\r\n");

    return !volumesToDelete_.empty();
}

VolumeDeleter::VolumeDeleter(EventManager* eventManager, const std::vector<std::pair<std::string, std::string>>& volumesToDelete)
    : eventManager_(eventManager), volumesToDelete_(volumesToDelete) {}

bool VolumeDeleter::execute() {
    for (auto it = volumesToDelete_.rbegin(); it != volumesToDelete_.rend(); ++it) {
        const std::string& label = it->first;
        const std::string& driveLetter = it->second;

        std::string deleteCmd = "diskpart /s " + Utils::getExeDirectory() + "delete_volume.txt";
        std::ofstream deleteScript((Utils::getExeDirectory() + "delete_volume.txt").c_str());
        if (deleteScript) {
            deleteScript << "select volume=" << driveLetter << "\n";
            deleteScript << "delete volume override\n";
            deleteScript.close();

            STARTUPINFOA si = {sizeof(si)};
            PROCESS_INFORMATION pi;
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            if (CreateProcessA(NULL, const_cast<char*>(deleteCmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD exitCode;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                if (exitCode == 0) {
                    if (eventManager_)
                        eventManager_->notifyLogUpdate(LocalizedFormatUtf8("log.space.volume_deleted", {Utils::utf8_to_wstring(label)}, "Volumen {0} eliminado.\r\n"));

                    char volumeGuid[MAX_PATH];
                    std::string mountPoint = driveLetter + ":\\";
                    if (!GetVolumeNameForVolumeMountPointA(mountPoint.c_str(), volumeGuid, sizeof(volumeGuid))) {
                        std::string mountPointToRemove = driveLetter + ":";
                        DeleteVolumeMountPointA(mountPointToRemove.c_str());
                    }
                } else {
                    if (eventManager_)
                        eventManager_->notifyLogUpdate(LocalizedFormatUtf8("log.space.volume_delete_failed", {Utils::utf8_to_wstring(label)}, "Error al eliminar volumen {0}.\r\n"));
                }
            }
        }
    }

    if (volumesToDelete_.empty()) {
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.space.no_volumes_deleted", "No se encontraron volúmenes ISOEFI o ISOBOOT para eliminar.\r\n"));
    }

    return true;
}

PartitionResizer::PartitionResizer(EventManager* eventManager) : eventManager_(eventManager) {}

bool PartitionResizer::execute() {
    if (eventManager_)
        eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.space.resizing_c", "Resizing C: partition...\r\n"));

    std::string resizeCmd = "diskpart /s " + Utils::getExeDirectory() + "resize_script.txt";
    std::ofstream resizeScript((Utils::getExeDirectory() + "resize_script.txt").c_str());
    if (resizeScript) {
        resizeScript << "select disk 0\n";
        resizeScript << "select partition 3\n"; // Assume C: is partition 3
        resizeScript << "extend\n";
        resizeScript.close();
    }

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessA(NULL, const_cast<char*>(resizeCmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.space.resize_completed", "Resize completed.\r\n"));
        return true;
    } else {
        if (eventManager_)
            eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.space.resize_error", "Error resizing.\r\n"));
        return false;
    }
}

BCDCleaner::BCDCleaner(EventManager* eventManager) : eventManager_(eventManager) {}

bool BCDCleaner::execute() {
    if (eventManager_)
        eventManager_->notifyLogUpdate(LocalizedOrUtf8("log.space.cleaning_bcd", "Cleaning BCD entries...\r\n"));

    BCDManager& bcdManager = BCDManager::getInstance();
    bcdManager.setEventManager(eventManager_);
    bcdManager.cleanBootThatISOEntries();
    return true;
}