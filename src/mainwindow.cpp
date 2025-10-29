#include "mainwindow.h"
#include "constants.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <cstring>

MainWindow::MainWindow(HWND parent)
    : hInst(GetModuleHandle(NULL)), hWndParent(parent), selectedFormat("NTFS"), selectedBootMode("EXTRACTED"), workerThread(nullptr), isProcessing(false)
{
    partitionManager = new PartitionManager();
    isoCopyManager = new ISOCopyManager();
    bcdManager = new BCDManager();
    generalLogFile.open("general_log.txt", std::ios::app);
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

    bootRamdiskRadio = CreateWindowW(L"BUTTON", L"RAMDISK (copiar ISO y configurar BCD con ramdisk)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 330, 155, 420, 20, parent, (HMENU)IDC_BOOTMODE_RAMDISK, hInst, NULL);
    SendMessage(bootRamdiskRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    bootExtractedRadio = CreateWindowW(L"BUTTON", L"Extraido (copiar contenido del ISO y configurar BCD sin ramdisk)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 330, 175, 420, 40, parent, (HMENU)IDC_BOOTMODE_EXTRACTED, hInst, NULL);
    SendMessage(bootExtractedRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(bootExtractedRadio, BM_SETCHECK, BST_CHECKED, 0); // default: Extracted

    diskSpaceLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 220, 700, 20, parent, NULL, hInst, NULL);
    SendMessage(diskSpaceLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    createPartitionButton = CreateWindowW(L"BUTTON", L"Realizar proceso y Bootear ISO seleccionado", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 290, 400, 40, parent, (HMENU)IDC_CREATE_PARTITION_BUTTON, hInst, NULL);
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

    // Disable button and start thread
    EnableWindow(createPartitionButton, FALSE);
    isProcessing = true;
    workerThread = new std::thread(&MainWindow::ProcessInThread, this);
}

bool MainWindow::OnCopyISO()
{
    PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Extrayendo archivos EFI del ISO...\r\n"));
    PostMessage(hWndParent, WM_UPDATE_PROGRESS, 40, 0);

    WCHAR isoPath[260];
    GetWindowTextW(isoPathEdit, isoPath, sizeof(isoPath) / sizeof(WCHAR));
    int len = WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, NULL, 0, NULL, NULL);
    char* buffer = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, buffer, len, NULL, NULL);
    std::string isoPathStr = buffer;
    delete[] buffer;

    std::string drive = partitionManager->getPartitionDriveLetter();
    if (drive.empty()) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición 'ISOBOOT' no encontrada.\r\n"));
        return false;
    }

    std::string dest = drive;
    std::string espDrive = partitionManager->getEfiPartitionDriveLetter();
    if (espDrive.empty()) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición 'ISOEFI' no encontrada.\r\n"));
        return false;
    }
    if (selectedBootMode == "RAMDISK") {
        // For RAMDISK: only copy EFI to ESP, do not extract content
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Modo RAMDISK: copiando solo EFI, sin extraer contenido...\r\n"));
        if (isoCopyManager->extractISOContents(isoPathStr, dest, espDrive, false)) {  // false = no extract content
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("EFI copiado exitosamente.\r\n"));
            PostMessage(hWndParent, WM_UPDATE_PROGRESS, 55, 0);
        } else {
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error al copiar EFI.\r\n"));
            return false;
        }
        // Then copy the full ISO
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Copiando archivo ISO completo...\r\n"));
        if (isoCopyManager->copyISOFile(isoPathStr, dest)) {
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Archivo ISO copiado exitosamente.\r\n"));
            PostMessage(hWndParent, WM_UPDATE_PROGRESS, 70, 0);
        } else {
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error al copiar el archivo ISO.\r\n"));
            return false;
        }
    } else {
        // For EXTRACTED: extract content and EFI
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Modo Extraido: extrayendo contenido y EFI...\r\n"));
        if (isoCopyManager->extractISOContents(isoPathStr, dest, espDrive, true)) {  // true = extract content
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Archivos extraídos exitosamente.\r\n"));
            PostMessage(hWndParent, WM_UPDATE_PROGRESS, 70, 0);
        } else {
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error al extraer archivos del ISO.\r\n"));
            return false;
        }
    }
    return true;
}

void MainWindow::OnConfigureBCD()
{
    PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Configurando BCD...\r\n"));
    PostMessage(hWndParent, WM_UPDATE_PROGRESS, 80, 0);

    std::string drive = partitionManager->getPartitionDriveLetter();
    if (drive.empty()) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición 'ISOBOOT' no encontrada.\r\n"));
        return;
    }

    std::string driveLetter = drive.substr(0, 2);
    std::string espDrive = partitionManager->getEfiPartitionDriveLetter();
    if (espDrive.empty()) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición 'ISOEFI' no encontrada.\r\n"));
        return;
    }
    std::string espDriveLetter = espDrive.substr(0, 2);
    std::string error = bcdManager->configureBCD(driveLetter, espDriveLetter, selectedBootMode);
    if (!error.empty()) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error al configurar BCD: " + error + "\r\n"));
    } else {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("BCD configurado exitosamente.\r\n"));
        PostMessage(hWndParent, WM_UPDATE_PROGRESS, 100, 0);
    }
}

void MainWindow::ProcessInThread()
{
    // Copied from OnCreatePartition after validations
    bool partitionExists = partitionManager->partitionExists();
    if (partitionExists) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Las particiones existen. Reformateando...\r\n"));
        PostMessage(hWndParent, WM_UPDATE_PROGRESS, 20, 0);
        if (!partitionManager->reformatPartition(selectedFormat)) {
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error al reformatear la partición.\r\n"));
            PostMessage(hWndParent, WM_ENABLE_BUTTON, 0, 0);
            return;
        }
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición reformateada.\r\n"));
        PostMessage(hWndParent, WM_UPDATE_PROGRESS, 30, 0);
    } else {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Espacio validado.\r\n"));
        PostMessage(hWndParent, WM_UPDATE_PROGRESS, 10, 0);

        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Confirmaciones completadas. Creando partición...\r\n"));
        PostMessage(hWndParent, WM_UPDATE_PROGRESS, 20, 0);

        if (!partitionManager->createPartition(selectedFormat)) {
            PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error al crear la partición.\r\n"));
            PostMessage(hWndParent, WM_ENABLE_BUTTON, 0, 0);
            return;
        }
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición creada.\r\n"));
        PostMessage(hWndParent, WM_UPDATE_PROGRESS, 30, 0);
    }

    // Verificar que la partición ISOBOOT se puede encontrar
    std::string partitionDrive = partitionManager->getPartitionDriveLetter();
    if (partitionDrive.empty()) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error: No se puede acceder a la partición " + std::string(VOLUME_LABEL) + ".\r\n"));
        PostMessage(hWndParent, WM_ENABLE_BUTTON, 0, 0);
        return;
    }
    PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición ISOBOOT encontrada en: " + partitionDrive + "\r\n"));
    std::string espDrive = partitionManager->getEfiPartitionDriveLetter();
    if (espDrive.empty()) {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Error: No se puede acceder a la partición ISOEFI.\r\n"));
        PostMessage(hWndParent, WM_ENABLE_BUTTON, 0, 0);
        return;
    }
    PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Partición ISOEFI encontrada en: " + espDrive + "\r\n"));

    if (OnCopyISO()) {
        OnConfigureBCD();
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Proceso completado.\r\n"));
        PostMessage(hWndParent, WM_UPDATE_PROGRESS, 100, 0);
    } else {
        PostMessage(hWndParent, WM_UPDATE_LOG, 0, (LPARAM)new std::string("Proceso fallido debido a errores en la copia del ISO.\r\n"));
    }
    PostMessage(hWndParent, WM_ENABLE_BUTTON, 0, 0);
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
    if (generalLogFile.is_open()) {
        generalLogFile << msg;
        generalLogFile.flush();
    }
    std::wstring wmsg(msg.begin(), msg.end());
    int len = GetWindowTextLengthW(logTextEdit);
    SendMessageW(logTextEdit, EM_SETSEL, len, len);
    SendMessageW(logTextEdit, EM_REPLACESEL, FALSE, (LPARAM)wmsg.c_str());
}