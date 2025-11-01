#include <iostream>
#include <windows.h>
#include <comdef.h>
#include <wbemidl.h>
#include "services/PartitionDetector.h"

int main() {
    std::cout << "Testing PartitionDetector..." << std::endl;

    try {
        PartitionDetector detector;
        std::cout << "PartitionDetector created successfully" << std::endl;

        // Test finding partitions by labels
        std::vector<std::string> labels = {"BOOTTHATISO", "EFI"};
        std::cout << "Searching for partitions with labels..." << std::endl;

        auto partitions = detector.findPartitionsByLabels(labels);
        std::cout << "Found " << partitions.size() << " partitions by labels" << std::endl;

        for (const auto& p : partitions) {
            std::cout << "Partition " << p.partitionNumber << ": " << p.fileSystemLabel
                     << " (" << p.sizeBytes / (1024*1024) << "MB)"
                     << " Drive: " << p.driveLetter
                     << " GPT: " << p.gptType
                     << " Active: " << (p.isActive ? "Yes" : "No") << std::endl;
        }

        // Test finding all partitions
        std::cout << "Getting all partitions..." << std::endl;
        auto allPartitions = detector.getAllPartitions();
        std::cout << "Found " << allPartitions.size() << " total partitions" << std::endl;

        for (const auto& p : allPartitions) {
            std::cout << "Partition " << p.diskNumber << "/" << p.partitionNumber 
                     << ": Label='" << p.fileSystemLabel << "'"
                     << " Size=" << p.sizeBytes / (1024*1024) << "MB"
                     << " Offset=" << p.offsetBytes / (1024*1024) << "MB"
                     << " Drive='" << p.driveLetter << "'"
                     << " GPT='" << p.gptType << "'"
                     << " Active=" << (p.isActive ? "Yes" : "No") << std::endl;
        }

        // Test finding by size (EFI partition ~500MB)
        std::cout << "Testing size-based search (EFI partition ~500MB)..." << std::endl;
        auto efiPartitions = detector.findPartitionsBySize(400ULL * 1024 * 1024, 600ULL * 1024 * 1024);
        std::cout << "Found " << efiPartitions.size() << " EFI-sized partitions" << std::endl;

        for (const auto& p : efiPartitions) {
            std::cout << "EFI Partition " << p.partitionNumber << ": " << p.fileSystemLabel
                     << " (" << p.sizeBytes / (1024*1024) << "MB)" << std::endl;
        }

        // Test finding by size (BOOTTHATISO partition ~10000MB)
        std::cout << "Testing size-based search (BOOTTHATISO partition ~10000MB)..." << std::endl;
        auto bootPartitions = detector.findPartitionsBySize(9000ULL * 1024 * 1024, 11000ULL * 1024 * 1024);
        std::cout << "Found " << bootPartitions.size() << " BOOTTHATISO-sized partitions" << std::endl;

        for (const auto& p : bootPartitions) {
            std::cout << "BOOT Partition " << p.partitionNumber << ": " << p.fileSystemLabel
                     << " (" << p.sizeBytes / (1024*1024) << "MB)" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "Unknown exception" << std::endl;
        return 1;
    }

    std::cout << "Test completed successfully" << std::endl;
    return 0;
}