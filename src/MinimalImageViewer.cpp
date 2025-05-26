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
int rotationAngle = 0; // 0, 90, 180, 270 degrees
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

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    // Prevent multiple instances
    HWND existingWnd = FindWindowW(L"MinimalImageViewer", nullptr);
    if (existingWnd) {
        SetForegroundWindow(existingWnd);
        if (IsIconic(existingWnd)) {
            ShowWindow(existingWnd, SW_RESTORE);
        }
        // Pass command line to existing instance
        if (lpCmdLine && *lpCmdLine) {
            COPYDATASTRUCT cds;
            cds.dwData = 1;
            cds.cbData = (DWORD)(wcslen(lpCmdLine) + 1) * sizeof(wchar_t);
            cds.lpData = lpCmdLine;
            SendMessage(existingWnd, WM_COPYDATA, (WPARAM)hInstance, (LPARAM)&cds);
        }
        return 0;
    }

    hInst = hInstance;

    // Initialize WIC
    CoInitialize(nullptr);
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize WIC.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Register window class
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(100)); // Use custom icon from resource.res
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wcex.lpszClassName = L"MinimalImageViewer";
    RegisterClassExW(&wcex);

    // Create borderless window
    hWnd = CreateWindowW(L"MinimalImageViewer", nullptr,
                         WS_POPUP | WS_VISIBLE,
                         CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                         nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Handle command line
    if (lpCmdLine && *lpCmdLine) {
        LoadImage(lpCmdLine);
        GetImagesInDirectory(lpCmdLine);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        FitImageToWindow(clientRect);
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
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

        // Double buffering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        // Draw to memory DC
        FillRect(memDC, &clientRect, CreateSolidBrush(RGB(0, 0, 0)));
        if (hBitmap) {
            DrawImage(memDC, clientRect);
        } else {
            // Draw "Right click to see hotkeys" when no image is loaded
            SetTextColor(memDC, RGB(255, 255, 255));
            SetBkMode(memDC, TRANSPARENT);
            HFONT hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);
            const wchar_t* text = L"Right click to see hotkeys";
            SIZE textSize;
            GetTextExtentPoint32W(memDC, text, lstrlenW(text), &textSize);
            DrawTextW(memDC, text, -1, &clientRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(memDC, hOldFont);
            DeleteObject(hFont);
        }

        // Copy to screen
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

        // Cleanup
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_KEYDOWN: {
        bool ctrlPressed = GetKeyState(VK_CONTROL) & 0x8000;
        switch (wParam) {
        case VK_RIGHT:
            NextImage();
            break;
        case VK_LEFT:
            PreviousImage();
            break;
        case VK_UP:
            RotateImage(true); // Clockwise
            break;
        case VK_DOWN:
            RotateImage(false); // Counterclockwise
            break;
        case VK_DELETE:
            DeleteCurrentImage();
            break;
        case VK_F11:
            ToggleFullScreen();
            break;
        case VK_ESCAPE:
            PostQuitMessage(0);
            break;
        case 'O':
            if (ctrlPressed) {
                wchar_t szFile[MAX_PATH] = {0};
                OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
                ofn.hwndOwner = hWnd;
                // Updated filter to include more image formats
                ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2\0All Files\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    LoadImage(szFile);
                    GetImagesInDirectory(szFile);
                    RECT clientRect;
                    GetClientRect(hWnd, &clientRect);
                    FitImageToWindow(clientRect);
                }
            }
            break;
        case 'S':
            if (ctrlPressed) {
                SaveImage();
            }
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
            // Updated filter to include more image formats
            ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2\0All Files\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
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
        const int edge = 10; // Edge area for resizing

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
                resizeStart = { LOWORD(lParam), HIWORD(lParam) };
                ClientToScreen(hWnd, &resizeStart);
                GetWindowRect(hWnd, &resizeRect);
                resizeEdge = hit;
                SetCapture(hWnd);
            }
            SetFocus(hWnd); // Ensure window is focused
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
            // Ensure window stays on screen
            HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMonitor, &mi);
            newRect.left = max(mi.rcWork.left, min(newRect.left, mi.rcWork.right - (newRect.right - newRect.left)));
            newRect.top = max(mi.rcWork.top, min(newRect.top, mi.rcWork.bottom - (newRect.bottom - newRect.top)));
            SetWindowPos(hWnd, nullptr, newRect.left, newRect.top,
                         newRect.right - newRect.left, newRect.bottom - newRect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateWindow(hWnd);
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
            UpdateWindow(hWnd);
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
        UpdateWindow(hWnd);
        InvalidateRect(hWnd, nullptr, TRUE);
        return TRUE;
    }
    case WM_SIZE: {
        UpdateWindow(hWnd);
        InvalidateRect(hWnd, nullptr, TRUE);
        break;
    }
    case WM_ACTIVATE: {
        if (LOWORD(wParam) != WA_INACTIVE) {
            SetFocus(hWnd);
            UpdateWindow(hWnd);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_SETFOCUS: {
        UpdateWindow(hWnd);
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }
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
    if (hBitmap) DeleteObject(hBitmap);
    hBitmap = nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(hWnd, L"Cannot open image file.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    IStream* stream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(filePath, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (SUCCEEDED(hr)) {
        hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (SUCCEEDED(hr)) {
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr)) {
                wicFactory->CreateFormatConverter(&converter);
                converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
                
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
            }
        }
        stream->Release();
    }
    if (FAILED(hr)) {
        MessageBoxW(hWnd, L"Failed to load image.", L"Error", MB_OK | MB_ICONERROR);
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
}

void DrawImage(HDC hdc, RECT clientRect) {
    if (!hBitmap) return;
    
    BITMAP bm;
    GetObject(hBitmap, sizeof(BITMAP), &bm);
    HDC memDC = CreateCompatibleDC(hdc);
    SelectObject(memDC, hBitmap);

    int srcWidth = bm.bmWidth;
    int srcHeight = bm.bmHeight;

    // Calculate window center
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;
    float windowCenterX = clientWidth / 2.0f;
    float windowCenterY = clientHeight / 2.0f;

    // Calculate rotation
    double rad = rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = (float)cos(rad);
    float sinTheta = (float)sin(rad);

    // Set up transformation to rotate around image center and place at window center
    SetGraphicsMode(hdc, GM_ADVANCED);
    XFORM xform = {0};
    xform.eM11 = cosTheta * zoomFactor;
    xform.eM12 = sinTheta * zoomFactor;
    xform.eM21 = -sinTheta * zoomFactor;
    xform.eM22 = cosTheta * zoomFactor;
    xform.eDx = windowCenterX - (srcWidth / 2.0f) * cosTheta * zoomFactor + (srcHeight / 2.0f) * sinTheta * zoomFactor;
    xform.eDy = windowCenterY - (srcWidth / 2.0f) * sinTheta * zoomFactor - (srcHeight / 2.0f) * cosTheta * zoomFactor;
    SetWorldTransform(hdc, &xform);

    // Draw image
    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, 0, 0, srcWidth, srcHeight,
               memDC, 0, 0, srcWidth, srcHeight, SRCCOPY);

    // Reset transform
    XFORM identity = {1, 0, 0, 1, 0, 0};
    SetWorldTransform(hdc, &identity);
    SetGraphicsMode(hdc, GM_COMPATIBLE);
    
    DeleteDC(memDC);
}

bool IsImageFile(const wchar_t* filePath) {
    // Attempt to create a WIC decoder to verify if the file is a valid image
    IWICBitmapDecoder* decoder = nullptr;
    IStream* stream = nullptr;
    bool isImage = false;

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        HRESULT hr = SHCreateStreamOnFileEx(filePath, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
        if (SUCCEEDED(hr)) {
            hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
            isImage = SUCCEEDED(hr);
            if (decoder) decoder->Release();
            stream->Release();
        }
        CloseHandle(hFile);
    }
    return isImage;
}

void GetImagesInDirectory(const wchar_t* filePath) {
    imageFiles.clear();
    currentImageIndex = -1;

    wchar_t folder[MAX_PATH];
    wcscpy_s(folder, MAX_PATH, filePath);
    PathRemoveFileSpecW(folder);
    
    // Use wildcard to get all files and filter by valid images
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
    GetObject(hBitmap, sizeof(BITMAP), &bm);
    
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
        SetWindowPos(hWnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        isFullScreen = true;
    } else {
        SetWindowLong(hWnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(hWnd, HWND_TOP,
                     savedRect.left, savedRect.top,
                     savedRect.right - savedRect.left,
                     savedRect.bottom - savedRect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        isFullScreen = false;
    }
    UpdateWindow(hWnd);
    InvalidateRect(hWnd, nullptr, TRUE);
}

void DeleteCurrentImage() {
    if (imageFiles.empty() || currentImageIndex < 0) return;
    
    if (MessageBoxW(hWnd, L"Are you sure you want to delete this image?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {


        std::wstring filePath = imageFiles[currentImageIndex];
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
            wchar_t errorMsg[256];
            swprintf_s(errorMsg, L"Failed to move image to Recycle Bin. Error code: %d", result);
            MessageBoxW(hWnd, errorMsg, L"Failed to delete image", MB_OK | MB_ICONERROR);
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

    if (hBitmap) {
        DeleteObject(hBitmap);
        hBitmap = nullptr;
        InvalidateRect(hWnd, nullptr, TRUE);
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmap* bitmap = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frameEncode = nullptr;
    IStream* stream = nullptr;
    HRESULT hr;

    HANDLE hFile = CreateFileW(imageFiles[currentImageIndex].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        wchar_t errorMsg[256];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, errorMsg, 256, nullptr);
        MessageBoxW(hWnd, errorMsg, L"Cannot open image file.", MB_OK | MB_ICONERROR);
        return;
    }

    hr = SHCreateStreamOnFileEx(imageFiles[currentImageIndex].c_str(), STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
    if (SUCCEEDED(hr)) {
        hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (SUCCEEDED(hr)) {
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr)) {
                hr = wicFactory->CreateFormatConverter(&converter);
                if (SUCCEEDED(hr)) {
                    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) {
                        hr = wicFactory->CreateBitmapFromSource(converter, WICBitmapCacheOnDemand, &bitmap);
                    }
                }
            }
        }
        stream->Release();
    }
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);

    if (FAILED(hr)) {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (bitmap) bitmap->Release();
        MessageBoxW(hWnd, L"Cannot load image for saving.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Get the original container format
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
        IWICFormatConverter* rotConverter = nullptr;
        hr = wicFactory->CreateBitmapFlipRotator(&rotator);
        if (SUCCEEDED(hr)) {
            hr = rotator->Initialize(bitmap, rotationAngle == 90 ? WICBitmapTransformRotate90 :
                                     rotationAngle == 180 ? WICBitmapTransformRotate180 :
                                     WICBitmapTransformRotate270);
            if (SUCCEEDED(hr)) {
                hr = wicFactory->CreateFormatConverter(&rotConverter);
                if (SUCCEEDED(hr)) {
                    hr = rotConverter->Initialize(rotator, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) {
                        hr = wicFactory->CreateBitmapFromSource(rotConverter, WICBitmapCacheOnDemand, &rotatedBitmap);
                        if (SUCCEEDED(hr)) {
                            bitmap->Release();
                            bitmap = rotatedBitmap;
                        }
                    }
                    rotConverter->Release();
                }
            }
            rotator->Release();
        }
        if (FAILED(hr)) {
            if (bitmap) bitmap->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            if (converter) converter->Release();
            MessageBoxW(hWnd, L"Cannot rotate image.", L"Error", MB_OK | MB_ICONERROR);
            return;
        }
    }

    std::wstring originalPath = imageFiles[currentImageIndex];
    std::wstring tempPath = originalPath + L".tmp";

    hr = SHCreateStreamOnFileW(tempPath.c_str(), STGM_WRITE | STGM_CREATE, &stream);
    if (FAILED(hr)) {
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot create temporary file. HRESULT: 0x%08X", hr);
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    hr = wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (FAILED(hr)) {
        stream->Release();
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot create encoder for the specified format. HRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        encoder->Release();
        stream->Release();
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot initialize encoder. HRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    IPropertyBag2* propBag = nullptr;
    hr = encoder->CreateNewFrame(&frameEncode, &propBag);
    if (FAILED(hr)) {
        encoder->Release();
        stream->Release();
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot create frame. HRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Apply format-specific properties (e.g., JPEG quality)
    if (containerFormat == GUID_ContainerFormatJpeg) {
        PROPBAG2 option = {0};
        option.pstrName = L"ImageQuality";
        VARIANT varValue;
        VariantInit(&varValue);
        varValue.vt = VT_R4;
        varValue.fltVal = 1.0f;
        hr = propBag->Write(1, &option, &varValue);
        VariantClear(&varValue);
        if (FAILED(hr)) {
            propBag->Release();
            frameEncode->Release();
            encoder->Release();
            stream->Release();
            bitmap->Release();
            frame->Release();
            decoder->Release();
            converter->Release();
            wchar_t errorMsg[256];
            swprintf_s(errorMsg, L"Cannot set JPEG quality. HRESULT: 0x%08X", hr);
            MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
            return;
        }
    }

    hr = frameEncode->Initialize(propBag);
    if (FAILED(hr)) {
        propBag->Release();
        frameEncode->Release();
        encoder->Release();
        stream->Release();
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot initialize frame. HRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }
    propBag->Release();

    hr = frameEncode->WriteSource(bitmap, nullptr);
    if (FAILED(hr)) {
        frameEncode->Release();
        encoder->Release();
        stream->Release();
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot write image. HRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    hr = frameEncode->Commit();
    if (FAILED(hr)) {
        frameEncode->Release();
        encoder->Release();
        stream->Release();
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot commit frame. HRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        frameEncode->Release();
        encoder->Release();
        stream->Release();
        bitmap->Release();
        frame->Release();
        decoder->Release();
        converter->Release();
        wchar_t errorMsg[256];
        swprintf_s(errorMsg, L"Cannot commit encoder. HRESULT: 0x%08X", hr);
        MessageBoxW(hWnd, errorMsg, L"Save Error", MB_OK | MB_ICONERROR);
        return;
    }

    frameEncode->Release();
    encoder->Release();
    stream->Release();
    bitmap->Release();
    frame->Release();
    decoder->Release();
    converter->Release();

    bool replaced = false;
    for (int i = 0; i < 3 && !replaced; ++i) {
        if (MoveFileExW(tempPath.c_str(), originalPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            replaced = true;
        } else {
            Sleep(100);
        }
    }

    if (!replaced) {
        wchar_t errorMsg[256];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, errorMsg, 256, nullptr);
        MessageBoxW(hWnd, errorMsg, L"Cannot replace original file.", MB_OK | MB_ICONERROR);
        DeleteFileW(tempPath.c_str());
        return;
    }

    DeleteFileW(tempPath.c_str());

    LoadImage(originalPath.c_str());
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    FitImageToWindow(clientRect);
}