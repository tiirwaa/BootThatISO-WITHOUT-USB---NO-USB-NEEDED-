#pragma once

#include <string>
#include <vector>
#include <optional>

class BCDEntryManager {
public:
    BCDEntryManager(const std::string &bcdCmdPath);

    // Create a new BCD entry
    std::optional<std::string> createEntry(const std::string &label, const std::string &type);

    // Delete entries by label
    void deleteEntriesByLabel(const std::string &labelToFind);

    // Set default entry
    bool setDefault(const std::string &guid);

    // Add to display order
    bool addToDisplayOrder(const std::string &guid, bool first = true);

    // Set timeout
    bool setTimeout(int seconds);

    // Delete value from entry
    bool deleteValue(const std::string &guid, const std::string &value);

    // Get all entries
    std::string enumAll();

private:
    std::string              bcdCmdPath_;
    std::vector<std::string> split(const std::string &s, char delim);
    std::vector<std::string> parseEntryBlocks(const std::string &enumOutput);
    bool                     icontains(const std::string &hay, const std::string &needle);
};