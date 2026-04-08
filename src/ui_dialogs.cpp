
#include "viewer.h"
#include <commctrl.h>
#include <stdio.h>

extern AppContext g_ctx;

static INT_PTR CALLBACK PreferencesDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        switch (g_ctx.bgColor) {
        case BackgroundColor::Grey:       CheckRadioButton(hDlg, IDC_RADIO_BG_GREY, IDC_RADIO_BG_TRANSPARENT, IDC_RADIO_BG_GREY); break;
        case BackgroundColor::Black:      CheckRadioButton(hDlg, IDC_RADIO_BG_GREY, IDC_RADIO_BG_TRANSPARENT, IDC_RADIO_BG_BLACK); break;
        case BackgroundColor::White:      CheckRadioButton(hDlg, IDC_RADIO_BG_GREY, IDC_RADIO_BG_TRANSPARENT, IDC_RADIO_BG_WHITE); break;
        case BackgroundColor::Transparent:CheckRadioButton(hDlg, IDC_RADIO_BG_GREY, IDC_RADIO_BG_TRANSPARENT, IDC_RADIO_BG_TRANSPARENT); break;
        }

        CheckDlgButton(hDlg, IDC_CHECK_ALWAYS_ON_TOP, g_ctx.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_START_FULLSCREEN, g_ctx.startFullScreen ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_SINGLE_INSTANCE, g_ctx.enforceSingleInstance ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_AUTO_REFRESH, g_ctx.isAutoRefresh ? BST_CHECKED : BST_UNCHECKED);

        if (g_ctx.defaultZoomMode == DefaultZoomMode::Fit) {
            CheckRadioButton(hDlg, IDC_RADIO_ZOOM_FIT, IDC_RADIO_ZOOM_ACTUAL, IDC_RADIO_ZOOM_FIT);
        }
        else {
            CheckRadioButton(hDlg, IDC_RADIO_ZOOM_FIT, IDC_RADIO_ZOOM_ACTUAL, IDC_RADIO_ZOOM_ACTUAL);
        }
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            if (IsDlgButtonChecked(hDlg, IDC_RADIO_BG_GREY)) g_ctx.bgColor = BackgroundColor::Grey;
            else if (IsDlgButtonChecked(hDlg, IDC_RADIO_BG_BLACK)) g_ctx.bgColor = BackgroundColor::Black;
            else if (IsDlgButtonChecked(hDlg, IDC_RADIO_BG_WHITE)) g_ctx.bgColor = BackgroundColor::White;
            else if (IsDlgButtonChecked(hDlg, IDC_RADIO_BG_TRANSPARENT)) g_ctx.bgColor = BackgroundColor::Transparent;

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
            InvalidateRect(g_ctx.hWnd, NULL, FALSE);

            EndDialog(hDlg, IDOK);
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

        HWND hSliderBright = GetDlgItem(hDlg, IDC_SLIDER_BRIGHTNESS);
        HWND hSliderContrast = GetDlgItem(hDlg, IDC_SLIDER_CONTRAST);
        HWND hSliderSaturation = GetDlgItem(hDlg, IDC_SLIDER_SATURATION);

        SendMessageW(hSliderBright, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(-100, 100));
        SendMessageW(hSliderBright, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)static_cast<int>(g_ctx.brightness * 100));

        SendMessageW(hSliderContrast, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0, 300));
        SendMessageW(hSliderContrast, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)static_cast<int>(g_ctx.contrast * 100));

        SendMessageW(hSliderSaturation, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0, 300));
        SendMessageW(hSliderSaturation, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)static_cast<int>(g_ctx.saturation * 100));

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

