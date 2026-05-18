
#include "viewer.h"
#include <commctrl.h>
#include <stdio.h>

template<typename T>
T* GetAppFromDialog(HWND hDlg, UINT message, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        return reinterpret_cast<T*>(lParam);
    }
    return reinterpret_cast<T*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
}

INT_PTR CALLBACK ViewerApp::PreferencesDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    ViewerApp* pApp = nullptr;
    if (message == WM_INITDIALOG) {
        pApp = reinterpret_cast<ViewerApp*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pApp);
    }
    else {
        pApp = reinterpret_cast<ViewerApp*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
    }

    if (!pApp) return (INT_PTR)FALSE;

    auto& ctx = pApp->GetContext();

    switch (message) {
    case WM_INITDIALOG: {
        pApp->UpdateTitleBarTheme(hDlg, ctx.bgColor);
        if (ctx.bgColor == BackgroundColor::Black || ctx.bgColor == BackgroundColor::Grey) {
            if (!ctx.darkBrush) ctx.darkBrush = CreateSolidBrush(RGB(32, 32, 32));
        }
        else {
            if (ctx.darkBrush) {
                DeleteObject(ctx.darkBrush);
                ctx.darkBrush = nullptr;
            }
        }

        int bgRadio = IDC_RADIO_BG_GREY + static_cast<int>(ctx.bgColor);
        CheckRadioButton(hDlg, IDC_RADIO_BG_GREY, IDC_RADIO_BG_TRANSPARENT, bgRadio);

        CheckDlgButton(hDlg, IDC_CHECK_ALWAYS_ON_TOP, ctx.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_START_FULLSCREEN, ctx.startFullScreen ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_SINGLE_INSTANCE, ctx.enforceSingleInstance ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_AUTO_REFRESH, ctx.isAutoRefresh ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_SMOOTH_SCALING, ctx.smoothScaling ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_FADE_ANIMATION, ctx.enableFadeAnimation ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(hDlg, IDC_RADIO_ZOOM_FIT, IDC_RADIO_ZOOM_ACTUAL,
            ctx.defaultZoomMode == DefaultZoomMode::Fit ? IDC_RADIO_ZOOM_FIT : IDC_RADIO_ZOOM_ACTUAL);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            for (int id = IDC_RADIO_BG_GREY; id <= IDC_RADIO_BG_TRANSPARENT; ++id) {
                if (IsDlgButtonChecked(hDlg, id)) ctx.bgColor = static_cast<BackgroundColor>(id - IDC_RADIO_BG_GREY);
            }

            ctx.alwaysOnTop = (IsDlgButtonChecked(hDlg, IDC_CHECK_ALWAYS_ON_TOP) == BST_CHECKED);
            ctx.startFullScreen = (IsDlgButtonChecked(hDlg, IDC_CHECK_START_FULLSCREEN) == BST_CHECKED);
            ctx.enforceSingleInstance = (IsDlgButtonChecked(hDlg, IDC_CHECK_SINGLE_INSTANCE) == BST_CHECKED);

            bool newAutoRefresh = (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTO_REFRESH) == BST_CHECKED);
            if (newAutoRefresh != ctx.isAutoRefresh) {
                ctx.isAutoRefresh = newAutoRefresh;
                if (ctx.isAutoRefresh) {
                    SetTimer(ctx.hWnd, AUTO_REFRESH_TIMER_ID, 1000, nullptr);
                }
                else {
                    KillTimer(ctx.hWnd, AUTO_REFRESH_TIMER_ID);
                }
            }

            bool newSmoothScaling = (IsDlgButtonChecked(hDlg, IDC_CHECK_SMOOTH_SCALING) == BST_CHECKED);
            if (newSmoothScaling != ctx.smoothScaling) {
                ctx.smoothScaling = newSmoothScaling;
                pApp->TriggerHqRender();
            }

            ctx.enableFadeAnimation = (IsDlgButtonChecked(hDlg, IDC_CHECK_FADE_ANIMATION) == BST_CHECKED);
            if (IsDlgButtonChecked(hDlg, IDC_RADIO_ZOOM_FIT)) ctx.defaultZoomMode = DefaultZoomMode::Fit;
            else if (IsDlgButtonChecked(hDlg, IDC_RADIO_ZOOM_ACTUAL)) ctx.defaultZoomMode = DefaultZoomMode::Actual;
            SetWindowPos(ctx.hWnd, (ctx.alwaysOnTop) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            pApp->UpdateTitleBarTheme(ctx.hWnd, ctx.bgColor);
            InvalidateRect(ctx.hWnd, NULL, FALSE);

            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    case WM_DESTROY:
        break;
    }
    return (INT_PTR)FALSE;
}

void ViewerApp::OpenPreferencesDialog() {
    DialogBoxParam(m_ctx.hInst, MAKEINTRESOURCE(IDD_PREFERENCES_DIALOG), m_ctx.hWnd, PreferencesDialogProc, (LPARAM)this);
}

std::wstring ViewerApp::GetHotkeyString(WORD hk) {
    if (!hk) return L"";
    std::wstring str;
    BYTE mods = HIBYTE(hk);
    BYTE vk = LOBYTE(hk);
    if (mods & HOTKEYF_CONTROL) str += L"Ctrl+";
    if (mods & HOTKEYF_SHIFT) str += L"Shift+";
    if (mods & HOTKEYF_ALT) str += L"Alt+";

    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t keyName[64] = { 0 };
    switch (vk) {
    case VK_LEFT: wcscpy_s(keyName, L"Left Arrow"); break;
    case VK_RIGHT: wcscpy_s(keyName, L"Right Arrow"); break;
    case VK_UP: wcscpy_s(keyName, L"Up Arrow"); break;
    case VK_DOWN: wcscpy_s(keyName, L"Down Arrow"); break;
    case VK_ESCAPE: wcscpy_s(keyName, L"Esc"); break;
    case VK_ADD: wcscpy_s(keyName, L"+"); break;
    case VK_SUBTRACT: wcscpy_s(keyName, L"-"); break;
    case VK_MULTIPLY: wcscpy_s(keyName, L"*"); break;
    default: GetKeyNameTextW(scanCode << 16, keyName, 64); break;
    }
    str += keyName;
    return str;
}

static const wchar_t* ActionNames[] = {
    L"Next Image", L"Previous Image", L"Zoom In", L"Zoom Out", L"Fit to Window", L"Actual Size",
    L"Fullscreen", L"Rotate Clockwise", L"Rotate Counter-Clockwise", L"Flip", L"Crop", L"Custom Zoom", L"Exit"
};

INT_PTR CALLBACK ViewerApp::KeybindingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    ViewerApp* pApp = GetAppFromDialog<ViewerApp>(hDlg, message, lParam);
    if (!pApp) return (INT_PTR)FALSE;

    auto& ctx = pApp->GetContext();

    switch (message) {
    case WM_INITDIALOG: {
        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ACTION);
        for (int i = 0; i < Act_Count; ++i) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)ActionNames[i]);
        }
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_CTRL), HKM_SETHOTKEY, ctx.hotkeys[0], 0);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_COMBO_ACTION && HIWORD(wParam) == CBN_SELCHANGE) {
            int idx = static_cast<int>(SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0));
            if (idx != CB_ERR) {
                SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_CTRL), HKM_SETHOTKEY, ctx.hotkeys[idx], 0);
            }
            return (INT_PTR)TRUE;
        }
        switch (LOWORD(wParam)) {
        case IDOK: {
            int idx = static_cast<int>(SendMessageW(GetDlgItem(hDlg, IDC_COMBO_ACTION), CB_GETCURSEL, 0, 0));
            if (idx != CB_ERR) {
                ctx.hotkeys[idx] = static_cast<WORD>(SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_CTRL), HKM_GETHOTKEY, 0, 0));
                SetDlgItemTextW(hDlg, IDOK, L"Applied!");
                SetTimer(hDlg, KEYBINDING_TIMER_ID, 1500, nullptr);
            }
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    case WM_TIMER:
        if (wParam == KEYBINDING_TIMER_ID) {
            KillTimer(hDlg, KEYBINDING_TIMER_ID);
            SetDlgItemTextW(hDlg, IDOK, L"Apply");
        }
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

void ViewerApp::OpenKeybindingsDialog() {
    DialogBoxParam(m_ctx.hInst, MAKEINTRESOURCE(IDD_KEYBINDINGS_DIALOG), m_ctx.hWnd, KeybindingsDialogProc, (LPARAM)this);
}

INT_PTR CALLBACK ViewerApp::ZoomDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    ViewerApp* pApp = GetAppFromDialog<ViewerApp>(hDlg, message, lParam);
    if (!pApp) return (INT_PTR)FALSE;

    if (message == WM_INITDIALOG) {
        // Default the text box to the current zoom level
        SetDlgItemInt(hDlg, IDC_EDIT_ZOOM, static_cast<UINT>(pApp->GetContext().zoomFactor * 100.0f + 0.5f), FALSE);
        return (INT_PTR)TRUE;
    }

    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            BOOL success = FALSE;
            UINT val = GetDlgItemInt(hDlg, IDC_EDIT_ZOOM, &success, FALSE);
            if (success && val > 0) {
                pApp->SetZoomLevel(val / 100.0f);
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBoxW(hDlg, L"Please enter a valid positive percentage.", L"Invalid Input", MB_ICONERROR);
            }
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void ViewerApp::OpenZoomDialog() {
    DialogBoxParam(m_ctx.hInst, MAKEINTRESOURCE(IDD_ZOOM_DIALOG), m_ctx.hWnd, ZoomDialogProc, (LPARAM)this);
}