#include "viewer.h"

extern AppContext g_ctx;

static RECT GetCloseButtonRect() {
    RECT clientRect{};
    GetClientRect(g_ctx.hWnd, &clientRect);
    return { clientRect.right - 40, 0, clientRect.right, 30 };
}

void ToggleFullScreen() {
    if (!g_ctx.isFullScreen) {
        g_ctx.savedStyle = GetWindowLong(g_ctx.hWnd, GWL_STYLE);
        GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        HMONITOR hMonitor = MonitorFromWindow(g_ctx.hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_ctx.isFullScreen = true;
    }
    else {
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, g_ctx.savedStyle | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, HWND_NOTOPMOST, g_ctx.savedRect.left, g_ctx.savedRect.top,
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
        GetImagesInDirectory(szFile);
    }
}

static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hWnd, &ps);
    RECT clientRect{};
    GetClientRect(hWnd, &clientRect);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

    FillRect(memDC, &clientRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    if (g_ctx.hBitmap && !IsIconic(hWnd)) {
        DrawImage(memDC, clientRect, g_ctx);
    }
    else if (!g_ctx.hBitmap) {
        SetTextColor(memDC, RGB(255, 255, 255));
        SetBkMode(memDC, TRANSPARENT);
        DrawTextW(memDC, L"Right-click for options or drag an image here", -1, &clientRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_ctx.showFilePath) {
        std::wstring pathToDisplay;
        if (!g_ctx.currentFilePathOverride.empty()) {
            pathToDisplay = g_ctx.currentFilePathOverride;
        }
        else if (g_ctx.currentImageIndex >= 0 && g_ctx.currentImageIndex < static_cast<int>(g_ctx.imageFiles.size())) {
            pathToDisplay = g_ctx.imageFiles[g_ctx.currentImageIndex];
        }

        if (!pathToDisplay.empty()) {
            SetBkMode(memDC, TRANSPARENT);
            HFONT hPathFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HFONT hOldPathFont = static_cast<HFONT>(SelectObject(memDC, hPathFont));

            RECT textRect = clientRect;
            textRect.bottom -= 5;
            textRect.right -= 5;

            RECT shadowRect = textRect;
            OffsetRect(&shadowRect, 1, 1);
            SetTextColor(memDC, RGB(0, 0, 0));
            DrawTextW(memDC, pathToDisplay.c_str(), -1, &shadowRect, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            SetTextColor(memDC, RGB(220, 220, 220));
            DrawTextW(memDC, pathToDisplay.c_str(), -1, &textRect, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            SelectObject(memDC, hOldPathFont);
            DeleteObject(hPathFont);
        }
    }

    RECT closeRect = GetCloseButtonRect();
    if (g_ctx.isHoveringClose) {
        HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50));
        FillRect(memDC, &closeRect, hBrush);
        DeleteObject(hBrush);
    }

    HPEN hPen;
    if (g_ctx.isHoveringClose) {
        hPen = CreatePen(PS_SOLID, 2, RGB(230, 80, 80));
    }
    else {
        hPen = CreatePen(PS_SOLID, 2, RGB(120, 120, 120));
    }
    HPEN hOldPen = static_cast<HPEN>(SelectObject(memDC, hPen));

    int x_center = closeRect.left + (closeRect.right - closeRect.left) / 2;
    int y_center = closeRect.top + (closeRect.bottom - closeRect.top) / 2;
    int size = 5;

    MoveToEx(memDC, x_center - size, y_center - size, nullptr);
    LineTo(memDC, x_center + size, y_center + size);
    MoveToEx(memDC, x_center + size, y_center - size, nullptr);
    LineTo(memDC, x_center - size, y_center + size);

    SelectObject(memDC, hOldPen);
    DeleteObject(hPen);

    BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
        ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
        memDC, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hWnd, &ps);
}

static void OnKeyDown(WPARAM wParam) {
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    switch (wParam) {
    case VK_RIGHT:
        if (!g_ctx.imageFiles.empty()) {
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex + 1) % g_ctx.imageFiles.size();
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    case VK_LEFT:
        if (!g_ctx.imageFiles.empty()) {
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex - 1 + g_ctx.imageFiles.size()) % g_ctx.imageFiles.size();
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    case VK_UP:    RotateImage(true); break;
    case VK_DOWN:  RotateImage(false); break;
    case VK_DELETE: DeleteCurrentImage(); break;
    case VK_F11:   ToggleFullScreen(); break;
    case VK_ESCAPE: PostQuitMessage(0); break;
    case 'O':      if (ctrlPressed) OpenFileAction(); break;
    case 'S':      if (ctrlPressed && (GetKeyState(VK_SHIFT) & 0x8000)) SaveImageAs(); else if (ctrlPressed) SaveImage(); break;
    case 'C':      if (ctrlPressed) HandleCopy(); break;
    case 'V':      if (ctrlPressed) HandlePaste(); break;
    case '0':      if (ctrlPressed) CenterImage(true); break;
    case VK_OEM_PLUS:  if (ctrlPressed) ZoomImage(1.25f); break;
    case VK_OEM_MINUS: if (ctrlPressed) ZoomImage(0.8f); break;
    }
}

static void OnContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN, L"Open Image\tCtrl+O");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_COPY, L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_NEXT_IMG, L"Next Image\tRight Arrow");
    AppendMenuW(hMenu, MF_STRING, IDM_PREV_IMG, L"Previous Image\tLeft Arrow");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_ROTATE_CW, L"Rotate Clockwise\tUp Arrow");
    AppendMenuW(hMenu, MF_STRING, IDM_ROTATE_CCW, L"Rotate Counter-Clockwise\tDown Arrow");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_IN, L"Zoom In\tCtrl++");
    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_OUT, L"Zoom Out\tCtrl+-");
    AppendMenuW(hMenu, MF_STRING, IDM_FIT_TO_WINDOW, L"Fit to Window\tCtrl+0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE, L"Save\tCtrl+S");
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE_AS, L"Save As\tCtrl+Shift+S");

    UINT locationFlags = (g_ctx.currentImageIndex != -1) ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(hMenu, locationFlags, IDM_OPEN_LOCATION, L"Open File Location");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (g_ctx.showFilePath ? MF_CHECKED : MF_UNCHECKED), IDM_SHOW_FILE_PATH, L"Show File Path");
    AppendMenuW(hMenu, MF_STRING | (g_ctx.startFullScreen ? MF_CHECKED : MF_UNCHECKED), IDM_START_FULLSCREEN, L"Start full screen");

    HMENU hSubMenu = CreatePopupMenu();
    if (IsAppRegistered()) {
        AppendMenuW(hSubMenu, MF_STRING | MF_GRAYED, IDM_REGISTER_APP, L"Set as default viewer");
        AppendMenuW(hSubMenu, MF_STRING, IDM_UNREGISTER_APP, L"Remove default viewer");
    }
    else {
        AppendMenuW(hSubMenu, MF_STRING, IDM_REGISTER_APP, L"Set as default viewer");
        AppendMenuW(hSubMenu, MF_STRING | MF_GRAYED, IDM_UNREGISTER_APP, L"Remove default viewer");
    }
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"File Associations");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_FULLSCREEN, L"Full Screen\tF11");
    AppendMenuW(hMenu, MF_STRING, IDM_DELETE_IMG, L"Delete Image\tDelete");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit\tEsc");

    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_OPEN:          OpenFileAction(); break;
    case IDM_COPY:          HandleCopy(); break;
    case IDM_PASTE:         HandlePaste(); break;
    case IDM_NEXT_IMG:      OnKeyDown(VK_RIGHT); break;
    case IDM_PREV_IMG:      OnKeyDown(VK_LEFT); break;
    case IDM_ZOOM_IN:       ZoomImage(1.25f); break;
    case IDM_ZOOM_OUT:      ZoomImage(0.8f); break;
    case IDM_FIT_TO_WINDOW: FitImageToWindow(); break;
    case IDM_FULLSCREEN:    ToggleFullScreen(); break;
    case IDM_DELETE_IMG:    DeleteCurrentImage(); break;
    case IDM_EXIT:          PostQuitMessage(0); break;
    case IDM_ROTATE_CW:     RotateImage(true); break;
    case IDM_ROTATE_CCW:    RotateImage(false); break;
    case IDM_SAVE:          SaveImage(); break;
    case IDM_SAVE_AS:       SaveImageAs(); break;
    case IDM_OPEN_LOCATION: OpenFileLocationAction(); break;
    case IDM_SHOW_FILE_PATH:
        g_ctx.showFilePath = !g_ctx.showFilePath;
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    case IDM_START_FULLSCREEN:
        g_ctx.startFullScreen = !g_ctx.startFullScreen;
        WriteSettings();
        break;
    case IDM_REGISTER_APP:
        RegisterApp();
        break;
    case IDM_UNREGISTER_APP:
        UnregisterApp();
        break;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static bool isDraggingImage = false;
    static POINT dragStart{};

    switch (message) {
    case WM_PAINT:
        OnPaint(hWnd);
        break;
    case WM_KEYDOWN:
        OnKeyDown(wParam);
        break;
    case WM_MOUSEWHEEL:
        ZoomImage(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.1f : 0.9f);
        break;
    case WM_LBUTTONDBLCLK:
        FitImageToWindow();
        break;
    case WM_RBUTTONUP: {
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
        RECT closeRect = GetCloseButtonRect();
        if (PtInRect(&closeRect, pt)) {
            PostQuitMessage(0);
            return 0;
        }
        if (g_ctx.hBitmap && IsPointInImage(pt, {})) {
            isDraggingImage = true;
            dragStart = pt;
            SetCapture(hWnd);
        }
        else if (!g_ctx.isFullScreen) {
            ReleaseCapture();
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        break;
    }
    case WM_LBUTTONUP:
        if (isDraggingImage) {
            isDraggingImage = false;
            ReleaseCapture();
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT closeRect = GetCloseButtonRect();
        bool isHoveringNow = PtInRect(&closeRect, pt);
        if (isHoveringNow != g_ctx.isHoveringClose) {
            g_ctx.isHoveringClose = isHoveringNow;
            InvalidateRect(hWnd, &closeRect, FALSE);
            SendMessage(hWnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hWnd), MAKELPARAM(HTCLIENT, 0));
        }
        if (isDraggingImage) {
            g_ctx.offsetX += (pt.x - dragStart.x) / g_ctx.zoomFactor;
            g_ctx.offsetY += (pt.y - dragStart.y) / g_ctx.zoomFactor;
            dragStart = pt;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT && g_ctx.isHoveringClose) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    case WM_COPYDATA: {
        PCOPYDATASTRUCT pcds = reinterpret_cast<PCOPYDATASTRUCT>(lParam);
        if (pcds && pcds->dwData == 1) {
            LoadImageFromFile(static_cast<wchar_t*>(pcds->lpData));
            GetImagesInDirectory(static_cast<wchar_t*>(pcds->lpData));
        }
        return TRUE;
    }
    case WM_SIZE:
        FitImageToWindow();
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}