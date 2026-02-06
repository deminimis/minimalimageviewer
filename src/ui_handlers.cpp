#include "viewer.h"
#include "exif_utils.h"
#include <string>
#include <stdio.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

extern AppContext g_ctx;


static void HandleEyedropperClick();
void HandleCopy();
void HandlePaste();
void DeleteCurrentImage();
void OpenFileLocationAction();
void ShowImageProperties();
void OpenPreferencesDialog();
static void OpenBrightnessContrastDialog();

static void UpdateEyedropperColor(POINT pt) {
    g_ctx.didCopyColor = false;
    float localX = 0, localY = 0;
    UINT imgWidth = 0, imgHeight = 0;

    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        g_ctx.colorStringRgb = L"N/A";
        g_ctx.colorStringHex = L"";
        return;
    }

    ConvertWindowToImagePoint(pt, localX, localY);

    BYTE r = 0, g = 0, b = 0;
    bool inImage = (localX >= 0 && localX < imgWidth && localY >= 0 && localY < imgHeight);

    if (inImage) {
        CriticalSectionLock lock(g_ctx.wicMutex);
        ComPtr<IWICBitmapSource> source;

        if (g_ctx.isAnimated && g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size()) {
            source = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
        }
        else {
            source = g_ctx.wicConverter;
        }

        if (source) {
            WICRect rc = { static_cast<INT>(localX), static_cast<INT>(localY), 1, 1 };
            UINT32 pixelData = 0;
            HRESULT hr = source->CopyPixels(&rc, 4, 4, reinterpret_cast<BYTE*>(&pixelData));

            if (SUCCEEDED(hr)) {
                b = (pixelData) & 0xFF;
                g = (pixelData >> 8) & 0xFF;
                r = (pixelData >> 16) & 0xFF;
                BYTE a = (pixelData >> 24) & 0xFF;

                if (a != 0 && a != 255) {
                    r = (r * 255) / a;
                    g = (g * 255) / a;
                    b = (b * 255) / a;
                }
            }
        }
    }
    else {
        switch (g_ctx.bgColor) {
        case BackgroundColor::Black:
            r = 0; g = 0; b = 0;
            break;
        case BackgroundColor::White:
            r = 255; g = 255; b = 255;
            break;
        case BackgroundColor::Grey:
            r = 30; g = 30; b = 30;
            break;
        case BackgroundColor::Transparent:
            g_ctx.colorStringRgb = L"N/A (Transparent BG)";
            g_ctx.colorStringHex = L"";
            return;
        }
    }

    g_ctx.hoveredColor = RGB(r, g, b);
    wchar_t rgbBuf[32];
    swprintf_s(rgbBuf, L"RGB(%d, %d, %d)", r, g, b);
    g_ctx.colorStringRgb = rgbBuf;

    wchar_t hexBuf[16];
    swprintf_s(hexBuf, L"#%02X%02X%02X", r, g, b);
    g_ctx.colorStringHex = hexBuf;
}

static void HandleEyedropperClick() {
    if (g_ctx.colorStringHex.empty()) return;

    if (!OpenClipboard(g_ctx.hWnd)) return;
    EmptyClipboard();

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (g_ctx.colorStringHex.length() + 1) * sizeof(wchar_t));
    if (hMem) {
        LPWSTR pMem = static_cast<LPWSTR>(GlobalLock(hMem));
        if (pMem) {
            wcscpy_s(pMem, g_ctx.colorStringHex.length() + 1, g_ctx.colorStringHex.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
            g_ctx.didCopyColor = true;
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        }
    }
    CloseClipboard();
}

void ToggleFullScreen() {
    if (!g_ctx.isFullScreen) {
        g_ctx.savedStyle = GetWindowLong(g_ctx.hWnd, GWL_STYLE);
        GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        HMONITOR hMonitor = MonitorFromWindow(g_ctx.hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, g_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_ctx.isFullScreen = true;
    }
    else {
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, g_ctx.savedStyle | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, g_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, g_ctx.savedRect.left, g_ctx.savedRect.top,
            g_ctx.savedRect.right - g_ctx.savedRect.left, g_ctx.savedRect.bottom - g_ctx.savedRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_ctx.isFullScreen = false;
    }
    FitImageToWindow();
}

static void OpenFileAction() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = g_ctx.hWnd;
    ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2\0All Files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        LoadImageFromFile(szFile);
    }
}

static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hWnd, &ps);

    if (g_ctx.isInitialized) {
        Render();
    }

    EndPaint(hWnd, &ps);
}

static void OnKeyDown(WPARAM wParam) {
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    switch (wParam) {
    case VK_RIGHT:
        if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex + 1) % static_cast<int>(size);
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    case VK_LEFT:
        if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex - 1 + static_cast<int>(size)) % static_cast<int>(size);
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    case VK_UP:    RotateImage(true); break;
    case VK_DOWN:  RotateImage(false); break;
    case VK_DELETE: DeleteCurrentImage(); break;
    case VK_F11:   ToggleFullScreen(); break;
    case 'F':      FlipImage(); break;
    case VK_ESCAPE:
        if (g_ctx.isCropMode || g_ctx.isSelectingCropRect || g_ctx.isCropPending || g_ctx.isSelectingOcrRect || g_ctx.isEyedropperActive) {
            g_ctx.isCropMode = false;
            g_ctx.isSelectingCropRect = false;
            g_ctx.isCropPending = false;
            g_ctx.isSelectingOcrRect = false;
            g_ctx.isDraggingOcrRect = false;
            g_ctx.isEyedropperActive = false;
            g_ctx.ocrRectWindow = { 0 };

            // Revert any pending views (like full image if we were in crop mode)
            ApplyEffectsToView();
            FitImageToWindow();

            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            if (GetCapture() == g_ctx.hWnd) {
                ReleaseCapture();
            }
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        }
        else {
            PostQuitMessage(0);
        }
        break;
    case 'O':      if (ctrlPressed) OpenFileAction(); break;
    case 'S':      if (ctrlPressed && (GetKeyState(VK_SHIFT) & 0x8000)) SaveImageAs(); else if (ctrlPressed) SaveImage(); break;
    case 'C':
        if (ctrlPressed) {
            HandleCopy();
        }
        else {
            g_ctx.isCropMode = !g_ctx.isCropMode;
            g_ctx.isCropActive = false;
            g_ctx.isCropPending = false;
            g_ctx.isSelectingCropRect = false;

            // Refresh view to show full image if we just toggled crop mode
            ApplyEffectsToView();
            FitImageToWindow();

            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
            SetCursor(LoadCursor(nullptr, g_ctx.isCropMode ? IDC_CROSS : IDC_ARROW));
        }
        break;
    case 'V':      if (ctrlPressed) HandlePaste(); break;
    case '0':      if (ctrlPressed) CenterImage(true); break;
    case VK_MULTIPLY: if (ctrlPressed) SetActualSize(); break;
    case VK_RETURN:
        if (g_ctx.isCropPending) {
            g_ctx.isCropActive = true;
            g_ctx.isCropPending = false;
            g_ctx.isCropMode = false;

            // Apply crop to the actual view
            ApplyEffectsToView();
            FitImageToWindow();

            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        }
        break;
    case 'I':
        g_ctx.isOsdVisible = !g_ctx.isOsdVisible;
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        break;
    case 'Q':
        PerformOcr();
        break;
    case 'W':
        g_ctx.isSelectingOcrRect = true;
        g_ctx.isDraggingOcrRect = false;
        g_ctx.isCropMode = false;
        g_ctx.isSelectingCropRect = false;
        g_ctx.isCropPending = false;
        g_ctx.isEyedropperActive = false;
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    case VK_ADD:
    case VK_OEM_PLUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(1.25f, centerPt);
        }
        break;
    case VK_SUBTRACT:
    case VK_OEM_MINUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(0.8f, centerPt);
        }
        break;
    case VK_F5:
        if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    }
}

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


static void OpenBrightnessContrastDialog() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        MessageBoxW(g_ctx.hWnd, L"No image loaded to adjust.", L"Image Effects", MB_ICONERROR);
        return;
    }
    DialogBoxParam(g_ctx.hInst, MAKEINTRESOURCE(IDD_BRIGHTNESS_DIALOG), g_ctx.hWnd, BrightnessContrastDialogProc, 0);
}

static void OnContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN, L"Open Image\tCtrl+O");
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"Refresh\tF5");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_COPY, L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_NEXT_IMG, L"Next Image\tRight Arrow");
    AppendMenuW(hMenu, MF_STRING, IDM_PREV_IMG, L"Previous Image\tLeft Arrow");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    HMENU hSortMenu = CreatePopupMenu();
    bool isNameAsc = (g_ctx.currentSortCriteria == SortCriteria::ByName && g_ctx.isSortAscending);
    bool isNameDesc = (g_ctx.currentSortCriteria == SortCriteria::ByName && !g_ctx.isSortAscending);
    bool isDateAsc = (g_ctx.currentSortCriteria == SortCriteria::ByDateModified && g_ctx.isSortAscending);
    bool isDateDesc = (g_ctx.currentSortCriteria == SortCriteria::ByDateModified && !g_ctx.isSortAscending);
    bool isSizeAsc = (g_ctx.currentSortCriteria == SortCriteria::ByFileSize && g_ctx.isSortAscending);
    bool isSizeDesc = (g_ctx.currentSortCriteria == SortCriteria::ByFileSize && !g_ctx.isSortAscending);

    AppendMenuW(hSortMenu, MF_STRING | (isNameAsc ? MF_CHECKED : MF_UNCHECKED), IDM_SORT_BY_NAME_ASC, L"Name (Ascending)");
    AppendMenuW(hSortMenu, MF_STRING | (isNameDesc ? MF_CHECKED : MF_UNCHECKED), IDM_SORT_BY_NAME_DESC, L"Name (Descending)");
    AppendMenuW(hSortMenu, MF_STRING | (isDateAsc ? MF_CHECKED : MF_UNCHECKED), IDM_SORT_BY_DATE_ASC, L"Date Modified (Ascending)");
    AppendMenuW(hSortMenu, MF_STRING | (isDateDesc ? MF_CHECKED : MF_UNCHECKED), IDM_SORT_BY_DATE_DESC, L"Date Modified (Descending)");
    AppendMenuW(hSortMenu, MF_STRING | (isSizeAsc ? MF_CHECKED : MF_UNCHECKED), IDM_SORT_BY_SIZE_ASC, L"File Size (Ascending)");
    AppendMenuW(hSortMenu, MF_STRING | (isSizeDesc ? MF_CHECKED : MF_UNCHECKED), IDM_SORT_BY_SIZE_DESC, L"File Size (Descending)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSortMenu, L"Sort By");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    HMENU hEditMenu = CreatePopupMenu();
    AppendMenuW(hEditMenu, MF_STRING, IDM_ROTATE_CW, L"Rotate Clockwise\tUp Arrow");
    AppendMenuW(hEditMenu, MF_STRING, IDM_ROTATE_CCW, L"Rotate Counter-Clockwise\tDown Arrow");
    AppendMenuW(hEditMenu, MF_STRING, IDM_FLIP, L"Flip\tF");
    AppendMenuW(hEditMenu, MF_STRING | (g_ctx.isGrayscale ? MF_CHECKED : MF_UNCHECKED), IDM_GRAYSCALE, L"Grayscale");
    AppendMenuW(hEditMenu, MF_STRING, IDM_CROP, L"Crop\tC");
    AppendMenuW(hEditMenu, MF_STRING, IDM_RESIZE, L"Resize Image...");
    AppendMenuW(hEditMenu, MF_STRING, IDM_BRIGHTNESS_CONTRAST, L"Image Effects...");
    AppendMenuW(hEditMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hEditMenu, MF_STRING | (g_ctx.isEyedropperActive ? MF_CHECKED : MF_UNCHECKED), IDM_EYEDROPPER, L"Pick Color");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"Edit");

    HMENU hViewMenu = CreatePopupMenu();
    AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_IN, L"Zoom In\tCtrl++");
    AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_OUT, L"Zoom Out\tCtrl+-");
    AppendMenuW(hViewMenu, MF_STRING, IDM_ACTUAL_SIZE, L"Actual Size (100%)\tCtrl+*");
    AppendMenuW(hViewMenu, MF_STRING, IDM_FIT_TO_WINDOW, L"Fit to Window\tCtrl+0");
    AppendMenuW(hViewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hViewMenu, MF_STRING, IDM_FULLSCREEN, L"Full Screen\tF11");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"View");

    AppendMenuW(hMenu, MF_STRING, IDM_OCR, L"Copy Text (OCR)\tQ");
    AppendMenuW(hMenu, MF_STRING, IDM_OCR_AREA, L"Copy Text from Area\tW");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE, L"Save\tCtrl+S");
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE_AS, L"Save As\tCtrl+Shift+S");

    UINT locationFlags = (g_ctx.currentImageIndex != -1) ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(hMenu, locationFlags, IDM_OPEN_LOCATION, L"Open File Location");
    AppendMenuW(hMenu, locationFlags, IDM_PROPERTIES, L"Properties...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_PREFERENCES, L"Preferences...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING, IDM_DELETE_IMG, L"Delete Image\tDelete");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit\tEsc");

    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_OPEN:          OpenFileAction(); break;
    case IDM_REFRESH:       OnKeyDown(VK_F5); break;
    case IDM_COPY:          HandleCopy(); break;
    case IDM_PASTE:         HandlePaste(); break;
    case IDM_NEXT_IMG:      OnKeyDown(VK_RIGHT); break;
    case IDM_PREV_IMG:      OnKeyDown(VK_LEFT); break;
    case IDM_ZOOM_IN: {
        POINT clientPt = pt;
        ScreenToClient(hWnd, &clientPt);
        ZoomImage(1.25f, clientPt);
        break;
    }
    case IDM_ZOOM_OUT: {
        POINT clientPt = pt;
        ScreenToClient(hWnd, &clientPt);
        ZoomImage(0.8f, clientPt);
        break;
    }
    case IDM_ACTUAL_SIZE:   SetActualSize(); break;
    case IDM_FIT_TO_WINDOW: FitImageToWindow(); break;
    case IDM_FULLSCREEN:    ToggleFullScreen(); break;
    case IDM_DELETE_IMG:    DeleteCurrentImage(); break;
    case IDM_EXIT:          PostQuitMessage(0); break;
    case IDM_ROTATE_CW:     RotateImage(true); break;
    case IDM_ROTATE_CCW:    RotateImage(false); break;
    case IDM_FLIP:          FlipImage(); break;
    case IDM_GRAYSCALE:
        g_ctx.isGrayscale = !g_ctx.isGrayscale;
        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.d2dBitmap = nullptr;
            g_ctx.animationD2DBitmaps.clear();
        }
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        break;
    case IDM_CROP:
        g_ctx.isCropMode = true;
        g_ctx.isCropActive = false;
        g_ctx.isCropPending = false;
        g_ctx.isSelectingCropRect = false;

        // Reset full view when starting new crop
        ApplyEffectsToView();
        FitImageToWindow();

        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    case IDM_RESIZE:        ResizeImageAction(); break;
    case IDM_BRIGHTNESS_CONTRAST: OpenBrightnessContrastDialog(); break;
    case IDM_EYEDROPPER:
        g_ctx.isEyedropperActive = !g_ctx.isEyedropperActive;
        g_ctx.didCopyColor = false;
        SetCursor(LoadCursor(nullptr, g_ctx.isEyedropperActive ? IDC_CROSS : IDC_ARROW));
        if (g_ctx.isEyedropperActive) SetCapture(g_ctx.hWnd);
        else ReleaseCapture();
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        break;
    case IDM_OCR:           PerformOcr(); break;
    case IDM_OCR_AREA:
        g_ctx.isSelectingOcrRect = true;
        g_ctx.isDraggingOcrRect = false;
        g_ctx.isCropMode = false;
        g_ctx.isSelectingCropRect = false;
        g_ctx.isCropPending = false;
        g_ctx.isEyedropperActive = false;
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    case IDM_SAVE:          SaveImage(); break;
    case IDM_SAVE_AS:       SaveImageAs(); break;
    case IDM_OPEN_LOCATION: OpenFileLocationAction(); break;
    case IDM_PROPERTIES:    ShowImageProperties(); break;
    case IDM_PREFERENCES:   OpenPreferencesDialog(); break;

    case IDM_SORT_BY_NAME_ASC:
    case IDM_SORT_BY_NAME_DESC:
    case IDM_SORT_BY_DATE_ASC:
    case IDM_SORT_BY_DATE_DESC:
    case IDM_SORT_BY_SIZE_ASC:
    case IDM_SORT_BY_SIZE_DESC:
    {
        std::wstring currentFile;
        if (g_ctx.currentImageIndex >= 0 && g_ctx.currentImageIndex < static_cast<int>(g_ctx.imageFiles.size())) {
            currentFile = g_ctx.imageFiles[g_ctx.currentImageIndex];
        }

        if (cmd == IDM_SORT_BY_NAME_ASC) { g_ctx.currentSortCriteria = SortCriteria::ByName; g_ctx.isSortAscending = true; }
        else if (cmd == IDM_SORT_BY_NAME_DESC) { g_ctx.currentSortCriteria = SortCriteria::ByName; g_ctx.isSortAscending = false; }
        else if (cmd == IDM_SORT_BY_DATE_ASC) { g_ctx.currentSortCriteria = SortCriteria::ByDateModified; g_ctx.isSortAscending = true; }
        else if (cmd == IDM_SORT_BY_DATE_DESC) { g_ctx.currentSortCriteria = SortCriteria::ByDateModified; g_ctx.isSortAscending = false; }
        else if (cmd == IDM_SORT_BY_SIZE_ASC) { g_ctx.currentSortCriteria = SortCriteria::ByFileSize; g_ctx.isSortAscending = true; }
        else if (cmd == IDM_SORT_BY_SIZE_DESC) { g_ctx.currentSortCriteria = SortCriteria::ByFileSize; g_ctx.isSortAscending = false; }

        if (!g_ctx.currentDirectory.empty()) {
            LoadImageFromFile(currentFile);
        }
        break;
    }
    }
}

static std::wstring FormatFileTime(const FILETIME& ft) {
    SYSTEMTIME stUTC = {}, stLocal = {};
    if (!FileTimeToSystemTime(&ft, &stUTC)) {
        return L"N/A";
    }
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &stUTC, &stLocal)) {
        return L"N/A";
    }

    wchar_t szDate[256], szTime[256], szDateTime[512];
    GetDateFormatW(LOCALE_USER_DEFAULT, 0, &stLocal, nullptr, szDate, 256);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &stLocal, nullptr, szTime, 256);
    swprintf_s(szDateTime, 512, L"%s  %s", szDate, szTime);
    return szDateTime;
}

static std::wstring FormatFileSize(const LARGE_INTEGER& fileSize) {
    double size = static_cast<double>(fileSize.QuadPart);
    const wchar_t* unit = L"Bytes";
    wchar_t buffer[256];

    if (size >= 1024.0 * 1024.0 * 1024.0) {
        size /= (1024.0 * 1024.0 * 1024.0);
        unit = L"GB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else if (size >= 1024.0 * 1024.0) {
        size /= (1024.0 * 1024.0);
        unit = L"MB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else if (size >= 1024.0) {
        size /= 1024.0;
        unit = L"KB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else {
        swprintf_s(buffer, 256, L"%llu Bytes", fileSize.QuadPart);
    }
    return buffer;
}

ImageProperties GetCurrentOsdProperties() {
    ImageProperties pProps = {};

    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        if (!g_ctx.currentFilePathOverride.empty()) {
            pProps.filePath = g_ctx.currentFilePathOverride;
        }
        return pProps;
    }

    const std::wstring& filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];
    pProps.filePath = filePath;

    UINT imgWidth = 0, imgHeight = 0;
    if (GetCurrentImageSize(&imgWidth, &imgHeight)) {
        pProps.dimensions = std::to_wstring(imgWidth) + L" x " + std::to_wstring(imgHeight) + L" pixels";
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER fileSize;
        fileSize.HighPart = fad.nFileSizeHigh;
        fileSize.LowPart = fad.nFileSizeLow;
        pProps.fileSize = FormatFileSize(fileSize);

        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) pProps.attributes += L"Read-only; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) pProps.attributes += L"Hidden; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) pProps.attributes += L"System; ";
        if (pProps.attributes.empty()) pProps.attributes = L"Normal";

    }

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> metadataReader;

    if (SUCCEEDED(CreateDecoderFromFile(filePath.c_str(), &decoder))) {
        GUID containerFormat;
        if (SUCCEEDED(decoder->GetContainerFormat(&containerFormat))) {
            pProps.imageFormat = GetContainerFormatName(containerFormat);
            if (g_ctx.isAnimated) {
                pProps.imageFormat += L" (Animated)";
            }
        }

        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {

            pProps.bitDepth = GetBitDepth(frame);

            double dpiX, dpiY;
            if (SUCCEEDED(frame->GetResolution(&dpiX, &dpiY))) {
                pProps.dpi = std::to_wstring(static_cast<int>(dpiX + 0.5)) + L" x " + std::to_wstring(static_cast<int>(dpiY + 0.5)) + L" DPI";
            }

            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                pProps.fStop = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33437}");
                pProps.exposureTime = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33434}");
                pProps.iso = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=34855}");
                pProps.software = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=305}");
                pProps.author = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=315}");
            }
        }
    }
    return pProps;
}

LRESULT CALLBACK PropsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
        ImageProperties* pProps = (ImageProperties*)pcs->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pProps);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        ImageProperties* pProps = (ImageProperties*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

        if (pProps) {
            SetBkMode(hdc, TRANSPARENT);
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SelectObject(hdc, hFont);

            int y = 15;
            int x_label = 15;
            int x_value = 160;
            int y_step = 20;

            TextOutW(hdc, x_label, y, L"File Path:", 10);
            TextOutW(hdc, x_value, y, pProps->filePath.c_str(), (int)pProps->filePath.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"Image Format:", 13);
            TextOutW(hdc, x_value, y, pProps->imageFormat.c_str(), (int)pProps->imageFormat.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Dimensions:", 11);
            TextOutW(hdc, x_value, y, pProps->dimensions.c_str(), (int)pProps->dimensions.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Bit Depth:", 10);
            TextOutW(hdc, x_value, y, pProps->bitDepth.c_str(), (int)pProps->bitDepth.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"DPI:", 4);
            TextOutW(hdc, x_value, y, pProps->dpi.c_str(), (int)pProps->dpi.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"File Size:", 10);
            TextOutW(hdc, x_value, y, pProps->fileSize.c_str(), (int)pProps->fileSize.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Attributes:", 11);
            TextOutW(hdc, x_value, y, pProps->attributes.c_str(), (int)pProps->attributes.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Created:", 8);
            TextOutW(hdc, x_value, y, pProps->createdDate.c_str(), (int)pProps->createdDate.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Modified:", 9);
            TextOutW(hdc, x_value, y, pProps->modifiedDate.c_str(), (int)pProps->modifiedDate.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Accessed:", 9);
            TextOutW(hdc, x_value, y, pProps->accessedDate.c_str(), (int)pProps->accessedDate.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"Camera Make:", 12);
            TextOutW(hdc, x_value, y, pProps->cameraMake.c_str(), (int)pProps->cameraMake.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Camera Model:", 13);
            TextOutW(hdc, x_value, y, pProps->cameraModel.c_str(), (int)pProps->cameraModel.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Date Taken:", 11);
            TextOutW(hdc, x_value, y, pProps->dateTaken.c_str(), (int)pProps->dateTaken.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"F-stop:", 7);
            TextOutW(hdc, x_value, y, pProps->fStop.c_str(), (int)pProps->fStop.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Exposure:", 9);
            TextOutW(hdc, x_value, y, pProps->exposureTime.c_str(), (int)pProps->exposureTime.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"ISO:", 4);
            TextOutW(hdc, x_value, y, pProps->iso.c_str(), (int)pProps->iso.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Focal Length:", 13);
            TextOutW(hdc, x_value, y, pProps->focalLength.c_str(), (int)pProps->focalLength.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"35mm Focal Length:", 18);
            TextOutW(hdc, x_value, y, pProps->focalLength35mm.c_str(), (int)pProps->focalLength35mm.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Exposure Bias:", 14);
            TextOutW(hdc, x_value, y, pProps->exposureBias.c_str(), (int)pProps->exposureBias.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Exposure Program:", 17);
            TextOutW(hdc, x_value, y, pProps->exposureProgram.c_str(), (int)pProps->exposureProgram.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"White Balance:", 14);
            TextOutW(hdc, x_value, y, pProps->whiteBalance.c_str(), (int)pProps->whiteBalance.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Metering Mode:", 14);
            TextOutW(hdc, x_value, y, pProps->meteringMode.c_str(), (int)pProps->meteringMode.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Flash:", 6);
            TextOutW(hdc, x_value, y, pProps->flash.c_str(), (int)pProps->flash.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Lens Model:", 11);
            TextOutW(hdc, x_value, y, pProps->lensModel.c_str(), (int)pProps->lensModel.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"Author:", 7);
            TextOutW(hdc, x_value, y, pProps->author.c_str(), (int)pProps->author.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Copyright:", 10);
            TextOutW(hdc, x_value, y, pProps->copyright.c_str(), (int)pProps->copyright.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Software:", 9);
            TextOutW(hdc, x_value, y, pProps->software.c_str(), (int)pProps->software.length());
        }
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY: {
        ImageProperties* pProps = (ImageProperties*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        delete pProps;
        g_ctx.hPropsWnd = nullptr;
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void ShowImageProperties() {
    if (g_ctx.hPropsWnd) {
        SetForegroundWindow(g_ctx.hPropsWnd);
        return;
    }

    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        return;
    }

    const std::wstring& filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];

    ImageProperties* pProps = new ImageProperties();

    pProps->filePath = filePath;

    UINT imgWidth = 0, imgHeight = 0;
    if (GetCurrentImageSize(&imgWidth, &imgHeight)) {
        pProps->dimensions = std::to_wstring(imgWidth) + L" x " + std::to_wstring(imgHeight) + L" pixels";
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER fileSize;
        fileSize.HighPart = fad.nFileSizeHigh;
        fileSize.LowPart = fad.nFileSizeLow;
        pProps->fileSize = FormatFileSize(fileSize);
        pProps->createdDate = FormatFileTime(fad.ftCreationTime);
        pProps->modifiedDate = FormatFileTime(fad.ftLastWriteTime);
        pProps->accessedDate = FormatFileTime(fad.ftLastAccessTime);

        // FIX: Ensure '->' is used here, not '.'
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) pProps->attributes += L"Read-only; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) pProps->attributes += L"Hidden; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) pProps->attributes += L"System; ";
        if (pProps->attributes.empty()) pProps->attributes = L"Normal";
    }
    else {
        pProps->fileSize = L"N/A";
        pProps->createdDate = L"N/A";
        pProps->modifiedDate = L"N/A";
        pProps->accessedDate = L"N/A";
        pProps->attributes = L"N/A";
    }

    // Initialize defaults
    pProps->imageFormat = L"N/A";
    pProps->bitDepth = L"N/A";
    pProps->dpi = L"N/A";
    pProps->cameraMake = L"N/A";
    pProps->cameraModel = L"N/A";
    pProps->dateTaken = L"N/A";
    pProps->fStop = L"N/A";
    pProps->exposureTime = L"N/A";
    pProps->iso = L"N/A";
    pProps->software = L"N/A";
    pProps->focalLength = L"N/A";
    pProps->focalLength35mm = L"N/A";
    pProps->exposureBias = L"N/A";
    pProps->meteringMode = L"N/A";
    pProps->flash = L"N/A";
    pProps->exposureProgram = L"N/A";
    pProps->whiteBalance = L"N/A";
    pProps->author = L"N/A";
    pProps->copyright = L"N/A";
    pProps->lensModel = L"N/A";

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> metadataReader;

    if (SUCCEEDED(CreateDecoderFromFile(filePath.c_str(), &decoder))) {
        GUID containerFormat;
        if (SUCCEEDED(decoder->GetContainerFormat(&containerFormat))) {
            pProps->imageFormat = GetContainerFormatName(containerFormat);
            if (g_ctx.isAnimated) {
                pProps->imageFormat += L" (Animated)";
            }
        }

        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
            pProps->bitDepth = GetBitDepth(frame);

            double dpiX, dpiY;
            if (SUCCEEDED(frame->GetResolution(&dpiX, &dpiY))) {
                pProps->dpi = std::to_wstring(static_cast<int>(dpiX + 0.5)) + L" x " + std::to_wstring(static_cast<int>(dpiY + 0.5)) + L" DPI";
            }

            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                pProps->dateTaken = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=36867}");
                pProps->cameraMake = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=271}");
                pProps->cameraModel = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=272}");
                pProps->fStop = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33437}");
                pProps->exposureTime = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33434}");
                pProps->iso = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=34855}");
                pProps->software = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=305}");
                pProps->focalLength = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=37386}");
                pProps->focalLength35mm = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=41989}");
                pProps->exposureBias = GetMetadataString(metadataReader, L"/app1/ifd/exif/{srational=37380}");
                pProps->meteringMode = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=37383}");
                pProps->flash = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=37385}");
                pProps->exposureProgram = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=34850}");
                pProps->whiteBalance = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=41987}");
                pProps->author = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=315}");
                pProps->copyright = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=33432}");
                pProps->lensModel = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=42036}");
            }
        }
    }

    static const wchar_t* PROPS_CLASS_NAME = L"MinimalImageViewerProperties";
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = PropsWndProc;
        wcex.hInstance = g_ctx.hInst;
        wcex.hIcon = LoadIcon(g_ctx.hInst, MAKEINTRESOURCE(IDI_APPICON));
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = PROPS_CLASS_NAME;
        if (RegisterClassExW(&wcex)) {
            classRegistered = true;
        }
    }

    RECT rcParent = {};
    GetWindowRect(g_ctx.hWnd, &rcParent);
    int wndWidth = 600;
    int wndHeight = 700;
    int xPos = rcParent.left + (rcParent.right - rcParent.left - wndWidth) / 2;
    int yPos = rcParent.top + (rcParent.bottom - rcParent.top - wndHeight) / 2;

    g_ctx.hPropsWnd = CreateWindowW(
        PROPS_CLASS_NAME,
        L"Image Properties",
        (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU) & ~WS_THICKFRAME,
        xPos, yPos, wndWidth, wndHeight,
        g_ctx.hWnd,
        nullptr,
        g_ctx.hInst,
        pProps
    );

    if (g_ctx.hPropsWnd) {
        ShowWindow(g_ctx.hPropsWnd, SW_SHOW);
    }
    else {
        delete pProps;
    }
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static POINT dragStart = {};

    switch (message) {
    case WM_APP_IMAGE_READY:
        OnImageReady(wParam != 0, (int)lParam);
        break;
    case WM_APP_DIR_READY:
        OnDirReady((int)lParam);
        break;
    case WM_APP_IMAGE_LOADED:
        FinalizeImageLoad(true, static_cast<int>(wParam));
        break;
    case WM_APP_IMAGE_LOAD_FAILED:
        KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
        if (lParam != 0) {
            if (g_ctx.loadSequenceId == (int)lParam) {
                FinalizeImageLoad(false, -1);
            }
        }
        else {
            FinalizeImageLoad(false, -1);
        }
        break;
    case WM_APP_OCR_DONE_TEXT:
        g_ctx.ocrMessage = L"Text copied to clipboard.";
        g_ctx.isOcrMessageVisible = true;
        g_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_DONE_AREA:
        g_ctx.ocrMessage = L"Text from selected area copied.";
        g_ctx.isOcrMessageVisible = true;
        g_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_DONE_NOTEXT:
        g_ctx.ocrMessage = (lParam == 1) ? L"No text found in selected area." : L"No text found on image.";
        g_ctx.isOcrMessageVisible = true;
        g_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_FAILED: {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        std::wstring errorMsg = L"An unknown error occurred during OCR.";
        switch (lParam) {
        case 1: errorMsg = L"OCR failed (hresult_error)."; break;
        case 2: errorMsg = L"An unknown error occurred during OCR."; break;
        case 3: errorMsg = L"Could not open clipboard."; break;
        case 4: errorMsg = L"Invalid selection area."; break;
        case 5: errorMsg = L"OCR engine not available."; break;
        }
        MessageBoxW(g_ctx.hWnd, errorMsg.c_str(), L"OCR Error", MB_OK | MB_ICONERROR);
        break;
    }
    case WM_TIMER:
        if (wParam == ANIMATION_TIMER_ID) {
            CriticalSectionLock lock(g_ctx.wicMutex);
            if (g_ctx.isAnimated && !g_ctx.animationFrameDelays.empty()) {
                g_ctx.currentAnimationFrame = (g_ctx.currentAnimationFrame + 1) % g_ctx.animationFrameDelays.size();
                InvalidateRect(hWnd, nullptr, FALSE);
                KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
                SetTimer(g_ctx.hWnd, ANIMATION_TIMER_ID, g_ctx.animationFrameDelays[g_ctx.currentAnimationFrame], nullptr);
            }
        }
        else if (wParam == OCR_MESSAGE_TIMER_ID) {
            ULONGLONG elapsedTime = GetTickCount64() - g_ctx.ocrMessageStartTime;
            if (elapsedTime > 1000) {
                g_ctx.isOcrMessageVisible = false;
                KillTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else if (wParam == AUTO_REFRESH_TIMER_ID) {
            if (g_ctx.isAutoRefresh && !g_ctx.isLoading && !g_ctx.imageFiles.empty() && g_ctx.currentImageIndex >= 0) {
                const std::wstring& currentFile = g_ctx.imageFiles[g_ctx.currentImageIndex];
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExW(currentFile.c_str(), GetFileExInfoStandard, &fad)) {
                    if (CompareFileTime(&fad.ftLastWriteTime, &g_ctx.lastWriteTime) > 0) {
                        // File has changed. Verify it's accessible (not locked by writer)
                        HANDLE hFile = CreateFileW(currentFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            CloseHandle(hFile);
                            g_ctx.preserveView = true;
                            LoadImageFromFile(currentFile);
                        }
                    }
                }
            }
        }
        break;
    case WM_PAINT:
        OnPaint(hWnd);
        break;
    case WM_KEYDOWN:
        OnKeyDown(wParam);
        break;
    case WM_KEYUP:
        break;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        ZoomImage(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.1f : 0.9f, pt);
        break;
    }
    case WM_LBUTTONDBLCLK:
        FitImageToWindow();
        break;
    case WM_RBUTTONUP: {
        if (g_ctx.isCropMode || g_ctx.isSelectingCropRect || g_ctx.isCropPending || g_ctx.isSelectingOcrRect) {
            g_ctx.isCropMode = false;
            g_ctx.isSelectingCropRect = false;
            g_ctx.isCropPending = false;
            g_ctx.isSelectingOcrRect = false;
            g_ctx.isDraggingOcrRect = false;
            g_ctx.ocrRectWindow = { 0 };

            // Revert cropped view if we cancel
            ApplyEffectsToView();
            FitImageToWindow();

            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            if (GetCapture() == hWnd) {
                ReleaseCapture();
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ClientToScreen(hWnd, &pt);
        OnContextMenu(hWnd, pt);
        break;
    }
    case WM_DROPFILES:
        HandleDropFiles(reinterpret_cast<HDROP>(wParam));
        break;
    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (g_ctx.isEyedropperActive) {
            HandleEyedropperClick();
            g_ctx.isEyedropperActive = false; // Turn off after click
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        if (g_ctx.isSelectingOcrRect) {
            g_ctx.isDraggingOcrRect = true;
            g_ctx.ocrStartPoint = pt;
            g_ctx.ocrRectWindow = D2D1::RectF(
                static_cast<float>(pt.x), static_cast<float>(pt.y),
                static_cast<float>(pt.x), static_cast<float>(pt.y)
            );
            SetCapture(hWnd);
        }
        else if (g_ctx.isCropMode) {
            g_ctx.isCropPending = false;
            g_ctx.isSelectingCropRect = true;
            g_ctx.cropStartPoint = pt;
            g_ctx.cropRectWindow = D2D1::RectF(
                static_cast<float>(pt.x), static_cast<float>(pt.y),
                static_cast<float>(pt.x), static_cast<float>(pt.y)
            );
            SetCapture(hWnd);
        }
        else {
            UINT w = 0, h = 0;
            if (GetCurrentImageSize(&w, &h) && IsPointInImage(pt, {})) {
                g_ctx.isDraggingImage = true;
                dragStart = pt;
                SetCapture(hWnd);
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            }
        }
        break;
    }
    case WM_LBUTTONUP:
        if (g_ctx.isSelectingOcrRect) {
            g_ctx.isSelectingOcrRect = false;
            g_ctx.isDraggingOcrRect = false;
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            ReleaseCapture();

            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            ConvertWindowToImagePoint(g_ctx.ocrStartPoint, x1, y1);
            POINT endPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ConvertWindowToImagePoint(endPoint, x2, y2);

            D2D1_RECT_F ocrRectLocal = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

            UINT imgWidth = 0, imgHeight = 0;
            GetCurrentImageSize(&imgWidth, &imgHeight);

            ocrRectLocal.left = std::max(0.0f, ocrRectLocal.left);
            ocrRectLocal.top = std::max(0.0f, ocrRectLocal.top);
            ocrRectLocal.right = std::min(static_cast<float>(imgWidth), ocrRectLocal.right);
            ocrRectLocal.bottom = std::min(static_cast<float>(imgHeight), ocrRectLocal.bottom);

            g_ctx.ocrRectWindow = { 0 };
            InvalidateRect(hWnd, nullptr, FALSE);

            if (ocrRectLocal.left < ocrRectLocal.right && ocrRectLocal.top < ocrRectLocal.bottom) {
                PerformOcrArea(ocrRectLocal);
            }
        }
        else if (g_ctx.isSelectingCropRect) {
            g_ctx.isSelectingCropRect = false;
            g_ctx.isCropMode = false;
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            ReleaseCapture();

            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            ConvertWindowToImagePoint(g_ctx.cropStartPoint, x1, y1);
            POINT endPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ConvertWindowToImagePoint(endPoint, x2, y2);

            g_ctx.cropRectLocal = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

            UINT imgWidth = 0, imgHeight = 0;
            GetCurrentImageSize(&imgWidth, &imgHeight);

            g_ctx.cropRectLocal.left = std::max(0.0f, g_ctx.cropRectLocal.left);
            g_ctx.cropRectLocal.top = std::max(0.0f, g_ctx.cropRectLocal.top);
            g_ctx.cropRectLocal.right = std::min(static_cast<float>(imgWidth), g_ctx.cropRectLocal.right);
            g_ctx.cropRectLocal.bottom = std::min(static_cast<float>(imgHeight), g_ctx.cropRectLocal.bottom);

            if (g_ctx.cropRectLocal.left < g_ctx.cropRectLocal.right && g_ctx.cropRectLocal.top < g_ctx.cropRectLocal.bottom) {
                g_ctx.isCropPending = true;
            }
            else {
                g_ctx.isCropPending = false;
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (g_ctx.isDraggingImage) {
            g_ctx.isDraggingImage = false;
            ReleaseCapture();
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_ctx.isEyedropperActive) {
            g_ctx.currentMousePos = pt;
            UpdateEyedropperColor(pt);
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        if (g_ctx.isDraggingOcrRect) {
            g_ctx.ocrRectWindow.right = static_cast<float>(pt.x);
            g_ctx.ocrRectWindow.bottom = static_cast<float>(pt.y);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (g_ctx.isSelectingCropRect) {
            g_ctx.cropRectWindow.right = static_cast<float>(pt.x);
            g_ctx.cropRectWindow.bottom = static_cast<float>(pt.y);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (g_ctx.isDraggingImage) {
            g_ctx.offsetX += (pt.x - dragStart.x);
            g_ctx.offsetY += (pt.y - dragStart.y);
            dragStart = pt;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            if (g_ctx.isEyedropperActive) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            if (g_ctx.isCropMode || g_ctx.isSelectingCropRect || g_ctx.isCropPending || g_ctx.isSelectingOcrRect) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            if (g_ctx.isDraggingImage) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    case WM_COPYDATA: {
        PCOPYDATASTRUCT pcds = reinterpret_cast<PCOPYDATASTRUCT>(lParam);
        if (pcds && pcds->dwData == 1) {
            wchar_t filePath[MAX_PATH];
            wcscpy_s(filePath, MAX_PATH, static_cast<wchar_t*>(pcds->lpData));
            PathUnquoteSpacesW(filePath);
            LoadImageFromFile(filePath);
        }
        return TRUE;
    }
    case WM_SIZE:
        if (g_ctx.renderTarget) {
            if (wParam == SIZE_MINIMIZED) {
                KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
            }
            else {
                g_ctx.renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
                if (g_ctx.isAnimated && !g_ctx.animationFrameDelays.empty()) {
                    KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
                    SetTimer(g_ctx.hWnd, ANIMATION_TIMER_ID, g_ctx.animationFrameDelays[g_ctx.currentAnimationFrame], nullptr);
                }
            }
        }
        if (wParam != SIZE_MINIMIZED) {
            if (!g_ctx.isLoading) {
                FitImageToWindow();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        break;
    case WM_DESTROY:
        KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
        KillTimer(g_ctx.hWnd, AUTO_REFRESH_TIMER_ID);
        if (g_ctx.hPropsWnd) {
            DestroyWindow(g_ctx.hPropsWnd);
        }
        if (!g_ctx.isFullScreen) {
            GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        }
        WriteSettings(g_ctx.settingsPath, g_ctx.savedRect, g_ctx.startFullScreen, g_ctx.enforceSingleInstance, g_ctx.alwaysOnTop);
        CleanupLoadingThread();
        DiscardDeviceResources();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void DeleteCurrentImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.imageFiles.empty()) return;

    std::wstring filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];
    std::wstring pathDoubleNull = filePath + L'\0';

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.hwnd = g_ctx.hWnd;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = pathDoubleNull.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;

    if (SHFileOperationW(&fileOp) == 0 && !fileOp.fAnyOperationsAborted) {
        g_ctx.imageFiles.erase(g_ctx.imageFiles.begin() + g_ctx.currentImageIndex);

        if (g_ctx.imageFiles.empty()) {
            g_ctx.currentImageIndex = -1;
            {
                CriticalSectionLock lock(g_ctx.wicMutex);
                g_ctx.wicConverter = nullptr;
                g_ctx.wicConverterOriginal = nullptr;
                g_ctx.d2dBitmap = nullptr;
                g_ctx.loadingFilePath = L"";
            }
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
            SetWindowTextW(g_ctx.hWnd, L"Minimal Image Viewer");
        }
        else {
            if (g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
                g_ctx.currentImageIndex = 0;
            }
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex]);
        }
    }
}

void HandleDropFiles(HDROP hDrop) {
    wchar_t filePath[MAX_PATH];
    if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) {
        LoadImageFromFile(filePath);
    }
    DragFinish(hDrop);
}

void HandleCopy() {
    if (g_ctx.loadingFilePath.empty()) return;

    if (OpenClipboard(g_ctx.hWnd)) {
        EmptyClipboard();

        size_t size = (g_ctx.loadingFilePath.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + size + sizeof(wchar_t));

        if (hMem) {
            BYTE* pData = (BYTE*)GlobalLock(hMem);
            if (pData) {
                DROPFILES* pDrop = (DROPFILES*)pData;
                pDrop->pFiles = sizeof(DROPFILES);
                pDrop->pt = { 0, 0 };
                pDrop->fNC = FALSE;
                pDrop->fWide = TRUE;

                wchar_t* pPath = (wchar_t*)(pData + sizeof(DROPFILES));
                wcscpy_s(pPath, g_ctx.loadingFilePath.length() + 1, g_ctx.loadingFilePath.c_str());

                GlobalUnlock(hMem);
                SetClipboardData(CF_HDROP, hMem);
            }
            else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }
}

void HandlePaste() {
    if (OpenClipboard(g_ctx.hWnd)) {
        if (IsClipboardFormatAvailable(CF_HDROP)) {
            HANDLE hData = GetClipboardData(CF_HDROP);
            if (hData) {
                HDROP hDrop = (HDROP)hData;
                wchar_t filePath[MAX_PATH];
                if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) {
                    LoadImageFromFile(filePath);
                }
            }
        }
        CloseClipboard();
    }
}

void OpenFileLocationAction() {
    if (g_ctx.loadingFilePath.empty()) return;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(g_ctx.loadingFilePath.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}