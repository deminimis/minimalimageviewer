#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <shellapi.h>
#include <propvarutil.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <fstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "oleaut32.lib")

// Global variables
HINSTANCE hInst;
HWND hWnd;
HBITMAP hBitmap = nullptr;
std::vector<std::wstring> imageFiles;
int currentImageIndex = -1;
float zoomFactor = 1.0f;
int rotationAngle = 0;
bool isFullScreen = false;
IWICImagingFactory* wicFactory = nullptr;
LONG savedStyle = 0;
RECT savedRect = {0};
bool isDragging = false;

// Function declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void LoadImage(const wchar_t* filePath);
void DrawImage(HDC hdc, RECT clientRect);
void GetImagesInDirectory(const wchar_t* filePath);
void NextImage();
void PreviousImage();
void ZoomImage(float delta);
void FitImageToWindow(RECT clientRect);
void ToggleFullScreen();
void DeleteCurrentImage();
void RotateImage(bool clockwise);
void SaveImage();
void LogDebugMessage(const std::wstring& message);

void LogDebugMessage(const std::wstring& message) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    std::wstring logPath = std::wstring(exePath) + L"\\debug_log.txt";

    std::wofstream logFile(logPath, std::ios::app);
    if (logFile.is_open()) {
        logFile << message << L"\n";
        logFile.close();
    } else {
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        std::wstring tempLogPath = std::wstring(tempPath) + L"MinimalImageViewer_debug_log.txt";
        std::wofstream tempLogFile(tempLogPath, std::ios::app);
        if (tempLogFile.is_open()) {
            tempLogFile << L"Failed to write to " << logPath << L". Writing to temp: " << message << L"\n";
            tempLogFile.close();
        }
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    std::wstring cmdLine = lpCmdLine ? lpCmdLine : L"";
    LogDebugMessage(L"Raw lpCmdLine: [" + cmdLine + L"]");
    LPWSTR fullCmdLine = GetCommandLineW();
    LogDebugMessage(L"Full GetCommandLineW: [" + std::wstring(fullCmdLine) + L"]");

    HWND existingWnd = FindWindowW(L"MinimalImageViewer", nullptr);
    if (existingWnd) {
        SetForegroundWindow(existingWnd);
        if (IsIconic(existingWnd)) {
            ShowWindow(existingWnd, SW_RESTORE);
        }
        if (!cmdLine.empty()) {
            COPYDATASTRUCT cds;
            cds.dwData = 1;
            cds.cbData = (DWORD)(cmdLine.length() + 1) * sizeof(wchar_t);
            cds.lpData = (PVOID)cmdLine.c_str();
            SendMessage(existingWnd, WM_COPYDATA, (WPARAM)hInstance, (LPARAM)&cds);
        }
        return 0;
    }

    hInst = hInstance;

    CoInitialize(nullptr);
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        LogDebugMessage(L"CoCreateInstance for WIC failed, HRESULT: " + std::to_wstring(hr));
        MessageBoxW(nullptr, L"Failed to initialize WIC.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(100));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wcex.lpszClassName = L"MinimalImageViewer";
    RegisterClassExW(&wcex);

    hWnd = CreateWindowW(L"MinimalImageViewer", nullptr, WS_POPUP | WS_VISIBLE,
                         CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                         nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) {
        LogDebugMessage(L"CreateWindowW failed, error: " + std::to_wstring(GetLastError()));
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    int argc;
    LPWSTR* argv = CommandLineToArgvW(fullCmdLine, &argc);
    std::wstring filePath;
    if (argc > 1) {
        filePath = argv[1];
        LogDebugMessage(L"Parsed file path from argv[1]: " + filePath);
    } else if (!cmdLine.empty()) {
        filePath = cmdLine;
        LogDebugMessage(L"Using lpCmdLine as file path: " + filePath);
    }
    if (argv) LocalFree(argv);

    if (!filePath.empty()) {
        if (filePath.front() == L'"' && filePath.back() == L'"') {
            filePath = filePath.substr(1, filePath.length() - 2);
        }
        filePath.erase(0, filePath.find_first_not_of(L" \t\r\n"));
        filePath.erase(filePath.find_last_not_of(L" \t\r\n") + 1);
        LogDebugMessage(L"Trimmed file path: " + filePath);

        if (filePath.find(L"file:///") == 0) {
            filePath = filePath.substr(8);
            std::replace(filePath.begin(), filePath.end(), L'/', L'\\');
            LogDebugMessage(L"After URI processing: " + filePath);
        }

        wchar_t longPath[MAX_PATH];
        DWORD result = GetLongPathNameW(filePath.c_str(), longPath, MAX_PATH);
        if (result == 0 || result > MAX_PATH) {
            LogDebugMessage(L"GetLongPathNameW failed, error: " + std::to_wstring(GetLastError()));
            wcscpy_s(longPath, MAX_PATH, filePath.c_str());
        } else {
            filePath = longPath;
        }
        LogDebugMessage(L"After GetLongPathNameW: " + filePath);

        wchar_t fullPath[MAX_PATH];
        if (PathIsRelativeW(filePath.c_str())) {
            wchar_t currentDir[MAX_PATH];
            GetCurrentDirectoryW(MAX_PATH, currentDir);
            PathCombineW(fullPath, currentDir, filePath.c_str());
        } else {
            wcscpy_s(fullPath, MAX_PATH, filePath.c_str());
        }
        LogDebugMessage(L"Final file path: " + std::wstring(fullPath));

        DWORD fileAttributes = GetFileAttributesW(fullPath);
        if (fileAttributes != INVALID_FILE_ATTRIBUTES && !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            HANDLE hFile = CreateFileW(fullPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                CloseHandle(hFile);
                LoadImage(fullPath);
                GetImagesInDirectory(fullPath);
                RECT clientRect;
                GetClientRect(hWnd, &clientRect);
                FitImageToWindow(clientRect);
            } else {
                DWORD error = GetLastError();
                LogDebugMessage(L"CreateFileW failed, error: " + std::to_wstring(error));
                std::wstring errorMsg = L"Cannot access file: " + std::wstring(fullPath);
                MessageBoxW(hWnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
            }
        } else {
            LogDebugMessage(L"Invalid file attributes: " + std::to_wstring(fileAttributes));
            std::wstring errorMsg = L"Invalid or inaccessible file: " + std::wstring(fullPath);
            MessageBoxW(hWnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        }
    } else {
        LogDebugMessage(L"No valid file path provided.");
        MessageBoxW(hWnd, L"No image file specified. Use Ctrl+O to open an image.", L"Error", MB_OK | MB_ICONINFORMATION);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hBitmap) DeleteObject(hBitmap);
    if (wicFactory) wicFactory->Release();
    CoUninitialize();
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static bool isResizing = false;
    static POINT resizeStart;
    static RECT resizeRect;
    static int resizeEdge;

    switch (message) {
    case WM_COPYDATA: {
        PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;
        if (pcds->dwData == 1) {
            wchar_t* filePath = (wchar_t*)pcds->lpData;
            LogDebugMessage(L"WM_COPYDATA filePath: " + std::wstring(filePath));
            LoadImage(filePath);
            GetImagesInDirectory(filePath);
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            FitImageToWindow(clientRect);
        }
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        FillRect(memDC, &clientRect, CreateSolidBrush(RGB(0, 0, 0)));
        if (hBitmap) {
            DrawImage(memDC, clientRect);
        } else {
            SetTextColor(memDC, RGB(255, 255, 255));
            SetBkMode(memDC, TRANSPARENT);
            HFONT hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
            const wchar_t* text = L"Right click to see hotkeys";
            DrawTextW(memDC, text, -1, &clientRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(memDC, oldFont);
            DeleteObject(hFont);
        }

        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_KEYDOWN: {
        bool ctrlPressed = GetKeyState(VK_CONTROL) & 0x8000;
        switch (wParam) {
        case VK_RIGHT: NextImage(); break;
        case VK_LEFT: PreviousImage(); break;
        case VK_UP: RotateImage(true); break;
        case VK_DOWN: RotateImage(false); break;
        case VK_DELETE: DeleteCurrentImage(); break;
        case VK_F11: ToggleFullScreen(); break;
        case VK_ESCAPE: PostQuitMessage(0); break;
        case 'O':
            if (ctrlPressed) {
                wchar_t szFile[MAX_PATH] = {0};
                OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
                ofn.hwndOwner = hWnd;
                ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2\0All Files\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    LogDebugMessage(L"Opening via Ctrl+O: " + std::wstring(szFile));
                    LoadImage(szFile);
                    GetImagesInDirectory(szFile);
                    RECT clientRect;
                    GetClientRect(hWnd, &clientRect);
                    FitImageToWindow(clientRect);
                }
            }
            break;
        case 'S':
            if (ctrlPressed) SaveImage();
            break;
        case VK_OEM_PLUS:
            if (ctrlPressed) ZoomImage(1.1f);
            break;
        case VK_OEM_MINUS:
            if (ctrlPressed) ZoomImage(0.9f);
            break;
        case '0':
            if (ctrlPressed) {
                RECT clientRect;
                GetClientRect(hWnd, &clientRect);
                FitImageToWindow(clientRect);
            }
            break;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        ZoomImage(delta > 0 ? 1.1f : 0.9f);
        break;
    }
    case WM_LBUTTONDBLCLK: {
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        FitImageToWindow(clientRect);
        break;
    }
    case WM_RBUTTONDOWN: {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1001, L"Open Image (Ctrl+O)");
        AppendMenuW(hMenu, MF_STRING, 1002, L"Next Image (Right Arrow)");
        AppendMenuW(hMenu, MF_STRING, 1003, L"Previous Image (Left Arrow)");
        AppendMenuW(hMenu, MF_STRING, 1004, L"Zoom In (Ctrl++)");
        AppendMenuW(hMenu, MF_STRING, 1005, L"Zoom Out (Ctrl+-)");
        AppendMenuW(hMenu, MF_STRING, 1006, L"Fit to Window (Ctrl+0 or Double Click)");
        AppendMenuW(hMenu, MF_STRING, 1007, L"Full Screen (F11)");
        AppendMenuW(hMenu, MF_STRING, 1008, L"Delete Image (Delete)");
        AppendMenuW(hMenu, MF_STRING, 1009, L"Exit (Esc)");
        AppendMenuW(hMenu, MF_STRING, 1010, L"Rotate Clockwise (Up Arrow)");
        AppendMenuW(hMenu, MF_STRING, 1011, L"Rotate Counterclockwise (Down Arrow)");
        AppendMenuW(hMenu, MF_STRING, 1012, L"Save Image (Ctrl+S)");
        POINT pt;
        GetCursorPos(&pt);
        int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
        DestroyMenu(hMenu);

        switch (cmd) {
        case 1001: {
            wchar_t szFile[MAX_PATH] = {0};
            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2\0All Files\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                LogDebugMessage(L"Opening via context menu: " + std::wstring(szFile));
                LoadImage(szFile);
                GetImagesInDirectory(szFile);
                RECT clientRect;
                GetClientRect(hWnd, &clientRect);
                FitImageToWindow(clientRect);
            }
            break;
        }
        case 1002: NextImage(); break;
        case 1003: PreviousImage(); break;
        case 1004: ZoomImage(1.1f); break;
        case 1005: ZoomImage(0.9f); break;
        case 1006: {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            FitImageToWindow(clientRect);
            break;
        }
        case 1007: ToggleFullScreen(); break;
        case 1008: DeleteCurrentImage(); break;
        case 1009: PostQuitMessage(0); break;
        case 1010: RotateImage(true); break;
        case 1011: RotateImage(false); break;
        case 1012: SaveImage(); break;
        }
        break;
    }
    case WM_NCHITTEST: {
        if (isFullScreen) return HTCLIENT;
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        const int edge = 10;

        bool nearLeft = pt.x >= 0 && pt.x < edge;
        bool nearRight = pt.x >= clientRect.right - edge && pt.x < clientRect.right;
        bool nearTop = pt.y >= 0 && pt.y < edge;
        bool nearBottom = pt.y >= clientRect.bottom - edge && pt.y < clientRect.bottom;

        if (nearTop && nearLeft) return HTTOPLEFT;
        if (nearTop && nearRight) return HTTOPRIGHT;
        if (nearBottom && nearLeft) return HTBOTTOMLEFT;
        if (nearBottom && nearRight) return HTBOTTOMRIGHT;
        if (nearTop) return HTTOP;
        if (nearBottom) return HTBOTTOM;
        if (nearLeft) return HTLEFT;
        if (nearRight) return HTRIGHT;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        if (!isFullScreen) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            LRESULT hit = SendMessage(hWnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
            if (hit == HTCLIENT) {
                isDragging = true;
                SetCapture(hWnd);
                ReleaseCapture();
                SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
            } else if (hit >= HTLEFT && hit <= HTBOTTOMRIGHT) {
                isResizing = true;
                resizeStart = pt;
                ClientToScreen(hWnd, &resizeStart);
                GetWindowRect(hWnd, &resizeRect);
                resizeEdge = hit;
                SetCapture(hWnd);
            }
            SetFocus(hWnd);
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (isResizing) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ClientToScreen(hWnd, &pt);
            RECT newRect = resizeRect;
            int minWidth = 200, minHeight = 200;

            switch (resizeEdge) {
            case HTLEFT:
                newRect.left = pt.x;
                if (newRect.right - newRect.left < minWidth) newRect.left = newRect.right - minWidth;
                break;
            case HTRIGHT:
                newRect.right = pt.x;
                if (newRect.right - newRect.left < minWidth) newRect.right = newRect.left + minWidth;
                break;
            case HTTOP:
                newRect.top = pt.y;
                if (newRect.bottom - newRect.top < minHeight) newRect.top = newRect.bottom - minHeight;
                break;
            case HTBOTTOM:
                newRect.bottom = pt.y;
                if (newRect.bottom - newRect.top < minHeight) newRect.bottom = newRect.top + minHeight;
                break;
            case HTTOPLEFT:
                newRect.left = pt.x;
                newRect.top = pt.y;
                if (newRect.right - newRect.left < minWidth) newRect.left = newRect.right - minWidth;
                if (newRect.bottom - newRect.top < minHeight) newRect.top = newRect.bottom - minHeight;
                break;
            case HTTOPRIGHT:
                newRect.right = pt.x;
                newRect.top = pt.y;
                if (newRect.right - newRect.left < minWidth) newRect.right = newRect.left + minWidth;
                if (newRect.bottom - newRect.top < minHeight) newRect.top = newRect.bottom - minHeight;
                break;
            case HTBOTTOMLEFT:
                newRect.left = pt.x;
                newRect.bottom = pt.y;
                if (newRect.right - newRect.left < minWidth) newRect.left = newRect.right - minWidth;
                if (newRect.bottom - newRect.top < minHeight) newRect.bottom = newRect.top + minHeight;
                break;
            case HTBOTTOMRIGHT:
                newRect.right = pt.x;
                newRect.bottom = pt.y;
                if (newRect.right - newRect.left < minWidth) newRect.right = newRect.left + minWidth;
                if (newRect.bottom - newRect.top < minHeight) newRect.bottom = newRect.top + minHeight;
                break;
            }
            HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMonitor, &mi);
            newRect.left = max(mi.rcWork.left, min(newRect.left, mi.rcWork.right - (newRect.right - newRect.left)));
            newRect.top = max(mi.rcWork.top, min(newRect.top, mi.rcWork.bottom - (newRect.bottom - newRect.top)));
            SetWindowPos(hWnd, nullptr, newRect.left, newRect.top,
                         newRect.right - newRect.left, newRect.bottom - newRect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hWnd, nullptr, TRUE);
        } else if (!isFullScreen) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            LRESULT hit = SendMessage(hWnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
            if (hit == HTLEFT || hit == HTRIGHT) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            else if (hit == HTTOP || hit == HTBOTTOM) SetCursor(LoadCursor(nullptr, IDC_SIZENS));
            else if (hit == HTTOPLEFT || hit == HTBOTTOMRIGHT) SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
            else if (hit == HTTOPRIGHT || hit == HTBOTTOMLEFT) SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
            else SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (isResizing) {
            isResizing = false;
            ReleaseCapture();
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        if (isDragging) {
            isDragging = false;
            ReleaseCapture();
        }
        break;
    }
    case WM_SIZING: {
        RECT* pRect = (RECT*)lParam;
        if (pRect->right - pRect->left < 200) pRect->right = pRect->left + 200;
        if (pRect->bottom - pRect->top < 200) pRect->bottom = pRect->top + 200;
        InvalidateRect(hWnd, nullptr, TRUE);
        return TRUE;
    }
    case WM_SIZE:
    case WM_ACTIVATE:
    case WM_SETFOCUS:
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    case WM_DESTROY:
        if (hBitmap) DeleteObject(hBitmap);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void LoadImage(const wchar_t* filePath) {
    LogDebugMessage(L"LoadImage called with: " + std::wstring(filePath));
    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IStream* stream = nullptr;

    HRESULT hr = SHCreateStreamOnFileEx(filePath, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (SUCCEEDED(hr)) {
        hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (SUCCEEDED(hr)) {
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr)) {
                hr = wicFactory->CreateFormatConverter(&converter);
                if (SUCCEEDED(hr)) {
                    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) {
                        UINT width, height;
                        frame->GetSize(&width, &height);
                        BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER), (LONG)width, -(LONG)height, 1, 32, BI_RGB };
                        HDC hdc = GetDC(hWnd);
                        void* bits;
                        hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
                        ReleaseDC(hWnd, hdc);
                        converter->CopyPixels(nullptr, width * 4, width * height * 4, (BYTE*)bits);
                        zoomFactor = 1.0f;
                        rotationAngle = 0;
                        RECT clientRect;
                        GetClientRect(hWnd, &clientRect);
                        FitImageToWindow(clientRect);
                        LogDebugMessage(L"LoadImage: Successfully loaded " + std::wstring(filePath));
                    }
                }
            }
        }
        stream->Release();
    }
    if (FAILED(hr)) {
        LogDebugMessage(L"LoadImage: Failed, HRESULT: " + std::to_wstring(hr));
        std::wstring errorMsg = L"Cannot load image: " + std::wstring(filePath);
        MessageBoxW(hWnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
}

void DrawImage(HDC hdc, RECT clientRect) {
    if (!hBitmap) return;

    BITMAP bm;
    GetObject(hBitmap, sizeof(bm), &bm);
    HDC memDC = CreateCompatibleDC(hdc);
    SelectObject(memDC, hBitmap);

    int srcWidth = bm.bmWidth;
    int srcHeight = bm.bmHeight;
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    float windowCenterX = clientWidth / 2.0f;
    float windowCenterY = clientHeight / 2.0f;

    double rad = rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = (float)cos(rad);
    float sinTheta = (float)sin(rad);

    SetGraphicsMode(hdc, GM_ADVANCED);
    XFORM xform = {0};
    xform.eM11 = cosTheta * zoomFactor;
    xform.eM12 = sinTheta * zoomFactor;
    xform.eM21 = -sinTheta * zoomFactor;
    xform.eM22 = cosTheta * zoomFactor;
    xform.eDx = windowCenterX - (srcWidth / 2.0f) * cosTheta * zoomFactor + (srcHeight / 2.0f) * sinTheta * zoomFactor;
    xform.eDy = windowCenterY - (srcWidth / 2.0f) * sinTheta * zoomFactor - (srcHeight / 2.0f) * cosTheta * zoomFactor;
    SetWorldTransform(hdc, &xform);

    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, 0, 0, srcWidth, srcHeight, memDC, 0, 0, srcWidth, srcHeight, SRCCOPY);

    XFORM identity = {1, 0, 0, 1, 0, 0};
    SetWorldTransform(hdc, &identity);
    SetGraphicsMode(hdc, GM_COMPATIBLE);
    DeleteDC(memDC);
}

bool IsImageFile(const wchar_t* filePath) {
    IWICBitmapDecoder* decoder = nullptr;
    IStream* stream = nullptr;
    bool isImage = false;

    HRESULT hr = SHCreateStreamOnFileEx(filePath, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (SUCCEEDED(hr)) {
        hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        isImage = SUCCEEDED(hr);
        if (decoder) decoder->Release();
        stream->Release();
    }
    return isImage;
}

void GetImagesInDirectory(const wchar_t* filePath) {
    imageFiles.clear();
    currentImageIndex = -1;

    wchar_t folder[MAX_PATH];
    wcscpy_s(folder, MAX_PATH, filePath);
    PathRemoveFileSpecW(folder);

    WIN32_FIND_DATAW fd;
    wchar_t searchPath[MAX_PATH];
    swprintf_s(searchPath, MAX_PATH, L"%s\\*.*", folder);

    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                wchar_t fullPath[MAX_PATH];
                swprintf_s(fullPath, MAX_PATH, L"%s\\%s", folder, fd.cFileName);
                if (IsImageFile(fullPath)) {
                    imageFiles.push_back(fullPath);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::sort(imageFiles.begin(), imageFiles.end());
    for (size_t i = 0; i < imageFiles.size(); ++i) {
        if (_wcsicmp(imageFiles[i].c_str(), filePath) == 0) {
            currentImageIndex = static_cast<int>(i);
            break;
        }
    }
}

void NextImage() {
    if (imageFiles.empty() || currentImageIndex < 0) return;
    if (currentImageIndex < (int)imageFiles.size() - 1) {
        currentImageIndex++;
        LoadImage(imageFiles[currentImageIndex].c_str());
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        FitImageToWindow(clientRect);
    }
}

void PreviousImage() {
    if (imageFiles.empty() || currentImageIndex < 0) return;
    if (currentImageIndex > 0) {
        currentImageIndex--;
        LoadImage(imageFiles[currentImageIndex].c_str());
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        FitImageToWindow(clientRect);
    }
}

void ZoomImage(float delta) {
    zoomFactor *= delta;
    if (zoomFactor < 0.1f) zoomFactor = 0.1f;
    if (zoomFactor > 10.0f) zoomFactor = 10.0f;
    InvalidateRect(hWnd, nullptr, TRUE);
}

void FitImageToWindow(RECT clientRect) {
    if (!hBitmap) return;
    BITMAP bm;
    GetObject(hBitmap, sizeof(bm), &bm);

    float clientWidth = (float)(clientRect.right - clientRect.left);
    float clientHeight = (float)(clientRect.bottom - clientRect.top);
    float imageWidth = (float)bm.bmWidth;
    float imageHeight = (float)bm.bmHeight;

    if (rotationAngle == 90 || rotationAngle == 270) {
        std::swap(imageWidth, imageHeight);
    }

    zoomFactor = min(clientWidth / imageWidth, clientHeight / imageHeight);
    InvalidateRect(hWnd, nullptr, TRUE);
}

void ToggleFullScreen() {
    if (!isFullScreen) {
        savedStyle = GetWindowLong(hWnd, GWL_STYLE);
        GetWindowRect(hWnd, &savedRect);

        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);

        SetWindowLong(hWnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
        isFullScreen = true;
    } else {
        SetWindowLong(hWnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(hWnd, HWND_TOP, savedRect.left, savedRect.top,
                     savedRect.right - savedRect.left, savedRect.bottom - savedRect.top,
                     SWP_FRAMECHANGED);
        isFullScreen = false;
    }
    InvalidateRect(hWnd, nullptr, TRUE);
}

void DeleteCurrentImage() {
    if (imageFiles.empty() || currentImageIndex < 0) return;

    if (MessageBoxW(hWnd, L"Are you sure you want to delete this image?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        std::wstring filePath = imageFiles[currentImageIndex];
        if (hBitmap) {
            DeleteObject(hBitmap);
            hBitmap = nullptr;
            InvalidateRect(hWnd, nullptr, TRUE);
        }

        std::vector<wchar_t> filePathBuffer(filePath.size() + 2, 0);
        wcscpy_s(filePathBuffer.data(), filePath.size() + 1, filePath.c_str());

        SHFILEOPSTRUCTW fileOp = {0};
        fileOp.hwnd = hWnd;
        fileOp.wFunc = FO_DELETE;
        fileOp.pFrom = filePathBuffer.data();
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;

        int result = SHFileOperationW(&fileOp);
        if (result == 0 && !fileOp.fAnyOperationsAborted) {
            imageFiles.erase(imageFiles.begin() + currentImageIndex);
            if (imageFiles.empty()) {
                currentImageIndex = -1;
                InvalidateRect(hWnd, nullptr, TRUE);
            } else {
                if (currentImageIndex >= (int)imageFiles.size()) currentImageIndex--;
                LoadImage(imageFiles[currentImageIndex].c_str());
                RECT clientRect;
                GetClientRect(hWnd, &clientRect);
                FitImageToWindow(clientRect);
            }
        } else {
            std::wstring errorMsg = L"Failed to delete image. Error code: " + std::to_wstring(result);
            MessageBoxW(hWnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

void RotateImage(bool clockwise) {
    if (!hBitmap) return;
    rotationAngle += clockwise ? 90 : -90;
    if (rotationAngle >= 360) rotationAngle -= 360;
    if (rotationAngle < 0) rotationAngle += 360;
    InvalidateRect(hWnd, nullptr, TRUE);
}

void SaveImage() {
    if (!hBitmap || currentImageIndex < 0) return;

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmap* bitmap = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frameEncode = nullptr;
    IStream* stream = nullptr;

    HRESULT hr = SHCreateStreamOnFileEx(imageFiles[currentImageIndex].c_str(),
                                        STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (SUCCEEDED(hr)) {
        hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (SUCCEEDED(hr)) {
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr)) {
                hr = wicFactory->CreateFormatConverter(&converter);
                if (SUCCEEDED(hr)) {
                    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) {
                        hr = wicFactory->CreateBitmapFromSource(converter, WICBitmapCacheOnDemand, &bitmap);
                    }
                }
            }
        }
        stream->Release();
    }
    if (FAILED(hr)) {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (bitmap) bitmap->Release();
        MessageBoxW(hWnd, L"Cannot load image for saving.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    GUID containerFormat;
    hr = decoder->GetContainerFormat(&containerFormat);
    if (FAILED(hr)) {
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        MessageBoxW(hWnd, L"Cannot determine original file format.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    if (rotationAngle != 0) {
        IWICBitmap* rotatedBitmap = nullptr;
        IWICBitmapFlipRotator* rotator = nullptr;
        hr = wicFactory->CreateBitmapFlipRotator(&rotator);
        if (SUCCEEDED(hr)) {
            hr = rotator->Initialize(bitmap, rotationAngle == 90 ? WICBitmapTransformRotate90 :
                                     rotationAngle == 180 ? WICBitmapTransformRotate180 :
                                     WICBitmapTransformRotate270);
            if (SUCCEEDED(hr)) {
                hr = wicFactory->CreateBitmapFromSource(rotator, WICBitmapCacheOnDemand, &rotatedBitmap);
                if (SUCCEEDED(hr)) {
                    bitmap->Release();
                    bitmap = rotatedBitmap;
                }
            }
            rotator->Release();
        }
        if (FAILED(hr)) {
            bitmap->Release();
            frame->Release();
            decoder->Release();
            converter->Release();
            MessageBoxW(hWnd, L"Cannot rotate image.", L"Error", MB_OK | MB_ICONERROR);
            return;
        }
    }

    std::wstring originalPath = imageFiles[currentImageIndex];
    std::wstring tempPath = originalPath + L".tmp";

    hr = SHCreateStreamOnFileEx(tempPath.c_str(), STGM_WRITE | STGM_CREATE | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (FAILED(hr)) {
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        std::wstring errorMsg = L"Cannot create temporary file. HRESULT: " + std::to_wstring(hr);
        MessageBoxW(hWnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    hr = wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) {
            IPropertyBag2* propBag = nullptr;
            hr = encoder->CreateNewFrame(&frameEncode, &propBag);
            if (SUCCEEDED(hr)) {
                if (containerFormat == GUID_ContainerFormatJpeg) {
                    PROPBAG2 option = {0};
                    option.pstrName = L"ImageQuality";
                    VARIANT varValue;
                    VariantInit(&varValue);
                    varValue.vt = VT_R4;
                    varValue.fltVal = 1.0f;
                    propBag->Write(1, &option, &varValue);
                    VariantClear(&varValue);
                }
                hr = frameEncode->Initialize(propBag);
                if (propBag) propBag->Release();
                if (SUCCEEDED(hr)) {
                    hr = frameEncode->WriteSource(bitmap, nullptr);
                    if (SUCCEEDED(hr)) {
                        hr = frameEncode->Commit();
                        if (SUCCEEDED(hr)) {
                            hr = encoder->Commit();
                        }
                    }
                }
            }
            if (frameEncode) frameEncode->Release();
        }
        encoder->Release();
    }
    stream->Release();

    if (FAILED(hr)) {
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        DeleteFileW(tempPath.c_str());
        std::wstring errorMsg = L"Cannot save image. HRESULT: " + std::to_wstring(hr);
        MessageBoxW(hWnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    bitmap->Release();
    frame->Release();
    decoder->Release();
    converter->Release();

    if (!MoveFileExW(tempPath.c_str(), originalPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tempPath.c_str());
        std::wstring errorMsg = L"Cannot replace original file. Error: " + std::to_wstring(GetLastError());
        MessageBoxW(hWnd, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    LoadImage(originalPath.c_str());
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    FitImageToWindow(clientRect);
}