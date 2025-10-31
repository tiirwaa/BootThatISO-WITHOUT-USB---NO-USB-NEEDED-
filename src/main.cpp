#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include "views/mainwindow.h"
#include "controllers/ProcessController.h"
#include "models/EventManager.h"
#include "services/partitionmanager.h"
#include "services/isocopymanager.h"
#include "services/bcdmanager.h"
#include "utils/Utils.h"
#include "utils/constants.h"

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
    std::string logDir = Utils::getExeDirectory() + "logs";
    std::vector<std::string> logFiles = { GENERAL_LOG_FILE, BCD_CONFIG_LOG_FILE, ISO_EXTRACT_LOG_FILE };
    for (const auto& file : logFiles) {
        std::string path = logDir + "\\" + file;
        std::ofstream ofs(path, std::ios::trunc);
        ofs.close();
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    if (!IsRunAsAdmin())
    {
        MessageBoxW(NULL, L"Este programa requiere privilegios de administrador.", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Clear logs at startup
    ClearLogs();

    // Parse command line arguments
    int argc;
    LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
    bool unattended = false;
    std::string isoPath, mode, format;
    bool chkdsk = false;
    bool autoreboot = false;

    if (argv) {
        for (int i = 0; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"-unattended") {
                unattended = true;
            } else if (arg.find(L"-iso=") == 0) {
                isoPath = Utils::wstring_to_utf8(arg.substr(5));
            } else if (arg.find(L"-mode=") == 0) {
                mode = Utils::wstring_to_utf8(arg.substr(6));
                if (mode == "RAM") mode = "Boot desde Memoria";
                else if (mode == "EXTRACT") mode = "InstalaciÃ³n Completa";
            } else if (arg.find(L"-format=") == 0) {
                format = Utils::wstring_to_utf8(arg.substr(8));
            } else if (arg.find(L"-chkdsk=") == 0) {
                chkdsk = (arg.substr(8) == L"TRUE");
            } else if (arg.find(L"-autoreboot=") == 0) {
                autoreboot = (arg.substr(12) == L"y");
            }
        }
        LocalFree(argv);
    }

    if (unattended) {
        // Run unattended mode
        EventManager eventManager;
        ProcessController processController(eventManager);
        processController.startProcess(isoPath, format, mode, !chkdsk);
        // Wait for completion (simplified, in real app might need better handling)
        // For now, just return
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
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = L"BootThatISOClass";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"BootThatISOClass",
        L"BootThatISO! - ConfiguraciÃ³n de ParticiÃ³n Bootable",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 720,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL)
    {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Center the window
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
    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static MainWindow* mainWindow = nullptr;

    switch (msg)
    {
    case WM_CREATE:
        mainWindow = new MainWindow(hwnd);
        break;
    case WM_COMMAND:
        if (mainWindow)
        {
            mainWindow->HandleCommand(msg, wParam, lParam);
        }
        break;
    case WM_CLOSE:
        if (mainWindow && mainWindow->IsProcessing())
        {
            // Ask user if they want to cancel the running operation
            int res = MessageBoxW(hwnd, L"Un proceso estÃ¡ en ejecuciÃ³n. Â¿Desea cancelar la operaciÃ³n y cerrar la aplicaciÃ³n?", L"Proceso en ejecuciÃ³n", MB_YESNO | MB_ICONQUESTION);
            if (res == IDYES) {
                // Request cancellation and wait for cleanup
                mainWindow->requestCancel();
                // Proceed to close
                if (mainWindow) {
                    delete mainWindow;
                    mainWindow = nullptr;
                }
                PostQuitMessage(0);
                return 0;
            } else {
                // User chose not to cancel; keep running
                return 0;
            }
        } else {
            // No processing running, destroy window to trigger cleanup
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_UPDATE_PROGRESS:
    case WM_UPDATE_LOG:
    case WM_ENABLE_BUTTON:
    case WM_UPDATE_DETAILED_PROGRESS:
    case WM_UPDATE_ERROR:
    case WM_RECOVER_COMPLETE:
        if (mainWindow)
        {
            mainWindow->HandleCommand(msg, wParam, lParam);
        }
        break;
    case WM_DESTROY:
        if (mainWindow)
        {
            delete mainWindow;
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}


