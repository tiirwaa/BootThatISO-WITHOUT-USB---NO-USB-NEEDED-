#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include <string>
#include <fstream>
#include <thread>
#include "../services/partitionmanager.h"
#include "../services/isocopymanager.h"
#include "../services/bcdmanager.h"
#include "../models/EventObserver.h"
#include "../models/EventManager.h"
#include "../controllers/ProcessController.h"

#define IDC_BROWSE_BUTTON 1001
#define IDC_CREATE_PARTITION_BUTTON 1002
#define IDC_SERVICES_BUTTON 1003
#define IDC_FAT32_RADIO 1004
#define IDC_EXFAT_RADIO 1005
#define IDC_NTFS_RADIO 1006
// Boot mode
#define IDC_BOOTMODE_RAMDISK 1007
#define IDC_BOOTMODE_EXTRACTED 1008
#define IDC_RECOVER_BUTTON 1009
#define IDC_INTEGRITY_CHECKBOX 1010

#define WM_UPDATE_PROGRESS (WM_USER + 1)
#define WM_UPDATE_LOG (WM_USER + 2)
#define WM_ENABLE_BUTTON (WM_USER + 3)
#define WM_ASK_RESTART (WM_USER + 4)
#define WM_UPDATE_DETAILED_PROGRESS (WM_USER + 5)
#define WM_UPDATE_ERROR (WM_USER + 6)
#define WM_RECOVER_COMPLETE (WM_USER + 7)

class MainWindow : public EventObserver
{
public:
    MainWindow(HWND parent);
    ~MainWindow();

    void onProgressUpdate(int progress) override;
    void onLogUpdate(const std::string& message) override;
    void onButtonEnable() override;
    void onAskRestart() override;
    void onError(const std::string& message) override;
    void onDetailedProgress(long long copied, long long total, const std::string& operation) override;
    void onRecoverComplete(bool success) override;

    // Request cancellation of running process
    void requestCancel();

    bool IsProcessing() const { return isProcessing || isRecovering; }

    void HandleCommand(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void SetupUI(HWND parent);
    void ApplyStyles();
    void UpdateDiskSpaceInfo();
    void LogMessage(const std::string& msg);
    void UpdateDetailedProgressLabel(long long copied, long long total, const std::string& operation);

    void OnSelectISO();
    void OnCreatePartition();
    void OnOpenServicesPage();

    bool RestartSystem();

    PartitionManager* partitionManager;
    ISOCopyManager* isoCopyManager;
    BCDManager* bcdManager;
    EventManager eventManager;
    ProcessController* processController;

    // File system format selection
    std::string selectedFormat;
    // Boot mode selection: "RAMDISK" or "EXTRACTED"
    std::string selectedBootMode;
    // Skip integrity check
    bool skipIntegrityCheck;

    // Controls
    HWND logoLabel;
    HWND titleLabel;
    HWND subtitleLabel;

    HWND isoPathLabel;
    HWND isoPathEdit;
    HWND browseButton;
    HWND formatLabel;
    HWND fat32Radio;
    HWND exfatRadio;
    HWND ntfsRadio;
    HWND bootModeLabel;
    HWND bootRamdiskRadio;
    HWND bootExtractedRadio;
    HWND diskSpaceLabel;
    HWND createPartitionButton;
    HWND progressBar;
    HWND logTextEdit;

    HWND footerLabel;
    HWND servicesButton;
    HWND detailedProgressLabel;
    HWND detailedProgressBar;
    HWND recoverButton;
    HWND integrityCheckBox;

    HINSTANCE hInst;
    HWND hWndParent;

    // Thread management
    std::thread* workerThread;
    bool isProcessing;
    bool isRecovering;

    // Log file
    std::ofstream generalLogFile;
};

#endif // MAINWINDOW_H
