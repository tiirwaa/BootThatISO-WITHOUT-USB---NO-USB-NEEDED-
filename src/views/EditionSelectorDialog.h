#pragma once
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include "../wim/WindowsEditionSelector.h"

// Forward declaration
class LocalizationManager;

/**
 * @brief Dialog for selecting Windows editions to inject into boot.wim
 *
 * This dialog presents available Windows editions to the user and allows:
 * - Single or multiple edition selection
 * - Viewing edition information (name, description, size)
 * - Total size estimation
 */
class EditionSelectorDialog {
public:
    /**
     * @brief Shows the edition selection dialog
     * @param hInstance Application instance handle
     * @param parent Parent window handle
     * @param editions List of available Windows editions
     * @param selectedIndices Output vector of selected edition indices (1-based)
     * @param localizationManager Localization manager for translations
     * @return true if user confirmed selection, false if cancelled
     */
    static bool show(HINSTANCE hInstance, HWND parent,
                     const std::vector<WindowsEditionSelector::WindowsEdition> &editions,
                     std::vector<int> &selectedIndices, LocalizationManager *localizationManager = nullptr);

private:
    struct DialogData {
        const std::vector<WindowsEditionSelector::WindowsEdition> *editions;
        std::vector<int>                                           selectedIndices;
        HBITMAP                                                    logoBitmap;
        HWND                                                       hListView;
        HWND                                                       hSizeLabel;
        HWND                                                       hInfoEdit;
        long long                                                  totalSize;
        LocalizationManager                                       *locManager;
    };

    /**
     * @brief Dialog procedure
     */
    static INT_PTR CALLBACK dialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    /**
     * @brief Initializes the dialog controls
     */
    static void initDialog(HWND hDlg, DialogData *data);

    /**
     * @brief Initializes the list view control
     */
    static void initListView(HWND hListView, const std::vector<WindowsEditionSelector::WindowsEdition> &editions);

    /**
     * @brief Updates total size label
     */
    static void updateTotalSize(DialogData *data);

    /**
     * @brief Updates info edit with selected edition details
     */
    static void updateInfo(DialogData *data);

    /**
     * @brief Formats bytes to human-readable string (GB/MB)
     */
    static std::wstring formatSize(long long bytes);

    /**
     * @brief Converts UTF-8 string to wide string
     */
    static std::wstring utf8ToWide(const std::string &str);
};
