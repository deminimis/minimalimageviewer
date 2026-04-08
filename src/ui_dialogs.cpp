
#include "viewer.h"
#include <commctrl.h>
#include <stdio.h>

extern AppContext g_ctx;

static INT_PTR CALLBACK PreferencesDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
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

