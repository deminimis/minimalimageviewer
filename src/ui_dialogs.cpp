
#include "viewer.h"
#include <commctrl.h>
#include <stdio.h>

extern AppContext g_ctx;

static INT_PTR CALLBACK PreferencesDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hDarkBrush = nullptr;
    switch (message) {
    case WM_INITDIALOG: {
        UpdateTitleBarTheme(hDlg, g_ctx.bgColor);
        if (g_ctx.bgColor == BackgroundColor::Black || g_ctx.bgColor == BackgroundColor::Grey) {
            if (!hDarkBrush) hDarkBrush = CreateSolidBrush(RGB(32, 32, 32));
        }
        else {
            if (hDarkBrush) { DeleteObject(hDarkBrush); hDarkBrush = nullptr; }
        }

        int bgRadio = IDC_RADIO_BG_GREY + static_cast<int>(g_ctx.bgColor);
        CheckRadioButton(hDlg, IDC_RADIO_BG_GREY, IDC_RADIO_BG_TRANSPARENT, bgRadio);

        CheckDlgButton(hDlg, IDC_CHECK_ALWAYS_ON_TOP, g_ctx.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_START_FULLSCREEN, g_ctx.startFullScreen ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_SINGLE_INSTANCE, g_ctx.enforceSingleInstance ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_AUTO_REFRESH, g_ctx.isAutoRefresh ? BST_CHECKED : BST_UNCHECKED);

        CheckRadioButton(hDlg, IDC_RADIO_ZOOM_FIT, IDC_RADIO_ZOOM_ACTUAL,
            g_ctx.defaultZoomMode == DefaultZoomMode::Fit ? IDC_RADIO_ZOOM_FIT : IDC_RADIO_ZOOM_ACTUAL);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            for (int id = IDC_RADIO_BG_GREY; id <= IDC_RADIO_BG_TRANSPARENT; ++id) {
                if (IsDlgButtonChecked(hDlg, id)) g_ctx.bgColor = static_cast<BackgroundColor>(id - IDC_RADIO_BG_GREY);
            }

            g_ctx.alwaysOnTop = (IsDlgButtonChecked(hDlg, IDC_CHECK_ALWAYS_ON_TOP) == BST_CHECKED);
            g_ctx.startFullScreen = (IsDlgButtonChecked(hDlg, IDC_CHECK_START_FULLSCREEN) == BST_CHECKED);
            g_ctx.enforceSingleInstance = (IsDlgButtonChecked(hDlg, IDC_CHECK_SINGLE_INSTANCE) == BST_CHECKED);

            bool newAutoRefresh = (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTO_REFRESH) == BST_CHECKED);
            if (newAutoRefresh != g_ctx.isAutoRefresh) {
                g_ctx.isAutoRefresh = newAutoRefresh;
                if (g_ctx.isAutoRefresh) {
                    SetTimer(g_ctx.hWnd, AUTO_REFRESH_TIMER_ID, 1000, nullptr);
                }
                else {
                    KillTimer(g_ctx.hWnd, AUTO_REFRESH_TIMER_ID);
                }
            }

            if (IsDlgButtonChecked(hDlg, IDC_RADIO_ZOOM_FIT)) g_ctx.defaultZoomMode = DefaultZoomMode::Fit;
            else if (IsDlgButtonChecked(hDlg, IDC_RADIO_ZOOM_ACTUAL)) g_ctx.defaultZoomMode = DefaultZoomMode::Actual;

            SetWindowPos(g_ctx.hWnd, (g_ctx.alwaysOnTop) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            UpdateTitleBarTheme(g_ctx.hWnd, g_ctx.bgColor);
            InvalidateRect(g_ctx.hWnd, NULL, FALSE);

            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        if (g_ctx.bgColor == BackgroundColor::Black || g_ctx.bgColor == BackgroundColor::Grey) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(32, 32, 32));
            return (INT_PTR)hDarkBrush;
        }
        break;
    }
    case WM_DESTROY:
        if (hDarkBrush) { DeleteObject(hDarkBrush); hDarkBrush = nullptr; }
        break;
    }
    return (INT_PTR)FALSE;
}

void OpenPreferencesDialog() {
    DialogBoxParam(g_ctx.hInst, MAKEINTRESOURCE(IDD_PREFERENCES_DIALOG), g_ctx.hWnd, PreferencesDialogProc, 0);
}

static void UpdateEffectLabels(HWND hDlg) {
    wchar_t buf[64];

    swprintf_s(buf, L"Brightness: %d%%", static_cast<int>(g_ctx.brightness * 100));
    SetDlgItemTextW(hDlg, IDC_LABEL_BRIGHTNESS, buf);

    swprintf_s(buf, L"Contrast: %d%%", static_cast<int>(g_ctx.contrast * 100));
    SetDlgItemTextW(hDlg, IDC_LABEL_CONTRAST, buf);

    swprintf_s(buf, L"Saturation: %d%%", static_cast<int>(g_ctx.saturation * 100));
    SetDlgItemTextW(hDlg, IDC_LABEL_SATURATION, buf);
}

static INT_PTR CALLBACK BrightnessContrastDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        UINT imgWidth, imgHeight;
        if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }

        g_ctx.savedBrightness = g_ctx.brightness;
        g_ctx.savedContrast = g_ctx.contrast;
        g_ctx.savedSaturation = g_ctx.saturation;

        auto setupSlider = [&](int id, int minV, int maxV, float val) {
            HWND hSlider = GetDlgItem(hDlg, id);
            SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(minV, maxV));
            SendMessageW(hSlider, TBM_SETPOS, TRUE, static_cast<int>(val * 100));
            };
        setupSlider(IDC_SLIDER_BRIGHTNESS, -100, 100, g_ctx.brightness);
        setupSlider(IDC_SLIDER_CONTRAST, 0, 300, g_ctx.contrast);
        setupSlider(IDC_SLIDER_SATURATION, 0, 300, g_ctx.saturation);

        UpdateEffectLabels(hDlg);
        return (INT_PTR)TRUE;
    }
    case WM_HSCROLL: {
        HWND hSlider = (HWND)lParam;
        int pos = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);

        if (hSlider == GetDlgItem(hDlg, IDC_SLIDER_BRIGHTNESS)) {
            g_ctx.brightness = pos / 100.0f;
        }
        else if (hSlider == GetDlgItem(hDlg, IDC_SLIDER_CONTRAST)) {
            g_ctx.contrast = pos / 100.0f;
        }
        else if (hSlider == GetDlgItem(hDlg, IDC_SLIDER_SATURATION)) {
            g_ctx.saturation = pos / 100.0f;
        }

        ApplyEffectsToView();
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        UpdateEffectLabels(hDlg);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            g_ctx.savedBrightness = g_ctx.brightness;
            g_ctx.savedContrast = g_ctx.savedContrast;
            g_ctx.savedSaturation = g_ctx.savedSaturation;
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        case IDCANCEL: {
            g_ctx.brightness = g_ctx.savedBrightness;
            g_ctx.contrast = g_ctx.savedContrast;
            g_ctx.saturation = g_ctx.savedSaturation;
            ApplyEffectsToView();
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        case IDC_BUTTON_RESET_BC: {
            g_ctx.brightness = 0.0f;
            g_ctx.contrast = 1.0f;
            g_ctx.saturation = 1.0f;
            SendMessageW(GetDlgItem(hDlg, IDC_SLIDER_BRIGHTNESS), TBM_SETPOS, (WPARAM)TRUE, (LPARAM)0);
            SendMessageW(GetDlgItem(hDlg, IDC_SLIDER_CONTRAST), TBM_SETPOS, (WPARAM)TRUE, (LPARAM)100);
            SendMessageW(GetDlgItem(hDlg, IDC_SLIDER_SATURATION), TBM_SETPOS, (WPARAM)TRUE, (LPARAM)100);
            ApplyEffectsToView();
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
            UpdateEffectLabels(hDlg);
            return (INT_PTR)TRUE;
        }
        }
        break;
    }
    return (INT_PTR)FALSE;
}


void OpenBrightnessContrastDialog() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        MessageBoxW(g_ctx.hWnd, L"No image loaded to adjust.", L"Image Effects", MB_ICONERROR);
        return;
    }
    DialogBoxParam(g_ctx.hInst, MAKEINTRESOURCE(IDD_BRIGHTNESS_DIALOG), g_ctx.hWnd, BrightnessContrastDialogProc, 0);
}

std::wstring GetHotkeyString(WORD hk) {
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
    L"Fullscreen", L"Rotate Clockwise", L"Rotate Counter-Clockwise", L"Flip", L"Crop", L"Exit"
};

static INT_PTR CALLBACK KeybindingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_ACTION);
        for (int i = 0; i < Act_Count; ++i) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)ActionNames[i]);
        }
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_CTRL), HKM_SETHOTKEY, g_ctx.hotkeys[0], 0);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_COMBO_ACTION && HIWORD(wParam) == CBN_SELCHANGE) {
            int idx = SendMessageW((HWND)lParam, CB_GETCURSEL, 0, 0);
            if (idx != CB_ERR) {
                SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_CTRL), HKM_SETHOTKEY, g_ctx.hotkeys[idx], 0);
            }
            return (INT_PTR)TRUE;
        }
        switch (LOWORD(wParam)) {
        case IDOK: {
            int idx = SendMessageW(GetDlgItem(hDlg, IDC_COMBO_ACTION), CB_GETCURSEL, 0, 0);
            if (idx != CB_ERR) {
                g_ctx.hotkeys[idx] = SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_CTRL), HKM_GETHOTKEY, 0, 0);
                SetDlgItemTextW(hDlg, IDOK, L"Applied!");
                SetTimer(hDlg, 1, 1500, nullptr);
            }
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
                 break;
        case WM_TIMER:
            if (wParam == 1) {
                KillTimer(hDlg, 1);
                SetDlgItemTextW(hDlg, IDOK, L"Apply");
            }
            return (INT_PTR)TRUE;
        }
        return (INT_PTR)FALSE;
    }

void OpenKeybindingsDialog() {
    DialogBoxParam(g_ctx.hInst, MAKEINTRESOURCE(IDD_KEYBINDINGS_DIALOG), g_ctx.hWnd, KeybindingsDialogProc, 0);
}
