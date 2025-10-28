#include "viewer.h"

extern AppContext g_ctx;

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
    }
}

static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hWnd, &ps);
    Render();
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
    case VK_ESCAPE:
        if (g_ctx.isFullScreen) ToggleFullScreen();
        else PostQuitMessage(0);
        break;
    case 'O':      if (ctrlPressed) OpenFileAction(); break;
    case 'S':      if (ctrlPressed && (GetKeyState(VK_SHIFT) & 0x8000)) SaveImageAs(); else if (ctrlPressed) SaveImage(); break;
    case 'C':      if (ctrlPressed) HandleCopy(); break;
    case 'V':      if (ctrlPressed) HandlePaste(); break;
    case '0':      if (ctrlPressed) CenterImage(true); break;
    case VK_MULTIPLY: if (ctrlPressed) SetActualSize(); break;
    case VK_OEM_PLUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(1.25f, centerPt);
        }
        break;
    case VK_OEM_MINUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(0.8f, centerPt);
        }
        break;
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
    AppendMenuW(hMenu, MF_STRING, IDM_ACTUAL_SIZE, L"Actual Size (100%)\tCtrl+*");
    AppendMenuW(hMenu, MF_STRING, IDM_FIT_TO_WINDOW, L"Fit to Window\tCtrl+0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE, L"Save\tCtrl+S");
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE_AS, L"Save As\tCtrl+Shift+S");

    UINT locationFlags = (g_ctx.currentImageIndex != -1) ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(hMenu, locationFlags, IDM_OPEN_LOCATION, L"Open File Location");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    HMENU hBgMenu = CreatePopupMenu();
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::Grey ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_GREY, L"Grey (Default)");
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::Black ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_BLACK, L"Black");
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::White ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_WHITE, L"White");
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::Transparent ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_TRANSPARENT, L"Transparent (Checkerboard)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hBgMenu, L"Background Color");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (g_ctx.startFullScreen ? MF_CHECKED : MF_UNCHECKED), IDM_START_FULLSCREEN, L"Start full screen");

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
    case IDM_SAVE:          SaveImage(); break;
    case IDM_SAVE_AS:       SaveImageAs(); break;
    case IDM_OPEN_LOCATION: OpenFileLocationAction(); break;
    case IDM_BACKGROUND_GREY:
        g_ctx.bgColor = BackgroundColor::Grey;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_BACKGROUND_BLACK:
        g_ctx.bgColor = BackgroundColor::Black;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_BACKGROUND_WHITE:
        g_ctx.bgColor = BackgroundColor::White;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_BACKGROUND_TRANSPARENT:
        g_ctx.bgColor = BackgroundColor::Transparent;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_START_FULLSCREEN:
        g_ctx.startFullScreen = !g_ctx.startFullScreen;
        break;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static POINT dragStart{};

    switch (message) {
    case WM_APP_IMAGE_LOADED:
        FinalizeImageLoad(true, static_cast<int>(wParam));
        break;
    case WM_APP_IMAGE_LOAD_FAILED:
        FinalizeImageLoad(false, -1);
        break;
    case WM_PAINT:
        OnPaint(hWnd);
        break;
    case WM_KEYDOWN:
        OnKeyDown(wParam);
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
        if (g_ctx.wicConverter && IsPointInImage(pt, {})) {
            g_ctx.isDraggingImage = true;
            dragStart = pt;
            SetCapture(hWnd);
            SetCursor(LoadCursor(nullptr, IDC_HAND));
        }
        break;
    }
    case WM_LBUTTONUP:
        if (g_ctx.isDraggingImage) {
            g_ctx.isDraggingImage = false;
            ReleaseCapture();
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (g_ctx.isDraggingImage) {
            g_ctx.offsetX += (pt.x - dragStart.x);
            g_ctx.offsetY += (pt.y - dragStart.y);
            dragStart = pt;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
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
        if (g_ctx.renderTarget && wParam != SIZE_MINIMIZED) {
            g_ctx.renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        }
        if (wParam != SIZE_MINIMIZED) {
            FitImageToWindow();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_DESTROY:
        if (!g_ctx.isFullScreen) {
            GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        }
        WriteSettings(g_ctx.settingsPath, g_ctx.savedRect, g_ctx.startFullScreen);
        CleanupLoadingThread();
        DiscardDeviceResources();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}