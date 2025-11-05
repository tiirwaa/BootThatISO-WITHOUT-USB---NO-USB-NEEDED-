#include <iostream>
#include <string>
#include <windows.h>
#include <fstream>
#include <sstream>
#include "../src/services/partitionmanager.h"
#include "../src/utils/Utils.h"
#include "../src/utils/constants.h"
#include "../src/models/EventManager.h"

class TestObserver : public EventObserver {
public:
    void onProgressUpdate(int progress) override {}
    void onLogUpdate(const std::string &message) override {
        std::cout << message;
    }
    void onButtonEnable() override {}
    void onAskRestart() override {}
    void onError(const std::string &message) override {
        std::cout << "ERROR: " << message << std::endl;
    }
    void onDetailedProgress(long long copied, long long total, const std::string &operation) override {}
    void onRecoverComplete(bool success) override {}
};

int main() {
    // Check if running as admin
    BOOL   isAdmin = FALSE;
    HANDLE hToken  = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD           dwSize;
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            isAdmin = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }

    if (!isAdmin) {
        std::cout << "This test must be run as administrator!" << std::endl;
        return 1;
    }

    std::cout << "Testing PartitionManager recoverSpace functionality..." << std::endl;

    EventManager eventManager;
    TestObserver observer;
    eventManager.addObserver(&observer);

    PartitionManager &pm = PartitionManager::getInstance();
    pm.setEventManager(&eventManager);

    std::cout << "Calling recoverSpace()..." << std::endl;
    bool result = pm.recoverSpace();

    std::cout << "recoverSpace() returned: " << (result ? "SUCCESS" : "FAILED") << std::endl;

    // Verification: dump current partition layout
    std::cout << "\nVerifying partition layout after recovery..." << std::endl;
    
    std::string exeDir = Utils::getExeDirectory();
    std::string scriptPath = exeDir + "verify_partitions.txt";
    std::string outputPath = exeDir + "partition_layout_after_recovery.txt";
    
    // Create diskpart script
    std::ofstream scriptFile(scriptPath.c_str());
    if (scriptFile) {
        scriptFile << "select disk 0\n";
        scriptFile << "list partition\n";
        scriptFile << "list volume\n";
        scriptFile.close();
    }
    
    // Execute diskpart
    std::string cmd = "diskpart /s \"" + scriptPath + "\" > \"" + outputPath + "\" 2>&1";
    system(cmd.c_str());
    
    // Read and display the output
    std::ifstream outputFile(outputPath.c_str());
    if (outputFile) {
        std::cout << "Partition layout after recovery:\n";
        std::cout << "================================\n";
        std::string line;
        bool foundIsoVolumes = false;
        while (std::getline(outputFile, line)) {
            std::cout << line << std::endl;
            // Check for ISO volumes
            if (line.find(VOLUME_LABEL) != std::string::npos || line.find(EFI_VOLUME_LABEL) != std::string::npos) {
                foundIsoVolumes = true;
            }
        }
        outputFile.close();
        
        if (foundIsoVolumes) {
            std::cout << "\nWARNING: ISO volumes still present after recovery!\n";
            result = false; // Mark as failed if ISO volumes remain
        } else {
            std::cout << "\nVerification: No ISO volumes found. Recovery appears successful.\n";
        }
    } else {
        std::cout << "Failed to read partition layout output.\n";
    }

    return result ? 0 : 1;
}