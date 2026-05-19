#include "viewer.h"

// define dark mode for older Windows
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

void ViewerApp::CenterImage(bool resetZoom) {
    if (resetZoom) {
        m_ctx.zoomFactor = 1.0f;
    }
    m_ctx.offsetX = 0.0f;
    m_ctx.offsetY = 0.0f;
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::SetActualSize() {
    m_ctx.zoomFactor = 1.0f;
    m_ctx.offsetX = 0.0f;
    m_ctx.offsetY = 0.0f;
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::SetZoomLevel(float zoom) {
    m_ctx.zoomFactor = zoom;
    m_ctx.offsetX = 0.0f;
    m_ctx.offsetY = 0.0f;
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::UpdateTitleBarTheme(HWND hWnd, BackgroundColor bgColor) {
    // black/grey background triggers dark mode
    BOOL useDarkMode = (bgColor == BackgroundColor::Black || bgColor == BackgroundColor::Grey) ? TRUE : FALSE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
}

int ViewerApp::Run(HINSTANCE hInstance, int nCmdShow, LPWSTR lpCmdLine) {
    m_ctx.hInst = hInstance;
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    PathAppendW(exePath, L"minimal_image_viewer_settings.ini");
    m_ctx.settingsPath = exePath;

    ReadSettings(m_ctx.settingsPath, m_ctx.windowPlacement, m_ctx.startFullScreen, m_ctx.enforceSingleInstance, m_ctx.alwaysOnTop);
    float sysDpiScale = GetDpiForSystem() / 96.0f;

    if (m_ctx.enforceSingleInstance) {
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

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        return 1;
    }
    wil::unique_couninitialize_call cleanupCOM;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_ctx.wicFactory)))) {
        MessageBoxW(nullptr, L"Failed to create WIC Imaging Factory.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), (void**)&m_ctx.d2dFactory))) {
        MessageBoxW(nullptr, L"Failed to create Direct2D Factory.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_ctx.writeFactory.GetAddressOf())))) {
        MessageBoxW(nullptr, L"Failed to create DirectWrite Factory.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = ViewerApp::StaticWndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = L"MinimalImageViewer";
    RegisterClassExW(&wcex);

    DWORD exStyle = (m_ctx.alwaysOnTop) ? WS_EX_TOPMOST : 0;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int w = static_cast<int>(800 * sysDpiScale);
    int h = static_cast<int>(600 * sysDpiScale);

    if (m_ctx.windowPlacement.rcNormalPosition.left != CW_USEDEFAULT) {
        x = m_ctx.windowPlacement.rcNormalPosition.left;
        y = m_ctx.windowPlacement.rcNormalPosition.top;
        w = m_ctx.windowPlacement.rcNormalPosition.right - m_ctx.windowPlacement.rcNormalPosition.left;
        h = m_ctx.windowPlacement.rcNormalPosition.bottom - m_ctx.windowPlacement.rcNormalPosition.top;
    }

    m_ctx.hWnd = CreateWindowExW(
        exStyle,
        wcex.lpszClassName,
        L"Minimal Image Viewer",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        nullptr, nullptr, hInstance, this
    );
    if (!m_ctx.hWnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    DragAcceptFiles(m_ctx.hWnd, TRUE);
    UpdateTitleBarTheme(m_ctx.hWnd, m_ctx.bgColor);

    m_ctx.isInitialized = true;

    if (m_ctx.windowPlacement.rcNormalPosition.left != CW_USEDEFAULT) {
        SetWindowPlacement(m_ctx.hWnd, &m_ctx.windowPlacement);
    }

    if (m_ctx.startFullScreen) {
        ToggleFullScreen();
    }
    else if (m_ctx.windowPlacement.rcNormalPosition.left == CW_USEDEFAULT) {
        ShowWindow(m_ctx.hWnd, nCmdShow);
    }

    Render();
    UpdateWindow(m_ctx.hWnd);

    if (lpCmdLine && *lpCmdLine) {
        wchar_t filePath[MAX_PATH];
        wcscpy_s(filePath, MAX_PATH, lpCmdLine);
        PathUnquoteSpacesW(filePath);
        LoadImageFromFile(filePath);
    }

    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);

    // Flush  working set to drop idle RAM 
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    UpdateAcceleratorTable();

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!m_ctx.hAccelTable || !TranslateAcceleratorW(m_ctx.hWnd, m_ctx.hAccelTable.get(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    m_ctx.isShuttingDown = true;
    CleanupLoadingThread();

    while (m_ctx.activeBackgroundThreads > 0) {
        Sleep(10);
    }

    m_ctx.wicConverter = nullptr;
    m_ctx.wicConverterOriginal = nullptr;
    m_ctx.undoStack.clear();
    m_ctx.d2dBitmap = nullptr;
    m_ctx.animationFrameConverters.clear();
    m_ctx.animationD2DBitmaps.clear();
    m_ctx.textBrush = nullptr;
    m_ctx.textFormat = nullptr;
    m_ctx.renderTarget = nullptr;
    m_ctx.writeFactory = nullptr;
    m_ctx.d2dFactory = nullptr;
    m_ctx.wicFactory = nullptr;

    return static_cast<int>(msg.wParam);
    }

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ViewerApp app;
    return app.Run(hInstance, nCmdShow, lpCmdLine);
}