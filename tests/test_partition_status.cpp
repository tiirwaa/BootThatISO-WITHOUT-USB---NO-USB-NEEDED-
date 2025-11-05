#include <iostream>
#include <string>
#include <windows.h>
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

    std::cout << "Checking partition status..." << std::endl;

    EventManager eventManager;
    TestObserver observer;
    eventManager.addObserver(&observer);

    PartitionManager &pm = PartitionManager::getInstance();
    pm.setEventManager(&eventManager);

    std::cout << "=== PARTITION STATUS ===" << std::endl;

    bool hasIsoBoot = pm.partitionExists();
    std::cout << "ISOBOOT partition exists: " << (hasIsoBoot ? "YES" : "NO") << std::endl;

    bool hasEfi = pm.efiPartitionExists();
    std::cout << "ISOEFI partition exists: " << (hasEfi ? "YES" : "NO") << std::endl;

    if (hasIsoBoot) {
        std::string driveLetter = pm.getPartitionDriveLetter();
        std::cout << "ISOBOOT drive letter: " << driveLetter << std::endl;

        std::string fileSystem = pm.getPartitionFileSystem();
        std::cout << "ISOBOOT file system: " << fileSystem << std::endl;
    }

    if (hasEfi) {
        std::string efiDriveLetter = pm.getEfiPartitionDriveLetter();
        std::cout << "ISOEFI drive letter: " << efiDriveLetter << std::endl;

        int efiSize = pm.getEfiPartitionSizeMB();
        std::cout << "ISOEFI size: " << efiSize << " MB" << std::endl;
    }

    int efiCount = pm.countEfiPartitions();
    std::cout << "Total EFI partitions: " << efiCount << std::endl;

    std::cout << "=== END PARTITION STATUS ===" << std::endl;

    return 0;
}