#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>
#include "models/ISOReader.h"

static std::string resolveIsoPath(int argc, char** argv) {
    // Priority 1: CLI argument
    if (argc >= 2) return std::string(argv[1]);

    // Priority 2: Environment variable
    if (const char* env = std::getenv("EASYISOBOOT_TEST_ISO")) {
        if (std::filesystem::exists(env)) return std::string(env);
    }

    // Priority 3: Common relative locations
    const char* candidates[] = {
        "isos/Win11_25H2_Spanish_x64.iso",
        "../isos/Win11_25H2_Spanish_x64.iso",
        "../../isos/Win11_25H2_Spanish_x64.iso"
    };
    for (const char* c : candidates) {
        if (std::filesystem::exists(c)) return std::string(c);
    }

    return std::string();
}

int main(int argc, char** argv) {
    ISOReader reader;
    std::string isoPath = resolveIsoPath(argc, argv);
    if (isoPath.empty()) {
        std::cout << "Usage: " << argv[0] << " <iso_path>\n"
                  << "Or set EASYISOBOOT_TEST_ISO to a valid ISO file path.\n"
                  << "Tried common relative paths under 'isos/'.\n";
        return 1;
    }

    std::cout << "Using ISO: " << isoPath << "\n";

    // List files
    std::vector<std::string> files = reader.listFiles(isoPath);
    std::cout << "Total files: " << files.size() << std::endl;
    for (const auto& file : files) {
        std::cout << file << std::endl;
    }

    // Discover all *.wim and *.esd inside the ISO (case-insensitive)
    auto ends_with_icase = [](const std::string &s, const std::string &suffix) {
        if (s.size() < suffix.size()) return false;
        for (size_t i = 0; i < suffix.size(); ++i) {
            char a = (char)std::tolower((unsigned char)s[s.size() - suffix.size() + i]);
            char b = (char)std::tolower((unsigned char)suffix[i]);
            if (a != b) return false;
        }
        return true;
    };

    std::vector<std::string> images;
    for (const auto &f : files) {
        if (ends_with_icase(f, ".wim") || ends_with_icase(f, ".esd")) {
            images.push_back(f);
        }
    }

    std::cout << "Found " << images.size() << " image file(s) (*.wim, *.esd) in ISO" << "\n";
    for (const auto &im : images) {
        std::cout << "  - " << im << "\n";
    }

    // If nothing found, still try common Windows paths for convenience
    if (images.empty()) {
        std::vector<std::string> fallbacks = { "sources/install.wim", "sources/install.esd", "sources/boot.wim" };
        for (const auto &t : fallbacks) {
            if (reader.fileExists(isoPath, t)) images.push_back(t);
        }
    }

    // Extract all discovered images to a temp dir (preserves internal paths)
    std::filesystem::path dest = std::filesystem::temp_directory_path() / "EasyISOBoot_iso_extract_test";
    std::error_code ec;
    std::filesystem::create_directories(dest, ec);

    if (!images.empty()) {
        bool extracted = reader.extractFiles(isoPath, images, dest.string());
        std::cout << "ExtractFiles result: " << (extracted ? "OK" : "FAIL") << " -> " << dest.string() << "\n";
        for (const auto& t : images) {
            std::filesystem::path out = dest / std::filesystem::path(t);
            std::cout << "Extracted [" << t << "] -> " << (std::filesystem::exists(out) ? "OK" : "MISSING") << "\n";
        }
    } else {
        std::cout << "No *.wim/*.esd files to extract" << "\n";
    }

    return 0;
}