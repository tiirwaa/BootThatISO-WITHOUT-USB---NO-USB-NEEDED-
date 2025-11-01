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
    void onLogUpdate(const std::string& message) override {
        std::cout << message;
    }
    void onButtonEnable() override {}
    void onAskRestart() override {}
    void onError(const std::string& message) override {
        std::cout << "ERROR: " << message << std::endl;
    }
    void onDetailedProgress(long long copied, long long total, const std::string& operation) override {}
    void onRecoverComplete(bool success) override {}
};

int main() {
    // Check if running as admin
    BOOL isAdmin = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD dwSize;
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

    PartitionManager& pm = PartitionManager::getInstance();
    pm.setEventManager(&eventManager);

    std::cout << "Calling recoverSpace()..." << std::endl;
    bool result = pm.recoverSpace();

    std::cout << "recoverSpace() returned: " << (result ? "SUCCESS" : "FAILED") << std::endl;

    return result ? 0 : 1;
}