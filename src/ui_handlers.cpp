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

void UpdateViewToCurrentFrame() {
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        if (g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size()) {
            g_ctx.wicConverterOriginal = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
        }
    }
    // Re-apply rotation, brightness, etc. to the new frame
    ApplyEffectsToView();
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

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
        // tiff support
        if (!g_ctx.isAnimated && g_ctx.animationFrameConverters.size() > 1 &&
            g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size() - 1) {
            g_ctx.currentAnimationFrame++;
            UpdateViewToCurrentFrame();
        }
        else if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex + 1) % static_cast<int>(size);
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    case VK_LEFT:
        // tiff support
        if (!g_ctx.isAnimated && g_ctx.animationFrameConverters.size() > 1 &&
            g_ctx.currentAnimationFrame > 0) {
            g_ctx.currentAnimationFrame--;
            UpdateViewToCurrentFrame();
        }
        else if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex - 1 + static_cast<int>(size)) % static_cast<int>(size);
            // Pass true to open at the last frame of the previous image
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str(), true);
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

            // revert any pending views
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

            // refresh view to show full image if we just toggled crop mode
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

            // apply crop to the actual view
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

        // reset full view when starting new crop
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
                        // verify file is not locked
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

            // revert cropped view if canceled
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
            g_ctx.isEyedropperActive = false;
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
        else if (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB)) {
            HBITMAP hBitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
            if (hBitmap) {
                CriticalSectionLock lock(g_ctx.wicMutex);

                ComPtr<IWICBitmap> wicBitmap;
                HRESULT hr = g_ctx.wicFactory->CreateBitmapFromHBITMAP(hBitmap, NULL, WICBitmapUseAlpha, &wicBitmap);

                if (SUCCEEDED(hr)) {
                    ComPtr<IWICFormatConverter> converter;
                    hr = g_ctx.wicFactory->CreateFormatConverter(&converter);

                    if (SUCCEEDED(hr)) {
                        hr = converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);

                        if (SUCCEEDED(hr)) {
                            // reset state for new pasted image
                            g_ctx.wicConverter = converter;
                            g_ctx.wicConverterOriginal = converter;
                            g_ctx.d2dBitmap = nullptr;
                            g_ctx.animationD2DBitmaps.clear();
                            g_ctx.animationFrameConverters.clear();
                            g_ctx.animationFrameDelays.clear();
                            g_ctx.isAnimated = false;

                            // clear file context
                            g_ctx.imageFiles.clear();
                            g_ctx.currentImageIndex = -1;
                            g_ctx.currentDirectory = L"";
                            g_ctx.loadingFilePath = L"Clipboard Image";
                            g_ctx.originalContainerFormat = GUID_ContainerFormatPng;

                            g_ctx.zoomFactor = 1.0f;
                            g_ctx.offsetX = 0;
                            g_ctx.offsetY = 0;

                            // stop animations
                            KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);

                            SetWindowTextW(g_ctx.hWnd, L"Clipboard Image");
                            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
                        }
                    }
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