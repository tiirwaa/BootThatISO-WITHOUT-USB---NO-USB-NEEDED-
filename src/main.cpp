#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <memory>
#include <cwctype>
#include "views/mainwindow.h"
#include "resource.h"
#include "controllers/ProcessController.h"
#include "models/EventManager.h"
#include "services/partitionmanager.h"
#include "services/isocopymanager.h"
#include "services/bcdmanager.h"
#include "utils/Utils.h"
#include "utils/constants.h"
#include "utils/LocalizationManager.h"
#include "utils/LocalizationHelpers.h"
#include "utils/AppKeys.h"
#include "utils/Logger.h"

BOOL IsRunAsAdmin()
{
    BOOL bElevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TOKEN_ELEVATION elevation;
        DWORD dwSize;
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize))
        {
            bElevated = elevation.TokenIsElevated;
        }
    }
    if (hToken)
    {
        CloseHandle(hToken);
    }
    return bElevated;
}

void ClearLogs()
{
    Logger::instance().resetProcessLogs();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    LocalizationManager& localization = LocalizationManager::getInstance();
    std::wstring exeDir = Utils::utf8_to_wstring(Utils::getExeDirectory());
    std::wstring langDir = exeDir + L"lang";
    if (!localization.initialize() || !localization.hasLanguages()) {
        std::wstring noLangMessage = LocalizedOrW("message.noLanguagesFound", L"No language files were found in the 'lang' directory.");
        std::wstring errorTitle = LocalizedOrW("title.error", L"Error");
        MessageBoxW(NULL, noLangMessage.c_str(), errorTitle.c_str(), MB_ICONERROR | MB_OK);
        return 1;
    }
    localization.loadLanguageByIndex(0);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    bool unattended = false;
    std::string isoPath;
    std::string modeKey;
    std::string format;
    bool chkdsk = false;
    bool autoreboot = false;
    std::wstring languageCodeArg;

    if (argv) {
        for (int i = 0; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"-unattended") {
                unattended = true;
            } else if (arg.rfind(L"-iso=", 0) == 0) {
                isoPath = Utils::wstring_to_utf8(arg.substr(5));
            } else if (arg.rfind(L"-mode=", 0) == 0) {
                std::wstring value = arg.substr(6);
                std::wstring lowerValue = value;
                std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), ::towlower);
                std::string modeCandidate = Utils::wstring_to_utf8(lowerValue);
                if (modeCandidate == "ram") {
                    modeKey = AppKeys::BootModeRam;
                } else if (modeCandidate == "extract" || modeCandidate == "extracted") {
                    modeKey = AppKeys::BootModeExtract;
                } else {
                    modeKey = modeCandidate;
                }
            } else if (arg.rfind(L"-format=", 0) == 0) {
                format = Utils::wstring_to_utf8(arg.substr(8));
            } else if (arg.rfind(L"-chkdsk=", 0) == 0) {
                std::wstring value = arg.substr(8);
                std::transform(value.begin(), value.end(), value.begin(), ::towlower);
                chkdsk = (value == L"true" || value == L"1" || value == L"s" || value == L"y");
            } else if (arg.rfind(L"-autoreboot=", 0) == 0) {
                std::wstring value = arg.substr(12);
                std::transform(value.begin(), value.end(), value.begin(), ::towlower);
                autoreboot = (value == L"true" || value == L"1" || value == L"s" || value == L"y");
            } else if (arg.rfind(L"-lang=", 0) == 0) {
                languageCodeArg = arg.substr(6);
            }
        }
        LocalFree(argv);
    }

    if (!languageCodeArg.empty()) {
        if (!localization.loadLanguageByCode(languageCodeArg)) {
            std::wstring languageError = LocalizedOrW("message.languageLoadFailed", L"The requested language could not be loaded.");
        std::wstring errorTitle = LocalizedOrW("title.error", L"Error");
        MessageBoxW(NULL, languageError.c_str(), errorTitle.c_str(), MB_ICONWARNING | MB_OK);
        }
    }

    if (!unattended) {
        if (!localization.promptForLanguageSelection(hInstance)) {
            return 0;
        }
    }

    if (!IsRunAsAdmin())
    {
        std::wstring message = LocalizedOrW("message.adminRequired", L"Este programa requiere privilegios de administrador.");
        std::wstring title = LocalizedOrW("title.error", L"Error");
        MessageBoxW(NULL, message.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
        return 1;
    }

    ClearLogs();

    if (unattended) {
        std::string debugLogPath = Utils::getExeDirectory() + "logs\\unattended_debug.log";
        std::ofstream debugLog(debugLogPath.c_str());
        debugLog << "Unattended mode started\n";
        debugLog << "isoPath: " << isoPath << "\n";
        debugLog << "format: " << format << "\n";
        debugLog << "mode: " << modeKey << "\n";
        debugLog << "chkdsk: " << chkdsk << "\n";
        debugLog << "autoreboot: " << autoreboot << "\n";
        debugLog.close();

        EventManager eventManager;
        ProcessController processController(eventManager);
        std::string unattendedFallback = modeKey.empty() ? "" : (modeKey == AppKeys::BootModeRam ? "Boot desde Memoria" : "Boot desde Disco");
        std::string unattendedLabel = LocalizedOrUtf8("bootMode." + modeKey, unattendedFallback.empty() ? modeKey.c_str() : unattendedFallback.c_str());
        processController.startProcess(isoPath, format, modeKey, unattendedLabel, !chkdsk, true);
        return 0;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    WNDCLASSEX wc;
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    HICON appIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (!appIcon) {
        appIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    wc.hIcon = appIcon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = L"BootThatISOClass";
    HICON appIconSmall = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, 0));
    if (!appIconSmall) {
        appIconSmall = LoadIcon(NULL, IDI_APPLICATION);
    }
    wc.hIconSm = appIconSmall;

    if (!RegisterClassExW(&wc))
    {
        std::wstring message = LocalizedOrW("message.windowRegistrationFailed", L"Window Registration Failed!");
        std::wstring title = LocalizedOrW("title.error", L"Error");
        MessageBoxW(NULL, message.c_str(), title.c_str(), MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    std::wstring windowTitle = LocalizedOrW("app.windowTitle", L"BootThatISO! - Bootear ISO sin una USB");
    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"BootThatISOClass",
        windowTitle.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 720,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL)
    {
        std::wstring message = LocalizedOrW("message.windowCreationFailed", L"Window Creation Failed!");
        std::wstring title = LocalizedOrW("title.error", L"Error");
        MessageBoxW(NULL, message.c_str(), title.c_str(), MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    RECT rect;
    GetWindowRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - width) / 2;
    int y = (screenHeight - height) / 2;
    MoveWindow(hwnd, x, y, width, height, FALSE);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static std::unique_ptr<MainWindow> mainWindow;

    switch (msg)
    {
    case WM_CREATE:
        mainWindow = std::make_unique<MainWindow>(hwnd);
        return 0;
    case WM_COMMAND:
        if (mainWindow)
        {
            mainWindow->HandleCommand(msg, wParam, lParam);
        }
        return 0;
    case WM_DRAWITEM:
        if (mainWindow)
        {
            mainWindow->HandleCommand(msg, wParam, lParam);
        }
        return TRUE;
    case WM_CLOSE:
        if (mainWindow && mainWindow->IsProcessing())
        {
            const std::wstring prompt = LocalizedOrW("message.operationInProgress", L"Un proceso esta en ejecucion. Desea cancelar la operacion y cerrar la aplicacion?");
            const std::wstring title = LocalizedOrW("title.operationInProgress", L"Proceso en ejecucion");
            const int result = MessageBoxW(hwnd, prompt.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
            if (result == IDYES) {
                mainWindow->requestCancel();
                DestroyWindow(hwnd);
            }
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_UPDATE_PROGRESS:
    case WM_UPDATE_LOG:
    case WM_ENABLE_BUTTON:
    case WM_UPDATE_DETAILED_PROGRESS:
    case WM_UPDATE_ERROR:
    case WM_ASK_RESTART:
    case WM_RECOVER_COMPLETE:
        if (mainWindow)
        {
            mainWindow->HandleCommand(msg, wParam, lParam);
        }
        return 0;
    case WM_DESTROY:
        mainWindow.reset();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}







