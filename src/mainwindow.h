#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include <string>
#include <fstream>
#include <thread>
#include "partitionmanager.h"
#include "isocopymanager.h"
#include "bcdmanager.h"

#define IDC_BROWSE_BUTTON 1001
#define IDC_CREATE_PARTITION_BUTTON 1002
#define IDC_SERVICES_BUTTON 1003
#define IDC_FAT32_RADIO 1004
#define IDC_EXFAT_RADIO 1005
#define IDC_NTFS_RADIO 1006
// Boot mode
#define IDC_BOOTMODE_RAMDISK 1007
#define IDC_BOOTMODE_EXTRACTED 1008

#define WM_UPDATE_PROGRESS (WM_USER + 1)
#define WM_UPDATE_LOG (WM_USER + 2)
#define WM_ENABLE_BUTTON (WM_USER + 3)

class MainWindow
{
public:
    MainWindow(HWND parent);
    ~MainWindow();

    void HandleCommand(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void SetupUI(HWND parent);
    void ApplyStyles();
    void UpdateDiskSpaceInfo();
    void LogMessage(const std::string& msg);

    void OnSelectISO();
    void OnCreatePartition();
    void ProcessInThread();
    bool OnCopyISO();
    void OnConfigureBCD();
    void OnOpenServicesPage();

    PartitionManager *partitionManager;
    ISOCopyManager *isoCopyManager;
    BCDManager *bcdManager;

    // File system format selection
    std::string selectedFormat;
    // Boot mode selection: "RAMDISK" or "EXTRACTED"
    std::string selectedBootMode;

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

    HINSTANCE hInst;
    HWND hWndParent;

    // Thread management
    std::thread* workerThread;
    bool isProcessing;

    // Log file
    std::ofstream generalLogFile;
};

#endif // MAINWINDOW_H