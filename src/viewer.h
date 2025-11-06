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

constexpr UINT WM_APP_IMAGE_LOADED = (WM_APP + 1);
constexpr UINT WM_APP_IMAGE_LOAD_FAILED = (WM_APP + 2);
constexpr UINT ANIMATION_TIMER_ID = 1;

enum class BackgroundColor {
    Grey = 0,
    Black = 1,
    White = 2,
    Transparent = 3
};

enum class SortCriteria {
    ByName = 0,
    ByDateModified = 1,
    ByFileSize = 2
};

struct ImageProperties {
    std::wstring filePath;
    std::wstring dimensions;
    std::wstring fileSize;
    std::wstring createdDate;
    std::wstring modifiedDate;
    std::wstring accessedDate;
    std::wstring attributes;
    std::wstring imageFormat;
    std::wstring bitDepth;
    std::wstring dpi;
    std::wstring cameraMake;
    std::wstring cameraModel;
    std::wstring dateTaken;
    std::wstring fStop;
    std::wstring exposureTime;
    std::wstring iso;
    std::wstring software;
    std::wstring focalLength;
    std::wstring focalLength35mm;
    std::wstring exposureBias;
    std::wstring meteringMode;
    std::wstring flash;
    std::wstring exposureProgram;
    std::wstring whiteBalance;
    std::wstring author;
    std::wstring copyright;
    std::wstring lensModel;
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
    bool enforceSingleInstance = true;
    bool isDraggingImage = false;
    std::wstring settingsPath;
    std::wstring currentDirectory;
    SortCriteria currentSortCriteria = SortCriteria::ByName;
    bool isSortAscending = true;

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

    HWND hPropsWnd = nullptr;

    bool isAnimated = false;
    std::vector<ComPtr<IWICFormatConverter>> animationFrameConverters;
    std::vector<ComPtr<ID2D1Bitmap>> animationD2DBitmaps;
    std::vector<UINT> animationFrameDelays;
    UINT currentAnimationFrame = 0;

    std::atomic<bool> isInitialized{ false };
    bool isOsdVisible = false;
};

void CenterImage(bool resetZoom);
void SetActualSize();
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PropsWndProc(HWND, UINT, WPARAM, LPARAM);
void ToggleFullScreen();
void LoadImageFromFile(const std::wstring& filePath);
void FinalizeImageLoad(bool success, int foundIndex);
void CleanupLoadingThread();
void CleanupPreloadingThreads();
void StartPreloading();
void GetImagesInDirectory(const wchar_t* directoryPath);
void SaveImage();
void SaveImageAs();
void DeleteCurrentImage();
void HandleDropFiles(HDROP hDrop);
void HandlePaste();
void HandleCopy();
void OpenFileLocationAction();
void ShowImageProperties();
void Render();
void CreateDeviceResources();
void DiscardDeviceResources();
void FitImageToWindow();
void ZoomImage(float factor, POINT pt);
void RotateImage(bool clockwise);
bool IsPointInImage(POINT pt, const RECT& clientRect);
bool GetCurrentImageSize(UINT* width, UINT* height);
ImageProperties GetCurrentOsdProperties();

void ReadSettings(const std::wstring& path, RECT& rect, bool& fullscreen, bool& singleInstance);
void WriteSettings(const std::wstring& path, const RECT& rect, bool fullscreen, bool singleInstance);