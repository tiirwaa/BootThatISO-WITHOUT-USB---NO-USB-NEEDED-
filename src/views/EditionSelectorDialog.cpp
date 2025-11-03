#include "EditionSelectorDialog.h"
#include "../resource.h"
#include "../utils/LocalizationManager.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

std::wstring EditionSelectorDialog::utf8ToWide(const std::string &str) {
    if (str.empty())
        return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (size <= 0)
        return std::wstring();
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::wstring EditionSelectorDialog::formatSize(long long bytes) {
    const double GB = 1024.0 * 1024.0 * 1024.0;
    const double MB = 1024.0 * 1024.0;

    std::wostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= GB) {
        oss << (bytes / GB) << L" GB";
    } else if (bytes >= MB) {
        oss << (bytes / MB) << L" MB";
    } else if (bytes > 0) {
        oss << bytes << L" bytes";
    } else {
        // Use localized string for "Size unknown"
        return LocalizationManager::getInstance().getWString("editionSelector.sizeUnknown");
    }

    return oss.str();
}

void EditionSelectorDialog::initListView(HWND                                                       hListView,
                                         const std::vector<WindowsEditionSelector::WindowsEdition> &editions) {
    // Set extended styles (removed LVS_EX_CHECKBOXES to use radio button behavior)
    ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Get localized column names
    std::wstring colIndex = LocalizationManager::getInstance().getWString("editionSelector.columnIndex");
    std::wstring colName  = LocalizationManager::getInstance().getWString("editionSelector.columnName");
    std::wstring colSize  = LocalizationManager::getInstance().getWString("editionSelector.columnSize");

    // Add columns
    LVCOLUMNW col = {};
    col.mask      = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt       = LVCFMT_LEFT;

    col.cx      = 40;
    col.pszText = const_cast<LPWSTR>(colIndex.c_str());
    ListView_InsertColumn(hListView, 0, &col);

    col.cx      = 250;
    col.pszText = const_cast<LPWSTR>(colName.c_str());
    ListView_InsertColumn(hListView, 1, &col);

    col.cx      = 100;
    col.pszText = const_cast<LPWSTR>(colSize.c_str());
    ListView_InsertColumn(hListView, 2, &col);

    // Add items
    for (size_t i = 0; i < editions.size(); ++i) {
        const auto &ed = editions[i];

        LVITEMW item        = {};
        item.mask           = LVIF_TEXT | LVIF_PARAM;
        item.iItem          = static_cast<int>(i);
        item.iSubItem       = 0;
        item.lParam         = static_cast<LPARAM>(ed.index);
        std::wstring idxStr = std::to_wstring(ed.index);
        item.pszText        = const_cast<LPWSTR>(idxStr.c_str());
        ListView_InsertItem(hListView, &item);

        // Name
        std::wstring name = utf8ToWide(ed.name);
        ListView_SetItemText(hListView, static_cast<int>(i), 1, const_cast<LPWSTR>(name.c_str()));

        // Size
        std::wstring size = formatSize(ed.size);
        ListView_SetItemText(hListView, static_cast<int>(i), 2, const_cast<LPWSTR>(size.c_str()));
    }
}

void EditionSelectorDialog::initDialog(HWND hDlg, DialogData *data) {
    // Set localized title and texts
    if (data && data->locManager) {
        std::wstring title    = data->locManager->getWString("editionSelector.title");
        std::wstring header   = data->locManager->getWString("editionSelector.header");
        std::wstring subtitle = data->locManager->getWString("editionSelector.subtitle");

        SetWindowTextW(hDlg, title.c_str());
        SetDlgItemTextW(hDlg, IDC_EDITION_TITLE, header.c_str());
        SetDlgItemTextW(hDlg, IDC_EDITION_SUBTITLE, subtitle.c_str());

        // Set label texts
        std::wstring infoLabel = data->locManager->getWString("editionSelector.infoLabel");
        std::wstring sizeLabel = data->locManager->getWString("editionSelector.sizeLabel");
        SetDlgItemTextW(hDlg, IDC_EDITION_INFO_LABEL, infoLabel.c_str());
        SetDlgItemTextW(hDlg, IDC_EDITION_SIZE_TEXT, sizeLabel.c_str());

        // Set button texts
        std::wstring btnContinue = data->locManager->getWString("editionSelector.buttonContinue");
        std::wstring btnCancel   = data->locManager->getWString("editionSelector.buttonCancel");
        SetDlgItemTextW(hDlg, IDOK_EDITION, btnContinue.c_str());
        SetDlgItemTextW(hDlg, IDCANCEL_EDITION, btnCancel.c_str());
    }

    // Center dialog relative to parent window
    RECT rcDlg, rcParent;
    GetWindowRect(hDlg, &rcDlg);
    int dlgWidth  = rcDlg.right - rcDlg.left;
    int dlgHeight = rcDlg.bottom - rcDlg.top;

    HWND hParent = GetParent(hDlg);
    if (hParent && GetWindowRect(hParent, &rcParent)) {
        // Center relative to parent window
        int parentWidth  = rcParent.right - rcParent.left;
        int parentHeight = rcParent.bottom - rcParent.top;
        int x            = rcParent.left + (parentWidth - dlgWidth) / 2;
        int y            = rcParent.top + (parentHeight - dlgHeight) / 2;

        // Ensure dialog is visible on screen
        int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x + dlgWidth > screenWidth)
            x = screenWidth - dlgWidth;
        if (y + dlgHeight > screenHeight)
            y = screenHeight - dlgHeight;

        SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    } else {
        // Fallback: center on screen
        int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x            = (screenWidth - dlgWidth) / 2;
        int y            = (screenHeight - dlgHeight) / 2;
        SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    }

    // Get control handles
    data->hListView  = GetDlgItem(hDlg, IDC_EDITION_LIST);
    data->hSizeLabel = GetDlgItem(hDlg, IDC_EDITION_SIZE_LABEL);
    data->hInfoEdit  = GetDlgItem(hDlg, IDC_EDITION_INFO);
    data->totalSize  = 0;

    // Initialize list view
    if (data->editions && !data->editions->empty()) {
        initListView(data->hListView, *data->editions);

        // Auto-select recommended edition (Pro or Home)
        int selectedIndex = -1;
        for (size_t i = 0; i < data->editions->size(); ++i) {
            const auto &ed        = (*data->editions)[i];
            std::string nameLower = ed.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

            if (nameLower.find("pro") != std::string::npos || nameLower.find("home") != std::string::npos) {
                selectedIndex = static_cast<int>(i);
                break;
            }
        }

        // If no Pro/Home found, select first
        if (selectedIndex == -1) {
            selectedIndex = 0;
        }

        // Apply selection
        ListView_SetItemState(data->hListView, selectedIndex, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(data->hListView, selectedIndex, FALSE);
    }

    // Load logo
    HMODULE hModule  = GetModuleHandle(NULL);
    data->logoBitmap = LoadBitmap(hModule, MAKEINTRESOURCE(IDR_AG_LOGO));
    if (data->logoBitmap) {
        SendDlgItemMessageW(hDlg, IDC_EDITION_LOGO, STM_SETIMAGE, IMAGE_BITMAP,
                            reinterpret_cast<LPARAM>(data->logoBitmap));
    }

    updateTotalSize(data);
    updateInfo(data);
}

void EditionSelectorDialog::updateTotalSize(DialogData *data) {
    if (!data || !data->editions || !data->hListView)
        return;

    // Get selected item (radio button behavior - only one selection)
    int selectedItem = ListView_GetNextItem(data->hListView, -1, LVNI_SELECTED);
    if (selectedItem == -1) {
        data->totalSize       = 0;
        std::wstring sizeText = formatSize(0);
        SetWindowTextW(data->hSizeLabel, sizeText.c_str());
        return;
    }

    LVITEMW item = {};
    item.mask    = LVIF_PARAM;
    item.iItem   = selectedItem;
    ListView_GetItem(data->hListView, &item);

    int editionIndex = static_cast<int>(item.lParam);
    for (const auto &ed : *data->editions) {
        if (ed.index == editionIndex) {
            data->totalSize       = ed.size;
            std::wstring sizeText = formatSize(ed.size);
            SetWindowTextW(data->hSizeLabel, sizeText.c_str());
            break;
        }
    }
}

void EditionSelectorDialog::updateInfo(DialogData *data) {
    if (!data || !data->editions || !data->hListView)
        return;

    int selectedItem = ListView_GetNextItem(data->hListView, -1, LVNI_SELECTED);
    if (selectedItem == -1) {
        // Use localized string
        std::wstring infoText = LocalizationManager::getInstance().getWString("editionSelector.infoSelect");
        SetWindowTextW(data->hInfoEdit, infoText.c_str());
        return;
    }

    LVITEMW item = {};
    item.mask    = LVIF_PARAM;
    item.iItem   = selectedItem;
    ListView_GetItem(data->hListView, &item);

    int editionIndex = static_cast<int>(item.lParam);
    for (const auto &ed : *data->editions) {
        if (ed.index == editionIndex) {
            std::wstring indexLabel = LocalizationManager::getInstance().getWString("editionSelector.infoIndex");
            std::wstring info       = indexLabel + L" " + std::to_wstring(ed.index) + L" | ";
            info += utf8ToWide(ed.name);
            if (!ed.description.empty()) {
                info += L" | " + utf8ToWide(ed.description);
            }
            SetWindowTextW(data->hInfoEdit, info.c_str());
            break;
        }
    }
}

INT_PTR CALLBACK EditionSelectorDialog::dialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        auto *data = reinterpret_cast<DialogData *>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
        initDialog(hDlg, data);
        return TRUE;
    }

    case WM_NOTIFY: {
        auto   *data  = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
        LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lParam);

        if (nmhdr->idFrom == IDC_EDITION_LIST) {
            if (nmhdr->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW pnmv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if (pnmv->uChanged & LVIF_STATE) {
                    // Selection changed (radio button behavior)
                    if ((pnmv->uOldState ^ pnmv->uNewState) & LVIS_SELECTED) {
                        updateTotalSize(data);
                        updateInfo(data);
                    }
                }
            }
        }
        break;
    }

    case WM_COMMAND: {
        auto *data = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

        switch (LOWORD(wParam)) {
        case IDOK_EDITION: {
            if (data && data->editions && data->hListView) {
                data->selectedIndices.clear();

                // Get the selected item (radio button behavior - only one)
                int selectedItem = ListView_GetNextItem(data->hListView, -1, LVNI_SELECTED);

                if (selectedItem == -1) {
                    std::wstring warningMsg =
                        LocalizationManager::getInstance().getWString("editionSelector.warningNoSelection");
                    std::wstring warningTitle =
                        LocalizationManager::getInstance().getWString("editionSelector.warningTitle");
                    MessageBoxW(hDlg, warningMsg.c_str(), warningTitle.c_str(), MB_OK | MB_ICONWARNING);
                    return TRUE;
                }

                LVITEMW item = {};
                item.mask    = LVIF_PARAM;
                item.iItem   = selectedItem;
                ListView_GetItem(data->hListView, &item);
                data->selectedIndices.push_back(static_cast<int>(item.lParam));

                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        }

        case IDCANCEL_EDITION:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        default:
            break;
        }
        break;
    }

    case WM_DESTROY: {
        auto *data = reinterpret_cast<DialogData *>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
        if (data && data->logoBitmap) {
            DeleteObject(data->logoBitmap);
            data->logoBitmap = nullptr;
        }
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
        return TRUE;
    }

    default:
        break;
    }

    return FALSE;
}

bool EditionSelectorDialog::show(HINSTANCE hInstance, HWND parent,
                                 const std::vector<WindowsEditionSelector::WindowsEdition> &editions,
                                 std::vector<int> &selectedIndices, LocalizationManager *localizationManager) {
    if (editions.empty()) {
        return false;
    }

    DialogData data{};
    data.editions   = &editions;
    data.logoBitmap = nullptr;
    data.totalSize  = 0;
    data.locManager = localizationManager;

    INT_PTR result = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_EDITION_DIALOG), parent, dialogProc,
                                     reinterpret_cast<LPARAM>(&data));

    if (result == IDOK) {
        selectedIndices = data.selectedIndices;
        return !selectedIndices.empty();
    }

    return false;
}
