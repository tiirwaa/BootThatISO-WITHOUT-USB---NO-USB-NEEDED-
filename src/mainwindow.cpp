#include "mainwindow.h"
#include "constants.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <cstring>

MainWindow::MainWindow(HWND parent)
    : hInst(GetModuleHandle(NULL)), selectedFormat("FAT32")
{
    partitionManager = new PartitionManager();
    isoCopyManager = new ISOCopyManager();
    bcdManager = new BCDManager();
    if (partitionManager->partitionExists()) {
        bcdManager->restoreBCD();
    }
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
    SendMessage(fat32Radio, BM_SETCHECK, BST_CHECKED, 0); // FAT32 selected by default

    exfatRadio = CreateWindowW(L"BUTTON", L"exFAT (Sin límite de 4GB por archivo)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 175, 300, 20, parent, (HMENU)IDC_EXFAT_RADIO, hInst, NULL);
    SendMessage(exfatRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    diskSpaceLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 200, 700, 20, parent, NULL, hInst, NULL);
    SendMessage(diskSpaceLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    createPartitionButton = CreateWindowW(L"BUTTON", L"Realizar proceso y Bootear ISO seleccionado", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 230, 400, 40, parent, (HMENU)IDC_CREATE_PARTITION_BUTTON, hInst, NULL);
    SendMessage(createPartitionButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    progressBar = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 10, 280, 760, 20, parent, NULL, hInst, NULL);

    logTextEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 10, 310, 760, 250, parent, NULL, hInst, NULL);
    SendMessage(logTextEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    footerLabel = CreateWindowW(L"STATIC", L"Versión 1.0", WS_CHILD | WS_VISIBLE, 10, 570, 100, 20, parent, NULL, hInst, NULL);
    SendMessage(footerLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    servicesButton = CreateWindowW(L"BUTTON", L"Servicios", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 570, 100, 20, parent, (HMENU)IDC_SERVICES_BUTTON, hInst, NULL);
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
    LogMessage("Iniciando proceso...\r\n");

    WCHAR isoPath[260];
    GetWindowTextW(isoPathEdit, isoPath, sizeof(isoPath) / sizeof(WCHAR));
    if (wcslen(isoPath) == 0)
    {
        MessageBoxW(NULL, L"Por favor, seleccione un archivo ISO primero.", L"Archivo ISO", MB_OK);
        return;
    }

    bool partitionExists = partitionManager->partitionExists();
    if (partitionExists) {
        std::string currentFS = partitionManager->getPartitionFileSystem();
        std::string targetFS = (selectedFormat == "EXFAT") ? "exFAT" : "FAT32";
        if (_stricmp(currentFS.c_str(), targetFS.c_str()) != 0) {
            // reformat
            LogMessage("La partición existe con formato diferente. Reformateando...\r\n");
            SendMessage(progressBar, PBM_SETPOS, 20, 0);
            if (!partitionManager->reformatPartition(selectedFormat)) {
                LogMessage("Error al reformatear la partición.\r\n");
                MessageBoxW(NULL, L"Error al reformatear la partición.", L"Error", MB_OK);
                return;
            }
            LogMessage("Partición reformateada.\r\n");
            SendMessage(progressBar, PBM_SETPOS, 30, 0);
        }
    } else {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            WCHAR errorMsg[256];
            MultiByteToWideChar(CP_UTF8, 0, validation.errorMessage.c_str(), -1, errorMsg, 256);
            MessageBoxW(NULL, errorMsg, L"Espacio Insuficiente", MB_OK);
            return;
        }
        LogMessage("Espacio validado.\r\n");
        SendMessage(progressBar, PBM_SETPOS, 10, 0);

        if (MessageBoxW(NULL, L"Esta operación modificará el disco del sistema, reduciendo su tamaño en 10 GB para crear una partición bootable. ¿Desea continuar?", L"Confirmación de Operación", MB_YESNO) != IDYES)
            return;

        if (MessageBoxW(NULL, L"Esta es la segunda confirmación. La operación de modificación del disco es irreversible y puede causar pérdida de datos si no se realiza correctamente. ¿Está completamente seguro de que desea proceder?", L"Segunda Confirmación", MB_YESNO) != IDYES)
            return;

        LogMessage("Confirmaciones completadas. Creando partición...\r\n");
        SendMessage(progressBar, PBM_SETPOS, 20, 0);

        if (!partitionManager->createPartition(selectedFormat)) {
            LogMessage("Error al crear la partición.\r\n");
            MessageBoxW(NULL, L"Error al crear la partición.", L"Error", MB_OK);
            return;
        }
        LogMessage("Partición creada.\r\n");
        SendMessage(progressBar, PBM_SETPOS, 30, 0);
    }

    // Verificar que la partición ISOBOOT se puede encontrar
    std::string partitionDrive = partitionManager->getPartitionDriveLetter();
    if (partitionDrive.empty()) {
        LogMessage("Error: No se puede acceder a la partición " + std::string(VOLUME_LABEL) + ".\r\n");
        MessageBoxW(NULL, L"No se puede acceder a la partición ISOBOOT. Verifique el log para más detalles.", L"Error", MB_OK);
        return;
    }
    LogMessage("Partición ISOBOOT encontrada en: " + partitionDrive + "\r\n");

    if (OnCopyISO()) {
        OnConfigureBCD();
        LogMessage("Proceso completado.\r\n");
        SendMessage(progressBar, PBM_SETPOS, 100, 0);
    } else {
        LogMessage("Proceso fallido debido a errores en la copia del ISO.\r\n");
    }
}

bool MainWindow::OnCopyISO()
{
    LogMessage("Extrayendo archivos EFI del ISO...\r\n");
    SendMessage(progressBar, PBM_SETPOS, 40, 0);

    WCHAR isoPath[260];
    GetWindowTextW(isoPathEdit, isoPath, sizeof(isoPath) / sizeof(WCHAR));
    int len = WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, NULL, 0, NULL, NULL);
    char* buffer = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, buffer, len, NULL, NULL);
    std::string isoPathStr = buffer;
    delete[] buffer;

    std::string drive = partitionManager->getPartitionDriveLetter();
    if (drive.empty()) {
        LogMessage("Partición 'ISOBOOT' no encontrada.\r\n");
        MessageBoxW(NULL, L"Partición 'ISOBOOT' no encontrada.", L"Error", MB_OK);
        return false;
    }

    std::string dest = drive;
    if (isoCopyManager->extractISOContents(isoPathStr, dest)) {
        LogMessage("Archivos extraídos exitosamente.\r\n");
        SendMessage(progressBar, PBM_SETPOS, 55, 0);
    } else {
        LogMessage("Error al extraer archivos del ISO.\r\n");
        MessageBoxW(NULL, L"Error al extraer archivos del ISO.", L"Error", MB_OK);
        return false;
    }

    LogMessage("Copiando archivo ISO completo...\r\n");
    if (isoCopyManager->copyISOFile(isoPathStr, dest)) {
        LogMessage("Archivo ISO copiado exitosamente.\r\n");
        SendMessage(progressBar, PBM_SETPOS, 70, 0);
    } else {
        LogMessage("Error al copiar el archivo ISO.\r\n");
        MessageBoxW(NULL, L"Error al copiar el archivo ISO.", L"Error", MB_OK);
        return false;
    }
    return true;
}

void MainWindow::OnConfigureBCD()
{
    LogMessage("Configurando BCD...\r\n");
    SendMessage(progressBar, PBM_SETPOS, 80, 0);

    std::string drive = partitionManager->getPartitionDriveLetter();
    if (drive.empty()) {
        LogMessage("Partición 'ISOBOOT' no encontrada.\r\n");
        MessageBoxW(NULL, L"Partición 'ISOBOOT' no encontrada.", L"Error", MB_OK);
        return;
    }

    std::string driveLetter = drive.substr(0, 2);
    std::string error = bcdManager->configureBCD(driveLetter);
    if (!error.empty()) {
        LogMessage("Error al configurar BCD: " + error + "\r\n");
        std::wstring werror(error.begin(), error.end());
        MessageBoxW(NULL, werror.c_str(), L"Error", MB_OK);
    } else {
        LogMessage("BCD configurado exitosamente.\r\n");
        SendMessage(progressBar, PBM_SETPOS, 100, 0);
    }
}

void MainWindow::OnOpenServicesPage()
{
    ShellExecuteW(NULL, L"open", L"https://agsoft.co.cr/servicios/", NULL, NULL, SW_SHOWNORMAL);
}

void MainWindow::UpdateDiskSpaceInfo()
{
    long long availableGB = partitionManager->getAvailableSpaceGB();
    bool partitionExists = partitionManager->partitionExists();
    const WCHAR* existsStr = partitionExists ? L"Si" : L"No";
    WCHAR text[200];
    swprintf(text, 200, L"Espacio disponible en C: %lld GB | Particion 'ISOBOOT' encontrada: %s", availableGB, existsStr);
    SetWindowTextW(diskSpaceLabel, text);
}

void MainWindow::LogMessage(const std::string& msg)
{
    std::wstring wmsg(msg.begin(), msg.end());
    int len = GetWindowTextLengthW(logTextEdit);
    SendMessageW(logTextEdit, EM_SETSEL, len, len);
    SendMessageW(logTextEdit, EM_REPLACESEL, FALSE, (LPARAM)wmsg.c_str());
}