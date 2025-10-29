#include "mainwindow.h"
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include "../models/BootStrategyFactory.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <ctime>

#define WM_UPDATE_DETAILED_PROGRESS (WM_USER + 5)
#define WM_UPDATE_ERROR (WM_USER + 6)

struct DetailedProgressData {
    long long copied;
    long long total;
    std::string operation;
};

MainWindow::MainWindow(HWND parent)
    : hInst(GetModuleHandle(NULL)), hWndParent(parent), selectedFormat("NTFS"), selectedBootMode("EXTRACTED"), workerThread(nullptr), isProcessing(false)
{
    partitionManager = &PartitionManager::getInstance();
    isoCopyManager = &ISOCopyManager::getInstance();
    bcdManager = &BCDManager::getInstance();
    eventManager.addObserver(this);
    processController = new ProcessController(eventManager);
    generalLogFile.open(Utils::getExeDirectory() + "general_log.log", std::ios::app);
    if (partitionManager->partitionExists()) {
        bcdManager->restoreBCD();
    }
    SetupUI(parent);
    UpdateDiskSpaceInfo();
}

MainWindow::~MainWindow()
{
    generalLogFile.close();
    if (workerThread && workerThread->joinable()) {
        workerThread->join();
        delete workerThread;
    }
    delete processController;
}

void MainWindow::SetupUI(HWND parent)
{
    // Create controls
    logoLabel = CreateWindowW(L"STATIC", L"LOGO", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 65, 65, parent, NULL, hInst, NULL);
    SendMessage(logoLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    titleLabel = CreateWindowW(L"STATIC", L"EASY ISOBOOT", WS_CHILD | WS_VISIBLE, 75, 10, 300, 30, parent, NULL, hInst, NULL);
    SendMessage(titleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    subtitleLabel = CreateWindowW(L"STATIC", L"Configuración de Particiones Bootables EFI", WS_CHILD | WS_VISIBLE, 75, 40, 300, 20, parent, NULL, hInst, NULL);
    SendMessage(subtitleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    isoPathLabel = CreateWindowW(L"STATIC", L"Ruta del archivo ISO:", WS_CHILD | WS_VISIBLE, 10, 80, 200, 20, parent, NULL, hInst, NULL);
    SendMessage(isoPathLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    isoPathEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY, 10, 100, 600, 25, parent, NULL, hInst, NULL);
    SendMessage(isoPathEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    browseButton = CreateWindowW(L"BUTTON", L"Buscar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 620, 100, 80, 25, parent, (HMENU)IDC_BROWSE_BUTTON, hInst, NULL);
    SendMessage(browseButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    formatLabel = CreateWindowW(L"STATIC", L"Formato del sistema de archivos:", WS_CHILD | WS_VISIBLE, 10, 135, 200, 20, parent, NULL, hInst, NULL);
    SendMessage(formatLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    fat32Radio = CreateWindowW(L"BUTTON", L"FAT32 (Recomendado - Máxima compatibilidad EFI)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 10, 155, 350, 20, parent, (HMENU)IDC_FAT32_RADIO, hInst, NULL);
    SendMessage(fat32Radio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    exfatRadio = CreateWindowW(L"BUTTON", L"exFAT (Sin límite de 4GB por archivo)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 175, 300, 20, parent, (HMENU)IDC_EXFAT_RADIO, hInst, NULL);
    SendMessage(exfatRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    ntfsRadio = CreateWindowW(L"BUTTON", L"NTFS (Soporte completo de Windows)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 195, 300, 20, parent, (HMENU)IDC_NTFS_RADIO, hInst, NULL);
    SendMessage(ntfsRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(ntfsRadio, BM_SETCHECK, BST_CHECKED, 0); // NTFS selected by default

    // Boot mode selection: RAMDISK or Extracted
    bootModeLabel = CreateWindowW(L"STATIC", L"Modo de arranque:", WS_CHILD | WS_VISIBLE, 330, 135, 150, 20, parent, NULL, hInst, NULL);
    SendMessage(bootModeLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    bootRamdiskRadio = CreateWindowW(L"BUTTON", L"Boot desde Memoria (cargar ISO completo en RAM para arranque rápido)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 330, 155, 420, 20, parent, (HMENU)IDC_BOOTMODE_RAMDISK, hInst, NULL);
    SendMessage(bootRamdiskRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    bootExtractedRadio = CreateWindowW(L"BUTTON", L"Instalación Completa (extraer contenido del ISO al disco para arranque estándar)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 330, 175, 420, 40, parent, (HMENU)IDC_BOOTMODE_EXTRACTED, hInst, NULL);
    SendMessage(bootExtractedRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(bootExtractedRadio, BM_SETCHECK, BST_CHECKED, 0); // default: Extracted

    diskSpaceLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 220, 700, 20, parent, NULL, hInst, NULL);
    SendMessage(diskSpaceLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    detailedProgressLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 245, 700, 20, parent, NULL, hInst, NULL);
    SendMessage(detailedProgressLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    detailedProgressBar = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 10, 270, 760, 20, parent, NULL, hInst, NULL);

    createPartitionButton = CreateWindowW(L"BUTTON", L"Realizar proceso y Bootear ISO seleccionado", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 300, 400, 40, parent, (HMENU)IDC_CREATE_PARTITION_BUTTON, hInst, NULL);
    SendMessage(createPartitionButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    progressBar = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 10, 340, 760, 20, parent, NULL, hInst, NULL);

    logTextEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 10, 370, 760, 250, parent, NULL, hInst, NULL);
    SendMessage(logTextEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    footerLabel = CreateWindowW(L"STATIC", L"Versión 1.0", WS_CHILD | WS_VISIBLE, 10, 630, 100, 20, parent, NULL, hInst, NULL);
    SendMessage(footerLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    servicesButton = CreateWindowW(L"BUTTON", L"Servicios", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 630, 100, 20, parent, (HMENU)IDC_SERVICES_BUTTON, hInst, NULL);
    SendMessage(servicesButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

void MainWindow::ApplyStyles()
{
    // Default styles
}

void MainWindow::HandleCommand(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BROWSE_BUTTON:
            OnSelectISO();
            break;
        case IDC_CREATE_PARTITION_BUTTON:
            if (!isProcessing) {
                OnCreatePartition();
            }
            break;
        case IDC_SERVICES_BUTTON:
            OnOpenServicesPage();
            break;
        case IDC_FAT32_RADIO:
            if (HIWORD(wParam) == BN_CLICKED) {
                selectedFormat = "FAT32";
            }
            break;
        case IDC_EXFAT_RADIO:
            if (HIWORD(wParam) == BN_CLICKED) {
                selectedFormat = "EXFAT";
            }
            break;
        case IDC_NTFS_RADIO:
            if (HIWORD(wParam) == BN_CLICKED) {
                selectedFormat = "NTFS";
            }
            break;
        case IDC_BOOTMODE_RAMDISK:
            if (HIWORD(wParam) == BN_CLICKED) {
                selectedBootMode = "RAMDISK";
            }
            break;
        case IDC_BOOTMODE_EXTRACTED:
            if (HIWORD(wParam) == BN_CLICKED) {
                selectedBootMode = "EXTRACTED";
            }
            break;
        }
        break;
    case WM_UPDATE_PROGRESS:
        SendMessage(progressBar, PBM_SETPOS, wParam, 0);
        break;
    case WM_UPDATE_LOG:
        {
            std::string* logMsg = reinterpret_cast<std::string*>(lParam);
            LogMessage(*logMsg);
            delete logMsg;
        }
        break;
    case WM_ENABLE_BUTTON:
        EnableWindow(createPartitionButton, TRUE);
        isProcessing = false;
        break;
    case WM_UPDATE_DETAILED_PROGRESS:
        {
            DetailedProgressData* data = reinterpret_cast<DetailedProgressData*>(lParam);
            UpdateDetailedProgressLabel(data->copied, data->total, data->operation);
            delete data;
        }
        break;
    case WM_UPDATE_ERROR:
        {
            std::string* errorMsg = reinterpret_cast<std::string*>(lParam);
            std::wstring wmsg(errorMsg->begin(), errorMsg->end());
            MessageBoxW(hWndParent, wmsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
            delete errorMsg;
        }
        break;
    }
}

void MainWindow::OnSelectISO()
{
    OPENFILENAMEW ofn;
    WCHAR szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL; // or parent
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
    ofn.lpstrFilter = L"Archivos ISO (*.iso)\0*.iso\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        SetWindowTextW(isoPathEdit, szFile);
    }
}

void MainWindow::OnCreatePartition()
{
    SendMessage(progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(progressBar, PBM_SETPOS, 0, 0);
    SendMessage(detailedProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(detailedProgressBar, PBM_SETPOS, 0, 0);
    LogMessage("Iniciando proceso...\r\n");

    WCHAR isoPath[260];
    GetWindowTextW(isoPathEdit, isoPath, sizeof(isoPath) / sizeof(WCHAR));
    if (wcslen(isoPath) == 0)
    {
        MessageBoxW(NULL, L"Por favor, seleccione un archivo ISO primero.", L"Archivo ISO", MB_OK);
        return;
    }

    bool partitionExists = partitionManager->partitionExists();
    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            WCHAR errorMsg[256];
            MultiByteToWideChar(CP_UTF8, 0, validation.errorMessage.c_str(), -1, errorMsg, 256);
            MessageBoxW(NULL, errorMsg, L"Espacio Insuficiente", MB_OK);
            return;
        }

        if (MessageBoxW(NULL, L"Esta operación modificará el disco del sistema, reduciendo su tamaño en 10.5 GB para crear dos particiones bootables: una ESP FAT32 de 500MB (ISOEFI) y una partición de datos de 10GB (ISOBOOT). ¿Desea continuar?", L"Confirmación de Operación", MB_YESNO) != IDYES)
            return;

        if (MessageBoxW(NULL, L"Esta es la segunda confirmación. La operación de modificación del disco es irreversible y puede causar pérdida de datos si no se realiza correctamente. ¿Está completamente seguro de que desea proceder?", L"Segunda Confirmación", MB_YESNO) != IDYES)
            return;
    }

    // Disable button and start process
    EnableWindow(createPartitionButton, FALSE);
    isProcessing = true;
    std::string isoPathStr = std::string(isoPath, isoPath + wcslen(isoPath));
    processController->startProcess(isoPathStr, selectedFormat, selectedBootMode);
}

void MainWindow::OnOpenServicesPage()
{
    ShellExecuteW(NULL, L"open", L"https://agsoft.co.cr/servicios/", NULL, NULL, SW_SHOWNORMAL);
}

void MainWindow::UpdateDiskSpaceInfo()
{
    long long availableGB = partitionManager->getAvailableSpaceGB();
    bool partitionExists = partitionManager->partitionExists();
    bool efiPartitionExists = !partitionManager->getEfiPartitionDriveLetter().empty();
    const WCHAR* existsStr = partitionExists ? L"Si" : L"No";
    const WCHAR* efiExistsStr = efiPartitionExists ? L"Si" : L"No";
    WCHAR text[200];
    swprintf(text, 200, L"Espacio disponible en C: %lld GB | ISOBOOT: %s | ISOEFI: %s", availableGB, existsStr, efiExistsStr);
    SetWindowTextW(diskSpaceLabel, text);
}

void MainWindow::LogMessage(const std::string& msg)
{
    // Get current time
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::stringstream timeStream;
    timeStream << std::put_time(&localTime, "[%Y-%m-%d %H:%M:%S] ");
    std::string timestampedMsg = timeStream.str() + msg;

    if (generalLogFile.is_open()) {
        generalLogFile << timestampedMsg;
        generalLogFile.flush();
    }
    std::wstring wmsg(timestampedMsg.begin(), timestampedMsg.end());
    int len = GetWindowTextLengthW(logTextEdit);
    SendMessageW(logTextEdit, EM_SETSEL, len, len);
    SendMessageW(logTextEdit, EM_REPLACESEL, FALSE, (LPARAM)wmsg.c_str());
}

bool MainWindow::RestartSystem()
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
    if (GetLastError() != ERROR_SUCCESS)
        return false;
    if (!ExitWindowsEx(EWX_REBOOT, 0))
        return false;
    return true;
}

void MainWindow::onProgressUpdate(int progress) {
    SendMessage(progressBar, PBM_SETPOS, progress, 0);
}

void MainWindow::onLogUpdate(const std::string& message) {
    // Get current time
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);
    std::stringstream timeStream;
    timeStream << std::put_time(localTime, "[%Y-%m-%d %H:%M:%S] ");
    std::string timestampedMsg = timeStream.str() + message;

    std::wstring wmsg(timestampedMsg.begin(), timestampedMsg.end());
    int len = GetWindowTextLengthW(logTextEdit);
    SendMessageW(logTextEdit, EM_SETSEL, len, len);
    SendMessageW(logTextEdit, EM_REPLACESEL, FALSE, (LPARAM)wmsg.c_str());
    if (generalLogFile.is_open()) {
        generalLogFile << timestampedMsg;
        generalLogFile.flush();
    }
}

void MainWindow::onButtonEnable() {
    EnableWindow(createPartitionButton, TRUE);
    isProcessing = false;
}

void MainWindow::onDetailedProgress(long long copied, long long total, const std::string& operation) {
    DetailedProgressData* data = new DetailedProgressData{copied, total, operation};
    PostMessage(hWndParent, WM_UPDATE_DETAILED_PROGRESS, 0, (LPARAM)data);
}

void MainWindow::UpdateDetailedProgressLabel(long long copied, long long total, const std::string& operation) {
    if (total == 0) {
        SetWindowTextW(detailedProgressLabel, L"");
        SendMessage(detailedProgressBar, PBM_SETPOS, 0, 0);
        return;
    }
    int percent = static_cast<int>((copied * 100) / total);
    double copiedMB = copied / (1024.0 * 1024.0);
    double totalMB = total / (1024.0 * 1024.0);
    std::string unit = "MB";
    if (totalMB >= 1024) {
        copiedMB /= 1024;
        totalMB /= 1024;
        unit = "GB";
    }
    std::wstringstream ss;
    ss << operation.c_str() << L": " << percent << L"% (" << std::fixed << std::setprecision(1) << copiedMB << L" " << unit.c_str() << L" / " << totalMB << L" " << unit.c_str() << L")";
    SetWindowTextW(detailedProgressLabel, ss.str().c_str());
    SendMessage(detailedProgressBar, PBM_SETPOS, percent, 0);
}

void MainWindow::onAskRestart() {
    if (MessageBoxW(hWndParent, L"Proceso terminado. ¿Desea reiniciar el sistema ahora?", L"Reiniciar", MB_YESNO) == IDYES) {
        if (!RestartSystem()) {
            MessageBoxW(hWndParent, L"Error al reiniciar el sistema.", L"Error", MB_OK);
        }
    }
}

void MainWindow::onError(const std::string& message) {
    std::string* msg = new std::string(message);
    PostMessage(hWndParent, WM_UPDATE_ERROR, 0, (LPARAM)msg);
}