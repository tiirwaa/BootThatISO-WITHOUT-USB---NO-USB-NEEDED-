#include "mainwindow.h"
#include "../utils/constants.h"
#include "../utils/Utils.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"
#include "../utils/AppKeys.h"
#include "../utils/Logger.h"
#include "../models/BootStrategyFactory.h"
#include "version.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <ctime>
#include <memory>
#include <objidl.h>
#include "../resource.h"

#define WM_UPDATE_DETAILED_PROGRESS (WM_USER + 5)
#define WM_UPDATE_ERROR (WM_USER + 6)

namespace {
constexpr int LOGO_TARGET_WIDTH = 56;
constexpr int LOGO_TARGET_HEIGHT = 56;
constexpr int BUTTON_ICON_WIDTH = 160;
constexpr int BUTTON_ICON_HEIGHT = 160;
constexpr int BUTTON_ICON_LEFT_MARGIN = 6;
constexpr int BUTTON_ICON_TEXT_GAP = 12;
}

struct DetailedProgressData {
    long long copied;
    long long total;
    std::string operation;
};

#include <objidl.h>

Gdiplus::Bitmap* LoadBitmapFromResource(int resourceId) {
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hRes) return nullptr;
    HGLOBAL hGlob = LoadResource(NULL, hRes);
    if (!hGlob) return nullptr;
    LPVOID pData = LockResource(hGlob);
    DWORD size = SizeofResource(NULL, hRes);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    LPVOID pMem = GlobalLock(hMem);
    memcpy(pMem, pData, size);
    GlobalUnlock(hMem);
    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(hMem, TRUE, &pStream);
    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    return bitmap;
}

Gdiplus::Bitmap* ResizeBitmap(Gdiplus::Bitmap* source, int targetWidth, int targetHeight) {
    if (!source || targetWidth <= 0 || targetHeight <= 0) {
        return nullptr;
    }

    const UINT srcWidth = source->GetWidth();
    const UINT srcHeight = source->GetHeight();
    if (srcWidth == 0 || srcHeight == 0) {
        return nullptr;
    }

    const double aspect = static_cast<double>(srcWidth) / static_cast<double>(srcHeight);
    int destWidth = targetWidth;
    int destHeight = static_cast<int>(destWidth / aspect);

    if (destHeight > targetHeight) {
        destHeight = targetHeight;
        destWidth = static_cast<int>(destHeight * aspect);
    }

    // Ensure we end up with at least 1px in both dimensions.
    if (destWidth < 1) destWidth = 1;
    if (destHeight < 1) destHeight = 1;

    auto* scaled = new Gdiplus::Bitmap(destWidth, destHeight, PixelFormat32bppARGB);
    if (!scaled) {
        return nullptr;
    }

    Gdiplus::Graphics graphics(scaled);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.DrawImage(source, Gdiplus::Rect(0, 0, destWidth, destHeight));
    return scaled;
}

MainWindow::MainWindow(HWND parent)
    : hInst(GetModuleHandle(NULL)), hWndParent(parent), selectedFormat("NTFS"), selectedBootModeKey(AppKeys::BootModeExtract), isProcessing(false), isRecovering(false), skipIntegrityCheck(true), logoBitmap(nullptr), logoHIcon(nullptr), buttonHIcon(nullptr), buttonIconOwned(false)
{
    partitionManager = &PartitionManager::getInstance();
    isoCopyManager = &ISOCopyManager::getInstance();
    bcdManager = &BCDManager::getInstance();
    eventManager.addObserver(this);
    processController = std::make_unique<ProcessController>(eventManager);
    if (partitionManager->partitionExists()) {
        bcdManager->restoreBCD();
    }
    LoadTexts();
    SetupUI(parent);
    UpdateDiskSpaceInfo();
}

MainWindow::~MainWindow()
{
    if (logoBitmap) delete logoBitmap;
    if (logoHIcon) DestroyIcon(logoHIcon);
    if (buttonIconOwned && buttonHIcon) DestroyIcon(buttonHIcon);
}

void MainWindow::requestCancel()
{
    if (isRecovering) {
        LogMessage(LocalizedOrUtf8("log.recovery.cannotCancel", "La recuperaci?n de espacio est? en curso y no se puede cancelar. Espera a que finalice.\r\n"));
        return;
    }
    if (processController) {
        LogMessage(LocalizedOrUtf8("log.cancel.requested", "Solicitud de cancelaci?n enviada. Esperando limpieza...\r\n"));
        processController->requestCancel();
        LogMessage(LocalizedOrUtf8("log.cancel.completed", "Operaci?n cancelada y limpiada.\r\n"));
        onButtonEnable();
    }
}

void MainWindow::LoadTexts()
{
    logoText = LocalizedOrW("mainwindow.logoPlaceholder", L"LOGO");
    titleText = LocalizedOrW("mainwindow.title", L"BOOT THAT ISO!");
    subtitleText = LocalizedOrW("mainwindow.subtitle", L"Configuracion de Particiones Bootables EFI");
    isoLabelText = LocalizedOrW("mainwindow.isoPathLabel", L"Ruta del archivo ISO:");
    browseText = LocalizedOrW("mainwindow.browseButton", L"Buscar");
    formatText = LocalizedOrW("mainwindow.formatLabel", L"Formato del sistema de archivos:");
    fat32Text = LocalizedOrW("mainwindow.format.fat32", L"FAT32 (Recomendado - Maxima compatibilidad EFI)");
    exfatText = LocalizedOrW("mainwindow.format.exfat", L"exFAT (Sin limite de 4GB por archivo)");
    ntfsText = LocalizedOrW("mainwindow.format.ntfs", L"NTFS (Soporte completo de Windows)");
    bootModeText = LocalizedOrW("mainwindow.bootModeLabel", L"Modo de arranque:");
    bootRamText = LocalizedOrW("bootMode.ram", L"Boot desde RAM");
    bootDiskText = LocalizedOrW("bootMode.extract", L"Boot desde Disco");
    integrityText = LocalizedOrW("mainwindow.integrityCheck", L"Realizar verificacion de la integridad del disco");
    createButtonText = LocalizedOrW("mainwindow.createButton", L"Realizar proceso y Bootear ISO seleccionado");
    versionText = LocalizedFormatW("mainwindow.versionLabel", { Utils::utf8_to_wstring(APP_VERSION) }, L"Version {0}");
    servicesText = LocalizedOrW("mainwindow.servicesButton", L"Servicios");
    recoverText = LocalizedOrW("mainwindow.recoverButton", L"Recuperar mi espacio");
}

void MainWindow::SetupUI(HWND parent)
{
    LoadTexts();
    logoBitmap = LoadBitmapFromResource(IDR_AG_LOGO);
    if (logoBitmap) {
        if (Gdiplus::Bitmap* resizedLogo = ResizeBitmap(logoBitmap, LOGO_TARGET_WIDTH, LOGO_TARGET_HEIGHT)) {
            delete logoBitmap;
            logoBitmap = resizedLogo;
        }
    }
    buttonHIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, BUTTON_ICON_WIDTH, BUTTON_ICON_HEIGHT, LR_CREATEDIBSECTION));
    if (buttonHIcon) {
        buttonIconOwned = true;
    } else {
        HICON sharedIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
        if (sharedIcon) {
            buttonHIcon = CopyIcon(sharedIcon);
            buttonIconOwned = (buttonHIcon != nullptr);
        }
    }
    CreateControls(parent);
    ApplyStyles();
}

void MainWindow::CreateControls(HWND parent)
{
    int logoWidth = LOGO_TARGET_WIDTH;
    int logoHeight = LOGO_TARGET_HEIGHT;
    if (logoBitmap) {
        logoWidth = static_cast<int>(logoBitmap->GetWidth());
        logoHeight = static_cast<int>(logoBitmap->GetHeight());
    }
    logoLabel = CreateWindowW(L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE, 10, 10, logoWidth, logoHeight, parent, NULL, hInst, NULL);
    if (logoBitmap && logoBitmap->GetHICON(&logoHIcon) == Gdiplus::Ok) {
        HICON previousIcon = reinterpret_cast<HICON>(SendMessage(logoLabel, STM_SETICON, (WPARAM)logoHIcon, 0));
        if (previousIcon && previousIcon != logoHIcon) {
            DestroyIcon(previousIcon);
        }
    }

    titleLabel = CreateWindowW(L"STATIC", titleText.c_str(), WS_CHILD | WS_VISIBLE, 70, 10, 300, 30, parent, NULL, hInst, NULL);
    SendMessage(titleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    subtitleLabel = CreateWindowW(L"STATIC", subtitleText.c_str(), WS_CHILD | WS_VISIBLE, 70, 40, 300, 20, parent, NULL, hInst, NULL);
    SendMessage(subtitleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    isoPathLabel = CreateWindowW(L"STATIC", isoLabelText.c_str(), WS_CHILD | WS_VISIBLE, 10, 80, 200, 20, parent, NULL, hInst, NULL);
    SendMessage(isoPathLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    isoPathEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY, 10, 100, 600, 25, parent, NULL, hInst, NULL);
    SendMessage(isoPathEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    browseButton = CreateWindowW(L"BUTTON", browseText.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 620, 100, 80, 25, parent, (HMENU)IDC_BROWSE_BUTTON, hInst, NULL);
    SendMessage(browseButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    formatLabel = CreateWindowW(L"STATIC", formatText.c_str(), WS_CHILD | WS_VISIBLE, 10, 135, 200, 20, parent, NULL, hInst, NULL);
    SendMessage(formatLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    fat32Radio = CreateWindowW(L"BUTTON", fat32Text.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 10, 155, 350, 20, parent, (HMENU)IDC_FAT32_RADIO, hInst, NULL);
    SendMessage(fat32Radio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    exfatRadio = CreateWindowW(L"BUTTON", exfatText.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 175, 300, 20, parent, (HMENU)IDC_EXFAT_RADIO, hInst, NULL);
    SendMessage(exfatRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    ntfsRadio = CreateWindowW(L"BUTTON", ntfsText.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 10, 195, 300, 20, parent, (HMENU)IDC_NTFS_RADIO, hInst, NULL);
    SendMessage(ntfsRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(ntfsRadio, BM_SETCHECK, BST_CHECKED, 0);

    bootModeLabel = CreateWindowW(L"STATIC", bootModeText.c_str(), WS_CHILD | WS_VISIBLE, 330, 135, 150, 20, parent, NULL, hInst, NULL);
    SendMessage(bootModeLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    bootRamdiskRadio = CreateWindowW(L"BUTTON", bootRamText.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 330, 155, 420, 20, parent, (HMENU)IDC_BOOTMODE_RAMDISK, hInst, NULL);
    SendMessage(bootRamdiskRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    bootExtractedRadio = CreateWindowW(L"BUTTON", bootDiskText.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 330, 175, 420, 40, parent, (HMENU)IDC_BOOTMODE_EXTRACTED, hInst, NULL);
    SendMessage(bootExtractedRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(bootExtractedRadio, BM_SETCHECK, BST_CHECKED, 0);

    integrityCheckBox = CreateWindowW(L"BUTTON", integrityText.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 10, 220, 350, 20, parent, (HMENU)IDC_INTEGRITY_CHECKBOX, hInst, NULL);
    SendMessage(integrityCheckBox, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    diskSpaceLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 240, 700, 20, parent, NULL, hInst, NULL);
    SendMessage(diskSpaceLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    detailedProgressLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 265, 700, 20, parent, NULL, hInst, NULL);
    SendMessage(detailedProgressLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    detailedProgressBar = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 10, 290, 760, 20, parent, NULL, hInst, NULL);

    DWORD createButtonStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE | BS_CENTER | BS_VCENTER | BS_OWNERDRAW;
    createPartitionButton = CreateWindowW(L"BUTTON", createButtonText.c_str(), createButtonStyle, 140, 320, 520, 160, parent, (HMENU)IDC_CREATE_PARTITION_BUTTON, hInst, NULL);
    SendMessage(createPartitionButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    progressBar = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 10, 500, 760, 20, parent, NULL, hInst, NULL);

    logTextEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 10, 530, 760, 120, parent, NULL, hInst, NULL);
    SendMessage(logTextEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    footerLabel = CreateWindowW(L"STATIC", versionText.c_str(), WS_CHILD | WS_VISIBLE, 10, 655, 140, 20, parent, NULL, hInst, NULL);
    SendMessage(footerLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    servicesButton = CreateWindowW(L"BUTTON", servicesText.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 650, 655, 100, 20, parent, (HMENU)IDC_SERVICES_BUTTON, hInst, NULL);
    SendMessage(servicesButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

    recoverButton = CreateWindowW(L"BUTTON", recoverText.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 500, 655, 140, 20, parent, (HMENU)IDC_RECOVER_BUTTON, hInst, NULL);
    SendMessage(recoverButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

void MainWindow::ApplyStyles()
{
    // Set fonts for all controls
    SendMessage(titleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(subtitleLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(isoPathLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(isoPathEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(browseButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(formatLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(fat32Radio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(exfatRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(ntfsRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(bootModeLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(bootRamdiskRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(bootExtractedRadio, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(integrityCheckBox, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(diskSpaceLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(detailedProgressLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(createPartitionButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(logTextEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(footerLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(servicesButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessage(recoverButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
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
          case IDC_RECOVER_BUTTON:
              if (!isRecovering) {
                  if (processController->recoverSpace()) {
                      isRecovering = true;
                      EnableWindow(recoverButton, FALSE);
                      LogMessage(LocalizedOrUtf8("log.recovery.started", "Recuperacion de espacio iniciada en segundo plano.\r\n"));
                  } else {
                      LogMessage(LocalizedOrUtf8("log.recovery.startFailed", "No se pudo iniciar la recuperacion de espacio. Verifica si ya esta en curso.\r\n"));
                  }
              } else {
                  LogMessage(LocalizedOrUtf8("log.recovery.alreadyRunning", "La recuperacion de espacio ya esta en progreso.\r\n"));
              }
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
                selectedBootModeKey = AppKeys::BootModeRam;
            }
            break;
        case IDC_BOOTMODE_EXTRACTED:
            if (HIWORD(wParam) == BN_CLICKED) {
                selectedBootModeKey = AppKeys::BootModeExtract;
            }
            break;
        case IDC_INTEGRITY_CHECKBOX:
            if (HIWORD(wParam) == BN_CLICKED) {
                LRESULT check = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0);
                skipIntegrityCheck = (check == BST_UNCHECKED);
            }
            break;
        }
        break;
    case WM_DRAWITEM:
        if (wParam == IDC_CREATE_PARTITION_BUTTON) {
            auto* drawInfo = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            DrawCreateButton(drawInfo);
            return;
        }
        break;
    case WM_UPDATE_PROGRESS:
        SendMessage(progressBar, PBM_SETPOS, wParam, 0);
        break;
    case WM_UPDATE_LOG:
        {
            std::string* logMsg = reinterpret_cast<std::string*>(lParam);
            LogMessage(*logMsg, false);
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
            int wlen = MultiByteToWideChar(CP_UTF8, 0, errorMsg->c_str(), -1, NULL, 0);
            std::wstring wmsg(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, errorMsg->c_str(), -1, &wmsg[0], wlen);
            std::wstring errorTitle = LocalizedOrW("title.error", L"Error");
            MessageBoxW(hWndParent, wmsg.c_str(), errorTitle.c_str(), MB_OK | MB_ICONERROR);
            delete errorMsg;
        }
        break;
    case WM_ASK_RESTART:
        PromptRestart();
        break;
    case WM_RECOVER_COMPLETE:
        {
            bool success = (wParam != 0);
            isRecovering = false;
            EnableWindow(recoverButton, TRUE);
            if (success) {
                LogMessage("Recuperacion de espacio finalizada correctamente.\r\n");
                WCHAR exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                ShellExecuteW(NULL, L"open", exePath, NULL, NULL, SW_SHOWNORMAL);
                PostQuitMessage(0);
            } else {
                LogMessage("Recuperacion de espacio fallida. Revisa los detalles en los registros.\r\n");
                std::wstring recoverErrorTitle = LocalizedOrW("title.error", L"Error");
                std::wstring recoverErrorMessage = LocalizedOrW("message.recoverSpaceFailed", L"Error al recuperar espacio. Revisa los registros para mas detalles.");
                MessageBoxW(hWndParent, recoverErrorMessage.c_str(), recoverErrorTitle.c_str(), MB_OK | MB_ICONERROR);
            }
        }
        break;
    }
}

void MainWindow::DrawCreateButton(LPDRAWITEMSTRUCT drawInfo)
{
    if (!drawInfo) {
        return;
    }

    HDC hdc = drawInfo->hDC;
    const RECT originalRect = drawInfo->rcItem;

    const bool isDisabled = (drawInfo->itemState & ODS_DISABLED) != 0;
    const bool isPressed = (drawInfo->itemState & ODS_SELECTED) != 0;
    const bool isFocused = (drawInfo->itemState & ODS_FOCUS) != 0;

    const int pressOffset = isPressed ? 1 : 0;

    // Paint background.
    FillRect(hdc, &drawInfo->rcItem, GetSysColorBrush(COLOR_BTNFACE));
    RECT borderRect = originalRect;
    DrawEdge(hdc, &borderRect, isPressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);

    RECT innerRect = originalRect;
    InflateRect(&innerRect, -2, 0);
    OffsetRect(&innerRect, pressOffset, pressOffset);

    int iconSize = innerRect.bottom - innerRect.top;
    int maxIconWidth = innerRect.right - innerRect.left - BUTTON_ICON_LEFT_MARGIN;
    if (iconSize > maxIconWidth) {
        iconSize = maxIconWidth;
    }
    if (iconSize < 0) {
        iconSize = 0;
    }

    int iconX = innerRect.left + BUTTON_ICON_LEFT_MARGIN;
    int iconY = innerRect.top;

    if (buttonHIcon && iconSize > 0) {
        DrawIconEx(hdc, iconX, iconY, buttonHIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    RECT textRect = innerRect;
    textRect.left = iconX + iconSize + BUTTON_ICON_TEXT_GAP;
    textRect.right -= BUTTON_ICON_LEFT_MARGIN;

    HFONT buttonFont = reinterpret_cast<HFONT>(SendMessage(drawInfo->hwndItem, WM_GETFONT, 0, 0));
    HFONT oldFont = nullptr;
    if (buttonFont) {
        oldFont = static_cast<HFONT>(SelectObject(hdc, buttonFont));
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(isDisabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));

    DrawTextW(hdc, createButtonText.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_WORDBREAK);

    if (oldFont) {
        SelectObject(hdc, oldFont);
    }

    if (isFocused && !isDisabled) {
        RECT focusRect = innerRect;
        InflateRect(&focusRect, -2, -2);
        DrawFocusRect(hdc, &focusRect);
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
    std::wstring filterDescription = LocalizedOrW("dialog.openIso.filter", L"Archivos ISO (*.iso)");
    std::wstring filter = filterDescription;
    filter.push_back(L'\0');
    filter.append(L"*.iso");
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    ofn.lpstrFilter = filter.c_str();
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
    LogMessage(LocalizedOrUtf8("log.process.starting", "Iniciando proceso...\r\n"));

    WCHAR isoPath[260];
    GetWindowTextW(isoPathEdit, isoPath, sizeof(isoPath) / sizeof(WCHAR));
    if (wcslen(isoPath) == 0)
    {
        std::wstring selectIsoMessage = LocalizedOrW("message.selectIsoFirst", L"Por favor, seleccione un archivo ISO primero.");
        std::wstring isoDialogTitle = LocalizedOrW("title.isoFile", L"Archivo ISO");
        MessageBoxW(NULL, selectIsoMessage.c_str(), isoDialogTitle.c_str(), MB_OK);
        return;
    }

    bool partitionExists = partitionManager->partitionExists();
    if (!partitionExists) {
        SpaceValidationResult validation = partitionManager->validateAvailableSpace();
        if (!validation.isValid) {
            WCHAR errorMsg[256];
            MultiByteToWideChar(CP_UTF8, 0, validation.errorMessage.c_str(), -1, errorMsg, 256);
            std::wstring insufficientTitle = LocalizedOrW("title.spaceInsufficient", L"Espacio Insuficiente");
            MessageBoxW(NULL, errorMsg, insufficientTitle.c_str(), MB_OK);
            return;
        }

        std::wstring confirmStepOneMessage = LocalizedOrW("message.diskModifyConfirmPrimary", L"Esta operacion modificara el disco del sistema, reduciendo su tamano en 10.5 GB para crear dos particiones bootables: una ESP FAT32 de 500MB (ISOEFI) y una particion de datos de 10GB (ISOBOOT). Desea continuar?");
        std::wstring confirmStepOneTitle = LocalizedOrW("title.operationConfirmation", L"Confirmacion de Operacion");
        if (MessageBoxW(NULL, confirmStepOneMessage.c_str(), confirmStepOneTitle.c_str(), MB_YESNO) != IDYES)
            return;

        std::wstring confirmStepTwoMessage = LocalizedOrW("message.diskModifyConfirmSecondary", L"Esta es la segunda confirmacion. La operacion de modificacion del disco es irreversible y puede causar perdida de datos si no se realiza correctamente. Esta completamente seguro de que desea proceder?");
        std::wstring confirmStepTwoTitle = LocalizedOrW("title.secondConfirmation", L"Segunda Confirmacion");
        if (MessageBoxW(NULL, confirmStepTwoMessage.c_str(), confirmStepTwoTitle.c_str(), MB_YESNO) != IDYES)
            return;
    }

    // Disable button and start process
    EnableWindow(createPartitionButton, FALSE);
    isProcessing = true;
    int len = WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, NULL, 0, NULL, NULL);
    std::string isoPathStr(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, isoPath, -1, &isoPathStr[0], len, NULL, NULL);
    std::string bootModeFallback = (selectedBootModeKey == AppKeys::BootModeRam) ? "Boot desde Memoria" : "Boot desde Disco";
    std::string bootModeLabelStr = LocalizedOrUtf8("bootMode." + selectedBootModeKey, bootModeFallback.c_str());
    processController->startProcess(isoPathStr, selectedFormat, selectedBootModeKey, bootModeLabelStr, skipIntegrityCheck);
}

void MainWindow::OnOpenServicesPage()
{
    ShellExecuteW(NULL, L"open", L"https://agsoft.co.cr/en/software-and-services/", NULL, NULL, SW_SHOWNORMAL);
}

void MainWindow::UpdateDiskSpaceInfo()
{
    long long availableGB = partitionManager->getAvailableSpaceGB();
    bool partitionExists = partitionManager->partitionExists();
    bool efiPartitionExists = !partitionManager->getEfiPartitionDriveLetter().empty();

    std::wstring yesText = LocalizedOrW("common.yes", L"Si");
    std::wstring noText = LocalizedOrW("common.no", L"No");
    const std::wstring& existsStr = partitionExists ? yesText : noText;
    const std::wstring& efiExistsStr = efiPartitionExists ? yesText : noText;

    std::wstring availableStr = Utils::utf8_to_wstring(std::to_string(availableGB));
    std::wstring infoText = LocalizedFormatW(
        "mainwindow.diskSpaceInfo",
        { availableStr, existsStr, efiExistsStr },
        L"Espacio disponible en C: {0} GB | ISOBOOT: {1} | ISOEFI: {2}");

    SetWindowTextW(diskSpaceLabel, infoText.c_str());
}

void MainWindow::LogMessage(const std::string& msg, bool persist)
{
    // Get current time
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now);
    std::stringstream timeStream;
    timeStream << std::put_time(&localTime, "[%Y-%m-%d %H:%M:%S] " );

    std::string normalizedMsg = msg;
    auto hasTrailingCRLF = [&normalizedMsg]() -> bool {
        return normalizedMsg.size() >= 2 &&
               normalizedMsg[normalizedMsg.size() - 2] == '\r' &&
               normalizedMsg.back() == '\n';
    };
    if (!hasTrailingCRLF()) {
        while (!normalizedMsg.empty() &&
               (normalizedMsg.back() == '\n' || normalizedMsg.back() == '\r')) {
            normalizedMsg.pop_back();
        }
        normalizedMsg += "\r\n";
    }

    std::string timestampedMsg = timeStream.str() + normalizedMsg;
    if (persist) {
        Logger::instance().append(GENERAL_LOG_FILE, timestampedMsg);
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, timestampedMsg.c_str(), -1, NULL, 0);
    std::wstring wmsg(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, timestampedMsg.c_str(), -1, &wmsg[0], wlen);
    int len = GetWindowTextLengthW(logTextEdit);
    SendMessageW(logTextEdit, EM_SETSEL, len, len);
    SendMessageW(logTextEdit, EM_REPLACESEL, FALSE, (LPARAM)wmsg.c_str());
}

void MainWindow::PromptRestart()
{
    std::wstring restartPrompt = LocalizedOrW("message.processCompleteRestart", L"Proceso terminado. Desea reiniciar el sistema ahora?");
    std::wstring restartTitle = LocalizedOrW("title.restart", L"Reiniciar");
    if (MessageBoxW(hWndParent, restartPrompt.c_str(), restartTitle.c_str(), MB_YESNO) == IDYES) {
        if (!RestartSystem()) {
            std::wstring restartError = LocalizedOrW("message.restartFailed", L"Error al reiniciar el sistema.");
            std::wstring errorTitle = LocalizedOrW("title.error", L"Error");
            MessageBoxW(hWndParent, restartError.c_str(), errorTitle.c_str(), MB_OK);
        }
    }
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
    PostMessage(hWndParent, WM_UPDATE_PROGRESS, static_cast<WPARAM>(progress), 0);
}

void MainWindow::onLogUpdate(const std::string& message) {
    auto* logMsg = new std::string(message);
    PostMessage(hWndParent, WM_UPDATE_LOG, 0, reinterpret_cast<LPARAM>(logMsg));
}

void MainWindow::onButtonEnable() {
    PostMessage(hWndParent, WM_ENABLE_BUTTON, 0, 0);
}

void MainWindow::onDetailedProgress(long long copied, long long total, const std::string& operation) {
    DetailedProgressData* data = new DetailedProgressData{copied, total, operation};
    PostMessage(hWndParent, WM_UPDATE_DETAILED_PROGRESS, 0, (LPARAM)data);
}

void MainWindow::onRecoverComplete(bool success)
{
    PostMessage(hWndParent, WM_RECOVER_COMPLETE, success ? 1 : 0, 0);
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

    std::wstring unitText = LocalizedOrW("unit.mb", L"MB");
    if (totalMB >= 1024) {
        copiedMB /= 1024;
        totalMB /= 1024;
        unitText = LocalizedOrW("unit.gb", L"GB");
    }

    std::wstring operationText = Utils::utf8_to_wstring(operation);

    std::wstringstream ss;
    ss << operationText << L": " << percent << L"% (" << std::fixed << std::setprecision(1)
       << copiedMB << L" " << unitText.c_str() << L" / " << totalMB << L" " << unitText.c_str() << L")";
    SetWindowTextW(detailedProgressLabel, ss.str().c_str());
    SendMessage(detailedProgressBar, PBM_SETPOS, percent, 0);
}



void MainWindow::onAskRestart() {
    PostMessage(hWndParent, WM_ASK_RESTART, 0, 0);
}

void MainWindow::onError(const std::string& message) {
    std::string* msg = new std::string(message);
    PostMessage(hWndParent, WM_UPDATE_ERROR, 0, (LPARAM)msg);
}
























