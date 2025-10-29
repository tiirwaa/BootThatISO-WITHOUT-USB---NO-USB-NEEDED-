#include "mainwindow.h"
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <sstream>

MainWindow::MainWindow(HWND parent)
    : hInst(GetModuleHandle(NULL))
{
    partitionManager = new PartitionManager();
    isoCopyManager = new ISOCopyManager();
    bcdManager = new BCDManager();
    SetupUI(parent);
    UpdateDiskSpaceInfo();
}

MainWindow::~MainWindow()
{
    delete partitionManager;
    delete isoCopyManager;
    delete bcdManager;
}

void MainWindow::SetupUI(HWND parent)
{
    // Create controls
    logoLabel = CreateWindowW(L"STATIC", L"LOGO", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 65, 65, parent, NULL, hInst, NULL);
    SendMessage(logoLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    titleLabel = CreateWindowW(L"STATIC", L"EASY ISOBOOT", WS_CHILD | WS_VISIBLE, 75, 10, 300, 30, parent, NULL, hInst, NULL);
    SendMessage(titleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    subtitleLabel = CreateWindowW(L"STATIC", L"Configuración de Partición Bootable", WS_CHILD | WS_VISIBLE, 75, 40, 300, 20, parent, NULL, hInst, NULL);
    SendMessage(subtitleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    saveButton = CreateWindowW(L"BUTTON", L"Guardar Config", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 10, 130, 30, parent, NULL, hInst, NULL);
    SendMessage(saveButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    isoPathLabel = CreateWindowW(L"STATIC", L"Ruta del archivo ISO:", WS_CHILD | WS_VISIBLE, 10, 80, 200, 20, parent, NULL, hInst, NULL);
    SendMessage(isoPathLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    isoPathEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY, 10, 100, 600, 25, parent, NULL, hInst, NULL);
    SendMessage(isoPathEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    browseButton = CreateWindowW(L"BUTTON", L"Buscar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 620, 100, 80, 25, parent, (HMENU)IDC_BROWSE_BUTTON, hInst, NULL);
    SendMessage(browseButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    diskSpaceLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 140, 400, 20, parent, NULL, hInst, NULL);
    SendMessage(diskSpaceLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    createPartitionButton = CreateWindowW(L"BUTTON", L"Realizar proceso y Bootear ISO seleccionado", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 170, 400, 40, parent, (HMENU)IDC_CREATE_PARTITION_BUTTON, hInst, NULL);
    SendMessage(createPartitionButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    logTextEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 10, 220, 760, 300, parent, NULL, hInst, NULL);
    SendMessage(logTextEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    footerLabel = CreateWindowW(L"STATIC", L"Versión 1.0", WS_CHILD | WS_VISIBLE, 10, 530, 100, 20, parent, NULL, hInst, NULL);
    SendMessage(footerLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    servicesButton = CreateWindowW(L"BUTTON", L"Servicios", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 530, 100, 20, parent, (HMENU)IDC_SERVICES_BUTTON, hInst, NULL);
    SendMessage(servicesButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

void MainWindow::ApplyStyles()
{
    // Default styles
}

void MainWindow::HandleCommand(WPARAM wParam, LPARAM lParam)
{
    switch (LOWORD(wParam))
    {
    case IDC_BROWSE_BUTTON:
        OnSelectISO();
        break;
    case IDC_CREATE_PARTITION_BUTTON:
        OnCreatePartition();
        break;
    case IDC_SERVICES_BUTTON:
        OnOpenServicesPage();
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
    WCHAR isoPath[260];
    GetWindowTextW(isoPathEdit, isoPath, sizeof(isoPath) / sizeof(WCHAR));
    if (wcslen(isoPath) == 0)
    {
        MessageBoxW(NULL, L"Por favor, seleccione un archivo ISO primero.", L"Archivo ISO", MB_OK);
        return;
    }

    if (!partitionManager->partitionExists())
    {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid)
        {
            WCHAR errorMsg[256];
            MultiByteToWideChar(CP_UTF8, 0, validation.errorMessage.c_str(), -1, errorMsg, 256);
            MessageBoxW(NULL, errorMsg, L"Espacio Insuficiente", MB_OK);
            return;
        }

        if (MessageBoxW(NULL, L"Esta operación modificará el disco del sistema, reduciendo su tamaño en 10 GB para crear una partición bootable. ¿Desea continuar?", L"Confirmación de Operación", MB_YESNO) != IDYES)
            return;

        if (MessageBoxW(NULL, L"Esta es la segunda confirmación. La operación de modificación del disco es irreversible y puede causar pérdida de datos si no se realiza correctamente. ¿Está completamente seguro de que desea proceder?", L"Segunda Confirmación", MB_YESNO) != IDYES)
            return;

        if (!partitionManager->createPartition())
        {
            MessageBoxW(NULL, L"Error al crear la partición.", L"Error", MB_OK);
            return;
        }
    }

    OnCopyISO();
    OnConfigureBCD();
}

void MainWindow::OnCopyISO()
{
    WCHAR isoPath[260];
    GetWindowTextW(isoPathEdit, isoPath, sizeof(isoPath) / sizeof(WCHAR));
    int len = WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, NULL, 0, NULL, NULL);
    char* buffer = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, buffer, len, NULL, NULL);
    std::string isoPathStr = buffer;
    delete[] buffer;
    if (isoCopyManager->copyISO(isoPathStr))
    {
        MessageBoxW(NULL, L"Función no implementada aún.", L"Copiar ISO", MB_OK);
    }
    else
    {
        MessageBoxW(NULL, L"Error al copiar el ISO.", L"Error", MB_OK);
    }
}

void MainWindow::OnConfigureBCD()
{
    if (bcdManager->configureBCD())
    {
        MessageBoxW(NULL, L"Función no implementada aún.", L"Configurar BCD", MB_OK);
    }
    else
    {
        MessageBoxW(NULL, L"Error al configurar BCD.", L"Error", MB_OK);
    }
}

void MainWindow::OnOpenServicesPage()
{
    ShellExecuteW(NULL, L"open", L"https://agsoft.co.cr/servicios/", NULL, NULL, SW_SHOWNORMAL);
}

void MainWindow::UpdateDiskSpaceInfo()
{
    long long availableGB = partitionManager->getAvailableSpaceGB();
    WCHAR text[100];
    swprintf(text, 100, L"Espacio disponible en C: %lld GB", availableGB);
    SetWindowTextW(diskSpaceLabel, text);
}