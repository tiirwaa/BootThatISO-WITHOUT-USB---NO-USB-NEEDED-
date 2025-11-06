#include "BCDEntryManager.h"
#include "../utils/Utils.h"
#include <algorithm>
#include <sstream>

BCDEntryManager::BCDEntryManager(const std::string &bcdCmdPath) : bcdCmdPath_(bcdCmdPath) {}

std::optional<std::string> BCDEntryManager::createEntry(const std::string &label, const std::string &type) {
    std::string output;
    if (type == "ramdisk") {
        output = Utils::exec((bcdCmdPath_ + " /create /application OSLOADER /d \"" + label + "\"").c_str());
    } else {
        output = Utils::exec((bcdCmdPath_ + " /copy {default} /d \"" + label + "\"").c_str());
    }

    if (output.find("{") == std::string::npos || output.find("}") == std::string::npos)
        return std::nullopt;

    size_t pos = output.find("{");
    size_t end = output.find("}", pos);
    if (end == std::string::npos)
        return std::nullopt;

    return output.substr(pos, end - pos + 1);
}

void BCDEntryManager::deleteEntriesByLabel(const std::string &labelToFind) {
    std::string enumOutput  = enumAll();
    auto        entryBlocks = parseEntryBlocks(enumOutput);

    for (const auto &blk : entryBlocks) {
        if (icontains(blk, labelToFind)) {
            size_t pos = blk.find('{');
            if (pos != std::string::npos) {
                size_t end = blk.find('}', pos);
                if (end != std::string::npos) {
                    std::string guid = blk.substr(pos, end - pos + 1);
                    Utils::exec((bcdCmdPath_ + " /delete " + guid + " /f").c_str());
                    Utils::exec((bcdCmdPath_ + " /displayorder " + guid + " /remove").c_str());
                }
            }
        }
    }
}

bool BCDEntryManager::setDefault(const std::string &guid) {
    std::string result = Utils::exec((bcdCmdPath_ + " /default " + guid).c_str());
    // Check for errors
    return result.find("error") == std::string::npos && result.find("Error") == std::string::npos;
}

bool BCDEntryManager::addToDisplayOrder(const std::string &guid, bool first) {
    std::string cmd    = bcdCmdPath_ + " /displayorder " + guid + (first ? " /addfirst" : " /addlast");
    std::string result = Utils::exec(cmd.c_str());
    return result.find("error") == std::string::npos && result.find("Error") == std::string::npos;
}

bool BCDEntryManager::setTimeout(int seconds) {
    std::string result = Utils::exec((bcdCmdPath_ + " /set {bootmgr} timeout " + std::to_string(seconds)).c_str());
    return result.find("error") == std::string::npos && result.find("Error") == std::string::npos;
}

bool BCDEntryManager::deleteValue(const std::string &guid, const std::string &value) {
    std::string result = Utils::exec((bcdCmdPath_ + " /deletevalue " + guid + " " + value).c_str());
    return true; // Ignore errors as it may not exist
}

std::string BCDEntryManager::enumAll() {
    return Utils::exec((bcdCmdPath_ + " /enum all").c_str());
}

std::vector<std::string> BCDEntryManager::split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::stringstream        ss(s);
    std::string              item;
    while (getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

std::vector<std::string> BCDEntryManager::parseEntryBlocks(const std::string &enumOutput) {
    auto                     blocks = split(enumOutput, '\n');
    std::vector<std::string> entryBlocks;
    std::string              currentBlock;
    for (const auto &line : blocks) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            if (!currentBlock.empty()) {
                entryBlocks.push_back(currentBlock);
                currentBlock.clear();
            }
        } else {
            currentBlock += line + "\n";
        }
    }
    if (!currentBlock.empty())
        entryBlocks.push_back(currentBlock);
    return entryBlocks;
}

bool BCDEntryManager::icontains(const std::string &hay, const std::string &needle) {
    std::string h = hay;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}