#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <memory>
#include <array>
#include <windows.h>
#include "models/ISOReader.h"

// Function to execute a command and capture output using Windows API
std::string exec(const char* cmd) {
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        throw std::runtime_error("CreatePipe failed");
    }

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, const_cast<char*>(cmd), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        throw std::runtime_error("CreateProcess failed");
    }

    CloseHandle(hWritePipe);

    std::string result;
    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

// Parse 7z output to extract file list
std::vector<std::string> parse7zOutput(const std::string& output) {
    std::vector<std::string> files;
    std::istringstream iss(output);
    std::string line;
    bool inFileList = false;

    while (std::getline(iss, line)) {
        // Look for the start of file listing (header line)
        if (line.find("Date") != std::string::npos && line.find("Time") != std::string::npos && line.find("Attr") != std::string::npos) {
            inFileList = true;
            continue;
        }

        // Skip lines until we reach the file list
        if (!inFileList) continue;

        // Skip separator lines and summary lines
        if (line.empty() || line.find("----") != std::string::npos || line.find("files") != std::string::npos) continue;

        // Parse file path from 7z output
        // 7z output format: Date Time Attr Size Compressed Name
        // The Name is everything after the 5th space-separated field
        std::string trimmedLine = line;
        // Remove leading whitespace
        size_t start = trimmedLine.find_first_not_of(" \t");
        if (start != std::string::npos) {
            trimmedLine = trimmedLine.substr(start);
        } else {
            continue; // Empty line
        }

        // Find the 5th space to get to the filename
        int spaceCount = 0;
        size_t pos = 0;
        while ((pos = trimmedLine.find(' ', pos)) != std::string::npos && spaceCount < 4) {
            spaceCount++;
            pos++; // Move past the space
        }

        if (spaceCount >= 4 && pos < trimmedLine.length()) {
            std::string name = trimmedLine.substr(pos);
            // Remove leading/trailing whitespace from name
            start = name.find_first_not_of(" \t");
            if (start != std::string::npos) {
                name = name.substr(start);
                size_t end = name.find_last_not_of(" \t");
                if (end != std::string::npos) {
                    name = name.substr(0, end + 1);
                }
                if (!name.empty()) {
                    files.push_back(name);
                }
            }
        }
    }

    return files;
}

bool isWindowsISOHeuristic(const std::vector<std::string>& files) {
    bool isWindowsISO = false;
    bool hasSourcesDir = false;
    bool hasSetupExe = false;

    for (const auto &file : files) {
        std::string lower = file;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("boot.wim") != std::string::npos ||
            lower.find("install.wim") != std::string::npos ||
            lower.find("install.esd") != std::string::npos) {
            isWindowsISO = true;
            break;
        }
        if (lower.find("sources/") == 0 || lower == "sources") {
            hasSourcesDir = true;
        }
        if (lower.find("setup.exe") != std::string::npos) {
            hasSetupExe = true;
        }
    }

    // Heuristic: If we have sources directory and setup.exe, it's likely a Windows ISO
    // even if we can't read the UDF part with boot.wim/install.wim
    if (!isWindowsISO && hasSourcesDir && hasSetupExe) {
        isWindowsISO = true;
        std::cout << "Windows ISO detected via heuristic (sources + setup.exe)" << std::endl;
    }

    return isWindowsISO;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <iso_path>" << std::endl;
        return 1;
    }

    std::string isoPath = argv[1];
    std::cout << "Testing ISO detection for: " << isoPath << std::endl;

    try {
        // Get file list from 7z
        std::string command = "7z l \"" + isoPath + "\"";
        std::cout << "Executing: " << command << std::endl;
        std::string sevenZipOutput = exec(command.c_str());
        std::vector<std::string> sevenZipFiles = parse7zOutput(sevenZipOutput);

        std::cout << "7z found " << sevenZipFiles.size() << " files:" << std::endl;
        for (const auto& file : sevenZipFiles) {
            std::cout << "  " << file << std::endl;
        }

        // Create ISO reader
        ISOReader reader;

        // Get file list from our ISO reader
        std::vector<std::string> isoReaderFiles = reader.listFiles(isoPath);
        std::cout << "ISOReader found " << isoReaderFiles.size() << " files:" << std::endl;
        for (const auto& file : isoReaderFiles) {
            std::cout << "  " << file << std::endl;
        }

        // Compare the file lists
        bool listsMatch = (sevenZipFiles.size() == isoReaderFiles.size());
        if (listsMatch) {
            // Sort both lists for comparison
            std::vector<std::string> sorted7z = sevenZipFiles;
            std::vector<std::string> sortedReader = isoReaderFiles;
            std::sort(sorted7z.begin(), sorted7z.end());
            std::sort(sortedReader.begin(), sortedReader.end());

            for (size_t i = 0; i < sorted7z.size(); ++i) {
                if (sorted7z[i] != sortedReader[i]) {
                    listsMatch = false;
                    std::cout << "File mismatch at position " << i << ":" << std::endl;
                    std::cout << "  7z: " << sorted7z[i] << std::endl;
                    std::cout << "  ISOReader: " << sortedReader[i] << std::endl;
                    break;
                }
            }
        }

        if (!listsMatch) {
            std::cout << "FAILURE: File lists do not match!" << std::endl;
            std::cout << "7z files: " << sevenZipFiles.size() << std::endl;
            std::cout << "ISOReader files: " << isoReaderFiles.size() << std::endl;
            return 1;
        }

        std::cout << "SUCCESS: File lists match!" << std::endl;

        // Test Windows ISO detection using heuristic
        bool isWindows = isWindowsISOHeuristic(isoReaderFiles);
        std::cout << "Heuristic Windows ISO detection result: " << (isWindows ? "YES" : "NO") << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}