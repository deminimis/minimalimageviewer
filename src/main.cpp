#include "viewer.h"

AppContext g_ctx;

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

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
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

    g_ctx.hInst = hInstance;
    ReadSettings();

    if (FAILED(CoInitialize(nullptr))) {
        MessageBoxW(nullptr, L"Failed to initialize COM.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_ctx.wicFactory)))) {
        MessageBoxW(nullptr, L"Failed to create WIC Imaging Factory.", L"Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wcex.lpszClassName = L"MinimalImageViewer";
    RegisterClassExW(&wcex);

    g_ctx.hWnd = CreateWindowW(
        wcex.lpszClassName,
        L"Minimal Image Viewer",
        WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_ctx.hWnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    SetWindowLongPtr(g_ctx.hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_ctx));
    DragAcceptFiles(g_ctx.hWnd, TRUE);

    if (g_ctx.startFullScreen) {
        void ToggleFullScreen();
        ToggleFullScreen();
    }

    ShowWindow(g_ctx.hWnd, nCmdShow);
    UpdateWindow(g_ctx.hWnd);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        LoadImageFromFile(argv[1]);
        GetImagesInDirectory(argv[1]);
    }
    if (argv) {
        LocalFree(argv);
    }

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_ctx.hBitmap) DeleteObject(g_ctx.hBitmap);
    g_ctx.wicFactory = nullptr;
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}