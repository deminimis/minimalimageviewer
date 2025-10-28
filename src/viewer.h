#pragma once
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <shellapi.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <d2d1.h>
#include <dwrite.h>
#include <thread>
#include <mutex>
#include <atomic>
#include "ComPtr.h"
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#define WM_APP_IMAGE_LOADED (WM_APP + 1)
#define WM_APP_IMAGE_LOAD_FAILED (WM_APP + 2)

enum class BackgroundColor {
    Grey = 0,
    Black = 1,
    White = 2,
    Transparent = 3
};

struct AppContext {
    HINSTANCE hInst = nullptr;
    HWND hWnd = nullptr;
    ComPtr<IWICImagingFactory> wicFactory = nullptr;
    ComPtr<ID2D1Factory> d2dFactory = nullptr;
    ComPtr<IDWriteFactory> writeFactory = nullptr;
    ComPtr<ID2D1HwndRenderTarget> renderTarget = nullptr;
    ComPtr<ID2D1Bitmap> d2dBitmap = nullptr;
    ComPtr<IWICFormatConverter> wicConverter = nullptr;
    ComPtr<IDWriteTextFormat> textFormat = nullptr;
    ComPtr<ID2D1SolidColorBrush> textBrush = nullptr;
    ComPtr<ID2D1BitmapBrush> checkerboardBrush = nullptr;
    BackgroundColor bgColor = BackgroundColor::Grey;
    std::vector<std::wstring> imageFiles;
    int currentImageIndex = -1;
    float zoomFactor = 1.0f;
    int rotationAngle = 0;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool isFullScreen = false;
    LONG savedStyle = 0;
    RECT savedRect = { 0 };
    std::wstring currentFilePathOverride;
    bool startFullScreen = false;
    bool isDraggingImage = false;
    std::wstring settingsPath;
    std::wstring currentDirectory;

    std::atomic<bool> isLoading{ false };
    std::thread loadingThread;
    std::mutex wicMutex;
    ComPtr<IWICFormatConverter> stagedWicConverter;
    std::wstring loadingFilePath;

    ComPtr<IWICFormatConverter> preloadedNextConverter;
    ComPtr<IWICFormatConverter> preloadedPrevConverter;
    std::thread preloadingNextThread;
    std::thread preloadingPrevThread;
    std::atomic<bool> cancelPreloading{ false };
    std::mutex preloadMutex;
};

void CenterImage(bool resetZoom);
void SetActualSize();
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ToggleFullScreen();
void LoadImageFromFile(const std::wstring& filePath);
void FinalizeImageLoad(bool success, int foundIndex);
void CleanupLoadingThread();
void CleanupPreloadingThreads();
void StartPreloading();
void GetImagesInDirectory(const wchar_t* filePath);
void SaveImage();
void SaveImageAs();
void DeleteCurrentImage();
void HandleDropFiles(HDROP hDrop);
void HandlePaste();
void HandleCopy();
void OpenFileLocationAction();
void Render();
void CreateDeviceResources();
void DiscardDeviceResources();
void FitImageToWindow();
void ZoomImage(float factor, POINT pt);
void RotateImage(bool clockwise);
bool IsPointInImage(POINT pt, const RECT& clientRect);

void ReadSettings(const std::wstring& path, RECT& rect, bool& fullscreen);
void WriteSettings(const std::wstring& path, const RECT& rect, bool fullscreen);