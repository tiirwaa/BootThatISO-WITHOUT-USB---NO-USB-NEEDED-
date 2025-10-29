#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include <string>
#include "partitionmanager.h"
#include "isocopymanager.h"
#include "bcdmanager.h"

#define IDC_BROWSE_BUTTON 1001
#define IDC_CREATE_PARTITION_BUTTON 1002
#define IDC_SERVICES_BUTTON 1003

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
    void OnCopyISO();
    void OnConfigureBCD();
    void OnOpenServicesPage();

    PartitionManager *partitionManager;
    ISOCopyManager *isoCopyManager;
    BCDManager *bcdManager;

    // Controls
    HWND logoLabel;
    HWND titleLabel;
    HWND subtitleLabel;

    HWND isoPathLabel;
    HWND isoPathEdit;
    HWND browseButton;
    HWND diskSpaceLabel;
    HWND createPartitionButton;
    HWND progressBar;
    HWND logTextEdit;

    HWND footerLabel;
    HWND servicesButton;

    HINSTANCE hInst;
};

#endif // MAINWINDOW_H