#include "viewer.h"

AppContext g_ctx;

void CleanupPreloadingThreads() {
    if (g_ctx.preloadingNextThread.joinable()) {
        g_ctx.preloadingNextThread.join();
    }
    if (g_ctx.preloadingPrevThread.joinable()) {
        g_ctx.preloadingPrevThread.join();
    }
}

void CleanupLoadingThread() {
    g_ctx.cancelPreloading = true;
    CleanupPreloadingThreads();
    if (g_ctx.loadingThread.joinable()) {
        g_ctx.loadingThread.join();
    }
    g_ctx.cancelPreloading = false;
    KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
}

void CenterImage(bool resetZoom) {
    if (resetZoom) {
        g_ctx.zoomFactor = 1.0f;
    }
    g_ctx.rotationAngle = 0;
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    FitImageToWindow();
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void SetActualSize() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;
    g_ctx.zoomFactor = 1.0f;
    g_ctx.rotationAngle = 0;
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    g_ctx.hInst = hInstance;

    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"settings.ini");
    g_ctx.settingsPath = exePath;

    RECT startupRect;
    ReadSettings(g_ctx.settingsPath, startupRect, g_ctx.startFullScreen, g_ctx.enforceSingleInstance);
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

    if (FAILED(CoInitialize(nullptr))) {
        MessageBoxW(nullptr, L"Failed to initialize COM.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_ctx.wicFactory)))) {
        MessageBoxW(nullptr, L"Failed to create WIC Imaging Factory.", L"Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_ctx.d2dFactory))) {
        MessageBoxW(nullptr, L"Failed to create Direct2D Factory.", L"Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_ctx.writeFactory)))) {
        MessageBoxW(nullptr, L"Failed to create DirectWrite Factory.", L"Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
    wcex.lpszClassName = L"MinimalImageViewer";
    RegisterClassExW(&wcex);

    g_ctx.hWnd = CreateWindowW(
        wcex.lpszClassName,
        L"Minimal Image Viewer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        startupRect.left, startupRect.top,
        (startupRect.left == CW_USEDEFAULT) ? 800 : (startupRect.right - startupRect.left),
        (startupRect.top == CW_USEDEFAULT) ? 600 : (startupRect.bottom - startupRect.top),
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_ctx.hWnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    SetWindowLongPtr(g_ctx.hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_ctx));
    DragAcceptFiles(g_ctx.hWnd, TRUE);

    if (g_ctx.startFullScreen) {
        ToggleFullScreen();
    }

    ShowWindow(g_ctx.hWnd, nCmdShow);
    UpdateWindow(g_ctx.hWnd);

    if (lpCmdLine && *lpCmdLine) {
        wchar_t filePath[MAX_PATH];
        wcscpy_s(filePath, MAX_PATH, lpCmdLine);
        PathUnquoteSpacesW(filePath);
        LoadImageFromFile(filePath);
    }

    g_ctx.isInitialized = true;

    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupLoadingThread();
    g_ctx.wicConverter = nullptr;
    g_ctx.d2dBitmap = nullptr;
    g_ctx.animationFrameConverters.clear();
    g_ctx.animationD2DBitmaps.clear();
    g_ctx.textBrush = nullptr;
    g_ctx.textFormat = nullptr;
    g_ctx.renderTarget = nullptr;
    g_ctx.writeFactory = nullptr;
    g_ctx.d2dFactory = nullptr;
    g_ctx.wicFactory = nullptr;
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}