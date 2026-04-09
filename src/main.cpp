#include "viewer.h"

AppContext g_ctx;

static void ResetImageState() {
    g_ctx.rotationAngle = 0;
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    g_ctx.isFlippedHorizontal = false;
    switch (g_ctx.currentOrientation) {
    case 2: g_ctx.isFlippedHorizontal = true; break;
    case 3: g_ctx.rotationAngle = 180; break;
    case 4: g_ctx.isFlippedHorizontal = true; g_ctx.rotationAngle = 180; break;
    case 5: g_ctx.isFlippedHorizontal = true; g_ctx.rotationAngle = 270; break;
    case 6: g_ctx.rotationAngle = 90; break;
    case 7: g_ctx.isFlippedHorizontal = true; g_ctx.rotationAngle = 90; break;
    case 8: g_ctx.rotationAngle = 270; break;
    }

    g_ctx.isGrayscale = false;
    g_ctx.isCropActive = false;
    g_ctx.isCropMode = false;
    g_ctx.isSelectingCropRect = false;
    g_ctx.isCropPending = false;
    g_ctx.brightness = 0.0f;
    g_ctx.contrast = 1.0f;
    g_ctx.saturation = 1.0f;
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.wicConverter = g_ctx.wicConverterOriginal;
        g_ctx.d2dBitmap = nullptr;
        g_ctx.animationD2DBitmaps.clear();
    }
}

void CenterImage(bool resetZoom) {
    if (resetZoom) g_ctx.zoomFactor = 1.0f;
    ResetImageState();
    FitImageToWindow();
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void SetActualSize() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;
    g_ctx.zoomFactor = 1.0f;
    ResetImageState();
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

// Try dark mode menus
enum PreferredAppMode { AppModeDefault = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3, MaxAppMode = 4 };
using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
using fnFlushMenuThemes = void(WINAPI*)();

void UpdateTitleBarTheme(HWND hWnd, BackgroundColor bgColor) {
    // dark mode for black/grey backgrounds
    BOOL useDarkMode = (bgColor == BackgroundColor::Black || bgColor == BackgroundColor::Grey) ? TRUE : FALSE;

    // title bar dark: 20 for W11; 19 for older
    if (FAILED(DwmSetWindowAttribute(hWnd, 20, &useDarkMode, sizeof(useDarkMode)))) {
        DwmSetWindowAttribute(hWnd, 19, &useDarkMode, sizeof(useDarkMode));
    }

    // dark mode context menu for newer Windows (1903+)
    HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!hUxtheme) {
        hUxtheme = LoadLibraryW(L"uxtheme.dll");
    }

    if (hUxtheme) {
        // Fetch  ordinals 135 and 136
        fnSetPreferredAppMode pSetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        fnFlushMenuThemes pFlushMenuThemes = (fnFlushMenuThemes)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));

        if (pSetPreferredAppMode && pFlushMenuThemes) {
            // Force the app mode to Dark or Light
            pSetPreferredAppMode(useDarkMode ? ForceDark : ForceLight);
            // apply instantly without restart
            pFlushMenuThemes();
        }
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    g_ctx.hInst = hInstance;

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"minimal_image_viewer_settings.ini");
    g_ctx.settingsPath = exePath;

    RECT startupRect;
    ReadSettings(g_ctx.settingsPath, startupRect, g_ctx.startFullScreen, g_ctx.enforceSingleInstance, g_ctx.alwaysOnTop);
    if (IsRectEmpty(&startupRect)) {
        startupRect = { CW_USEDEFAULT, CW_USEDEFAULT, 800, 600 };
    }

    if (g_ctx.enforceSingleInstance) {
        HWND existingWnd = FindWindowW(L"MinimalImageViewer", nullptr);
        if (existingWnd) {
            SetForegroundWindow(existingWnd);
            if (IsIconic(existingWnd)) {
                ShowWindow(existingWnd, SW_RESTORE);
            }
            if (lpCmdLine && *lpCmdLine) {
                COPYDATASTRUCT cds{};
                cds.dwData = 1;
                cds.cbData = (static_cast<DWORD>(wcslen(lpCmdLine)) + 1) * sizeof(wchar_t);
                cds.lpData = lpCmdLine;
                SendMessage(existingWnd, WM_COPYDATA, reinterpret_cast<WPARAM>(hInstance), reinterpret_cast<LPARAM>(&cds));
            }
            return 0;
        }
    }

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    InitializeCriticalSection(&g_ctx.wicMutex);
    InitializeCriticalSection(&g_ctx.preloadMutex);

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_ctx.wicFactory)))) {
        MessageBoxW(nullptr, L"Failed to create WIC Imaging Factory.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_ctx.d2dFactory))) {
        MessageBoxW(nullptr, L"Failed to create Direct2D Factory.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_ctx.writeFactory)))) {
        MessageBoxW(nullptr, L"Failed to create DirectWrite Factory.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = L"MinimalImageViewer";
    RegisterClassExW(&wcex);

    DWORD exStyle = (g_ctx.alwaysOnTop) ? WS_EX_TOPMOST : 0;

    g_ctx.hWnd = CreateWindowExW(
        exStyle,
        wcex.lpszClassName,
        L"Minimal Image Viewer",
        WS_OVERLAPPEDWINDOW,
        startupRect.left, startupRect.top,
        (startupRect.left == CW_USEDEFAULT) ? 800 : (startupRect.right - startupRect.left),
        (startupRect.top == CW_USEDEFAULT) ? 600 : (startupRect.bottom - startupRect.top),
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_ctx.hWnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_ctx.wicMutex);
        DeleteCriticalSection(&g_ctx.preloadMutex);
        winrt::uninit_apartment();
        return 1;
    }

    SetWindowLongPtr(g_ctx.hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_ctx));
    DragAcceptFiles(g_ctx.hWnd, TRUE);
    UpdateTitleBarTheme(g_ctx.hWnd, g_ctx.bgColor);

    g_ctx.isInitialized = true; 

    if (g_ctx.startFullScreen) {
        ToggleFullScreen();
    }

    Render();

    ShowWindow(g_ctx.hWnd, nCmdShow);
    UpdateWindow(g_ctx.hWnd);

    if (lpCmdLine && *lpCmdLine) {
        wchar_t filePath[MAX_PATH];
        wcscpy_s(filePath, MAX_PATH, lpCmdLine);
        PathUnquoteSpacesW(filePath);
        LoadImageFromFile(filePath);
    }

    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupLoadingThread();
    g_ctx.wicConverter = nullptr;
    g_ctx.wicConverterOriginal = nullptr;
    g_ctx.d2dBitmap = nullptr;
    g_ctx.animationFrameConverters.clear();
    g_ctx.animationD2DBitmaps.clear();
    g_ctx.textBrush = nullptr;
    g_ctx.textFormat = nullptr;
    g_ctx.renderTarget = nullptr;
    g_ctx.writeFactory = nullptr;
    g_ctx.d2dFactory = nullptr;
    g_ctx.wicFactory = nullptr;

    DeleteCriticalSection(&g_ctx.wicMutex);
    DeleteCriticalSection(&g_ctx.preloadMutex);
    winrt::uninit_apartment();
    return static_cast<int>(msg.wParam);
}