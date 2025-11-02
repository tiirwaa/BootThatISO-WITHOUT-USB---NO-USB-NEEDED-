#include <iostream>
#include <vector>
#include <string>
#include "models/ISOReader.h"

int main() {
    ISOReader reader;
    std::string isoPath = "../../isos/Win11_25H2_Spanish_x64.iso";
    std::vector<std::string> files = reader.listFiles(isoPath);
    std::cout << "Total files: " << files.size() << std::endl;
    for (const auto& file : files) {
        std::cout << file << std::endl;
    }
    return 0;
}