
#include "viewer.h"
#include <commctrl.h>
#include <stdio.h>



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
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        if (ctx.bgColor == BackgroundColor::Black || ctx.bgColor == BackgroundColor::Grey) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(32, 32, 32));
            return (INT_PTR)ctx.darkBrush;
        }
        break;
    }
    case WM_DESTROY:
        break;
    }
    return (INT_PTR)FALSE;
}

void ViewerApp::OpenPreferencesDialog() {
    DialogBoxParam(m_ctx.hInst, MAKEINTRESOURCE(IDD_PREFERENCES_DIALOG), m_ctx.hWnd, PreferencesDialogProc, (LPARAM)this);
}

static void UpdateEffectLabels(HWND hDlg, ViewerApp* pApp) {
    auto& ctx = pApp->GetContext();
    wchar_t buf[64];
    swprintf_s(buf, L"Brightness: %d%%", static_cast<int>(ctx.brightness * 100));
    SetDlgItemTextW(hDlg, IDC_LABEL_BRIGHTNESS, buf);
    swprintf_s(buf, L"Contrast: %d%%", static_cast<int>(ctx.contrast * 100));
    SetDlgItemTextW(hDlg, IDC_LABEL_CONTRAST, buf);
    swprintf_s(buf, L"Saturation: %d%%", static_cast<int>(ctx.saturation * 100));
    SetDlgItemTextW(hDlg, IDC_LABEL_SATURATION, buf);
}

INT_PTR CALLBACK ViewerApp::BrightnessContrastDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
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
        UINT imgWidth, imgHeight;
        if (!pApp->GetCurrentImageSize(&imgWidth, &imgHeight)) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }

        ctx.savedBrightness = ctx.brightness;
        ctx.savedContrast = ctx.contrast;
        ctx.savedSaturation = ctx.saturation;
        auto setupSlider = [&](int id, int minV, int maxV, float val) {
            HWND hSlider = GetDlgItem(hDlg, id);
            SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(minV, maxV));
            SendMessageW(hSlider, TBM_SETPOS, TRUE, static_cast<int>(val * 100));
            };
        setupSlider(IDC_SLIDER_BRIGHTNESS, -100, 100, ctx.brightness);
        setupSlider(IDC_SLIDER_CONTRAST, 0, 300, ctx.contrast);
        setupSlider(IDC_SLIDER_SATURATION, 0, 300, ctx.saturation);

        UpdateEffectLabels(hDlg, pApp);
        return (INT_PTR)TRUE;
    }
    case WM_HSCROLL: {
        HWND hSlider = (HWND)lParam;
        int pos = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);

        if (hSlider == GetDlgItem(hDlg, IDC_SLIDER_BRIGHTNESS)) {
            ctx.brightness = pos / 100.0f;
        }
        else if (hSlider == GetDlgItem(hDlg, IDC_SLIDER_CONTRAST)) {
            ctx.contrast = pos / 100.0f;
        }
        else if (hSlider == GetDlgItem(hDlg, IDC_SLIDER_SATURATION)) {
            ctx.saturation = pos / 100.0f;
        }

        pApp->ApplyEffectsToView();
        InvalidateRect(ctx.hWnd, nullptr, FALSE);
        UpdateEffectLabels(hDlg, pApp);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BUTTON_RESET_BC:
            ctx.brightness = 0.0f;
            ctx.contrast = 1.0f;
            ctx.saturation = 1.0f;
            SendMessageW(GetDlgItem(hDlg, IDC_SLIDER_BRIGHTNESS), TBM_SETPOS, TRUE, 0);
            SendMessageW(GetDlgItem(hDlg, IDC_SLIDER_CONTRAST), TBM_SETPOS, TRUE, 100);
            SendMessageW(GetDlgItem(hDlg, IDC_SLIDER_SATURATION), TBM_SETPOS, TRUE, 100);
            pApp->ApplyEffectsToView();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
            UpdateEffectLabels(hDlg, pApp);
            return (INT_PTR)TRUE;
        case IDOK:
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        case IDCANCEL:
            ctx.brightness = ctx.savedBrightness;
            ctx.contrast = ctx.savedContrast;
            ctx.saturation = ctx.savedSaturation;
            pApp->ApplyEffectsToView();
            InvalidateRect(ctx.hWnd, nullptr, FALSE);
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void ViewerApp::OpenBrightnessContrastDialog() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        MessageBoxW(m_ctx.hWnd, L"No image loaded to adjust.", L"Image Effects", MB_ICONERROR);
        return;
    }
    DialogBoxParam(m_ctx.hInst, MAKEINTRESOURCE(IDD_BRIGHTNESS_DIALOG), m_ctx.hWnd, BrightnessContrastDialogProc, (LPARAM)this);
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
    L"Fullscreen", L"Rotate Clockwise", L"Rotate Counter-Clockwise", L"Flip", L"Crop", L"Exit"
};

INT_PTR CALLBACK ViewerApp::KeybindingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
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

void ViewerApp::OpenKeybindingsDialog() {
    DialogBoxParam(m_ctx.hInst, MAKEINTRESOURCE(IDD_KEYBINDINGS_DIALOG), m_ctx.hWnd, KeybindingsDialogProc, (LPARAM)this);
}
