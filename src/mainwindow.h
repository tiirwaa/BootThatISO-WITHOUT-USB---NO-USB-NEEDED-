#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include <string>
#include <fstream>
#include "partitionmanager.h"
#include "isocopymanager.h"
#include "bcdmanager.h"

#define IDC_BROWSE_BUTTON 1001
#define IDC_CREATE_PARTITION_BUTTON 1002
#define IDC_SERVICES_BUTTON 1003
#define IDC_FAT32_RADIO 1004
#define IDC_EXFAT_RADIO 1005
#define IDC_NTFS_RADIO 1006

class MainWindow
{
public:
    MainWindow(HWND parent);
    ~MainWindow();

    void HandleCommand(WPARAM wParam, LPARAM lParam);

private:
    void SetupUI(HWND parent);
    void ApplyStyles();
    void UpdateDiskSpaceInfo();
    void LogMessage(const std::string& msg);

    void OnSelectISO();
    void OnCreatePartition();
    bool OnCopyISO();
    void OnConfigureBCD();
    void OnOpenServicesPage();

    PartitionManager *partitionManager;
    ISOCopyManager *isoCopyManager;
    BCDManager *bcdManager;

    // File system format selection
    std::string selectedFormat;

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
    HWND diskSpaceLabel;
    HWND createPartitionButton;
    HWND progressBar;
    HWND logTextEdit;

    HWND footerLabel;
    HWND servicesButton;

    HINSTANCE hInst;

    // Log file
    std::ofstream generalLogFile;
};

#endif // MAINWINDOW_H