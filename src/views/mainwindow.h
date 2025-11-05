#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include <string>
#include <memory>
#include <commctrl.h>
#include <gdiplus.h>
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
#define IDC_INJECT_DRIVERS_CHECKBOX 1011

#define WM_UPDATE_PROGRESS (WM_USER + 1)
#define WM_UPDATE_LOG (WM_USER + 2)
#define WM_ENABLE_BUTTON (WM_USER + 3)
#define WM_ASK_RESTART (WM_USER + 4)
#define WM_UPDATE_DETAILED_PROGRESS (WM_USER + 5)
#define WM_UPDATE_ERROR (WM_USER + 6)
#define WM_RECOVER_COMPLETE (WM_USER + 7)
#define WM_UPDATE_DISK_SPACE (WM_USER + 8)

class MainWindow : public EventObserver {
public:
    MainWindow(HWND parent);
    ~MainWindow();

    void onProgressUpdate(int progress) override;
    void onLogUpdate(const std::string &message) override;
    void onButtonEnable() override;
    void onAskRestart() override;
    void onError(const std::string &message) override;
    void onDetailedProgress(long long copied, long long total, const std::string &operation) override;
    void onRecoverComplete(bool success) override;

    // Request cancellation of running process
    void requestCancel();

    bool IsProcessing() const {
        return isProcessing || isRecovering;
    }

    bool closePending;

    LRESULT HandleCommand(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void SetupUI(HWND parent);
    void LoadTexts();
    void CreateControls(HWND parent);
    void ApplyStyles();
    void UpdateDiskSpaceInfo();
    void LogMessage(const std::string &msg, bool persist = true);
    void UpdateDetailedProgressLabel(long long copied, long long total, const std::string &operation);
    void PromptRestart();

    void OnSelectISO();
    void OnCreatePartition();
    void OnOpenServicesPage();

    bool RestartSystem();
    void StartProcessingAnimation();
    void StopProcessingAnimation();

    void PerformHeavyInitialization();

    PartitionManager                  *partitionManager;
    ISOCopyManager                    *isoCopyManager;
    BCDManager                        *bcdManager;
    EventManager                       eventManager;
    std::unique_ptr<ProcessController> processController;

    // File system format selection
    std::string selectedFormat;
    // Boot mode selection stored as key (e.g., "ram", "extract")
    std::string selectedBootModeKey;
    // Skip integrity check
    bool skipIntegrityCheck;
    // Inject drivers into ISO (Windows Install ISOs only)
    bool injectDriversIntoISO;

    // Localized texts
    std::wstring logoText;
    std::wstring titleText;
    std::wstring subtitleText;
    std::wstring isoLabelText;
    std::wstring browseText;
    std::wstring formatText;
    std::wstring fat32Text;
    std::wstring exfatText;
    std::wstring ntfsText;
    std::wstring bootModeText;
    std::wstring bootRamText;
    std::wstring bootDiskText;
    std::wstring integrityText;
    std::wstring injectDriversText;
    std::wstring createButtonText;
    std::wstring versionText;
    std::wstring servicesText;
    std::wstring recoverText;

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
    HWND injectDriversCheckBox;
    HWND performHintLabel;
    HWND developedByLabel;

    HINSTANCE hInst;
    HWND      hWndParent;

    // Images
    Gdiplus::Bitmap *logoBitmap;
    HICON            logoHIcon;
    HICON            buttonHIcon;
    Gdiplus::Bitmap *buttonBitmap;
    double           buttonRotationAngle;
    UINT_PTR         buttonSpinTimerId;

    // Recovery dialog
    HWND         hRecoverDialog;
    std::wstring recoverMessageText;

    // Thread management
    bool isProcessing;
    bool isRecovering;

    void DrawCreateButton(LPDRAWITEMSTRUCT drawInfo);
    void ShowRecoverDialog();
    void HideRecoverDialog();
};

#endif // MAINWINDOW_H
