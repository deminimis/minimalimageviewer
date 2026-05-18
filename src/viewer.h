#pragma once

#include <sdkddkver.h>
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0A00

#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <shellapi.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <d2d1_1.h>
#include <d2d1_3.h> 
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <thread>
#include <atomic>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
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
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

constexpr UINT WM_APP_IMAGE_LOADED = (WM_APP + 1);
constexpr UINT WM_APP_IMAGE_LOAD_FAILED = (WM_APP + 2);
constexpr UINT WM_APP_IMAGE_READY = (WM_APP + 7);
constexpr UINT WM_APP_DIR_READY = (WM_APP + 8);
constexpr UINT WM_APP_HQ_READY = (WM_APP + 9);

constexpr UINT ANIMATION_TIMER_ID = 1;
constexpr UINT AUTO_REFRESH_TIMER_ID = 3;
constexpr UINT LOADING_TIMER_ID = 4;
constexpr UINT HQ_RENDER_TIMER_ID = 5;
constexpr UINT NAV_DEBOUNCE_TIMER_ID = 6;
constexpr UINT KEYBINDING_TIMER_ID = 7;

class CriticalSectionLock {
public:
    CriticalSectionLock(CRITICAL_SECTION& cs) : m_cs(cs) {
        EnterCriticalSection(&m_cs);
    }
    ~CriticalSectionLock() {
        LeaveCriticalSection(&m_cs);
    }
private:
    CriticalSectionLock(const CriticalSectionLock&) = delete;
    CriticalSectionLock& operator=(const CriticalSectionLock&) = delete;
    CRITICAL_SECTION& m_cs;
};

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

enum class DefaultZoomMode {
    Fit = 0,
    Actual = 1
};

enum ActionID {
    Act_Next = 0, Act_Prev, Act_ZoomIn, Act_ZoomOut, Act_Fit, Act_Actual,
    Act_Fullscreen, Act_RotateCW, Act_RotateCCW, Act_Flip, Act_Crop, Act_CustomZoom, Act_Exit, Act_Count
};

class ImageProperties {
public:
    std::wstring filePath;
    std::wstring dimensions = L"N/A";
    std::wstring fileSize = L"N/A";
    std::wstring createdDate = L"N/A";
    std::wstring modifiedDate = L"N/A";
    std::wstring accessedDate = L"N/A";
    std::wstring attributes;
    std::wstring imageFormat = L"N/A";
    std::wstring bitDepth = L"N/A";
    std::wstring dpi = L"N/A";
    std::wstring cameraMake = L"N/A";
    std::wstring cameraModel = L"N/A";
    std::wstring dateTaken = L"N/A";
    std::wstring fStop = L"N/A";
    std::wstring exposureTime = L"N/A";
    std::wstring iso = L"N/A";
    std::wstring software = L"N/A";
    std::wstring focalLength = L"N/A";
    std::wstring focalLength35mm = L"N/A";
    std::wstring exposureBias = L"N/A";
    std::wstring meteringMode = L"N/A";
    std::wstring flash = L"N/A";
    std::wstring exposureProgram = L"N/A";
    std::wstring whiteBalance = L"N/A";
    std::wstring author = L"N/A";
    std::wstring copyright = L"N/A";
    std::wstring lensModel = L"N/A";
};

struct AppContext {
    HINSTANCE hInst = nullptr;
    HWND hWnd = nullptr;
    ComPtr<IWICImagingFactory> wicFactory = nullptr;
    ComPtr<ID2D1Factory1> d2dFactory = nullptr;
    ComPtr<IDWriteFactory> writeFactory = nullptr;
    ComPtr<ID2D1DeviceContext> renderTarget = nullptr;
    ComPtr<IDXGISwapChain1> swapChain = nullptr;
    ComPtr<ID2D1Effect> colorMatrixEffect = nullptr;
    ComPtr<ID2D1Bitmap> d2dBitmap = nullptr;
    ComPtr<ID2D1Bitmap> d2dBitmapHq = nullptr;
    float hqZoomFactor = 1.0f;
    bool isHqPending = false;
    int pendingNavIndex = -1;
    ComPtr<IWICFormatConverter> wicConverter = nullptr;
    ComPtr<IWICFormatConverter> wicConverterOriginal = nullptr;
    std::vector<ComPtr<IWICFormatConverter>> undoStack;
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
    UINT currentOrientation = 1;
    bool startFullScreen = false;
    bool enforceSingleInstance = true;
    bool alwaysOnTop = false;
    bool isDraggingImage = false;
    std::wstring settingsPath;
    std::wstring currentDirectory;
    SortCriteria currentSortCriteria = SortCriteria::ByName;
    bool isSortAscending = true;
    DefaultZoomMode defaultZoomMode = DefaultZoomMode::Fit;

    // Loading State
    std::atomic<bool> isLoading{ false };
    std::atomic<int> loadSequenceId{ 0 };
    CRITICAL_SECTION wicMutex{};
    ULONGLONG loadStartTime = 0;

    std::wstring loadingFilePath;
    GUID originalContainerFormat = {};
    bool startAtEnd = false;

 
    std::vector<std::vector<BYTE>> stagedFrames; 
    std::vector<UINT> stagedDelays;              
    UINT stagedWidth = 0;
    UINT stagedHeight = 0;
    UINT stagedOrientation = 1;

    std::vector<std::wstring> stagedImageFiles;
    int stagedFoundIndex = -1;

    // Preloading
    ComPtr<IWICFormatConverter> preloadedNextConverter;
    ComPtr<IWICFormatConverter> preloadedPrevConverter;
    GUID preloadedNextFormat = {};
    GUID preloadedPrevFormat = {};
    std::wstring preloadedNextPath;
    std::wstring preloadedPrevPath;

    // SVG State
    bool isSvg = false;
    ComPtr<ID2D1SvgDocument> svgDocument = nullptr;
    std::vector<BYTE> svgData;
    std::vector<BYTE> stagedSvgData;
    UINT preloadedNextOrientation = 1;
    UINT preloadedPrevOrientation = 1;
    std::atomic<bool> cancelPreloading{ false };
    CRITICAL_SECTION preloadMutex{};

    HWND hPropsWnd = nullptr;

    // Animation State
    bool isAnimated = false;
    std::vector<ComPtr<IWICFormatConverter>> animationFrameConverters;
    std::vector<ComPtr<ID2D1Bitmap>> animationD2DBitmaps;
    std::vector<UINT> animationFrameDelays;
    UINT currentAnimationFrame = 0;

    std::atomic<bool> isInitialized{ false };
    bool isOsdVisible = false;

    bool isFlippedHorizontal = false;
    bool isGrayscale = false;

    ComPtr<ID2D1SolidColorBrush> cropRectBrush;
    ComPtr<ID2D1SolidColorBrush> fadeBrush;
    HBRUSH darkBrush = nullptr;
    bool isCropMode = false;
    bool isSelectingCropRect = false;
    bool isCropPending = false;
    POINT cropStartPoint = { 0 };
    D2D1_RECT_F cropRectWindow = { 0 };
    D2D1_RECT_F cropRectLocal = { 0 };
    bool isCropActive = false;

    bool isEyedropperActive = false;
    POINT currentMousePos = { 0 };
    COLORREF hoveredColor = 0;
    std::wstring colorStringRgb;
    std::wstring colorStringHex;
    bool didCopyColor = false;

    bool isFading = false;
    ULONGLONG fadeStartTime = 0;

    bool isAutoRefresh = false;
    bool smoothScaling = true;
    bool enableFadeAnimation = true;
    FILETIME lastWriteTime = { 0 };
    bool preserveView = false;
    float renderScale = 1.0f;
    WORD hotkeys[Act_Count]{};

    // Thread Management
    std::atomic<int> activeBackgroundThreads{ 0 };
    std::atomic<bool> isShuttingDown{ false };
    std::atomic<int> hqRenderSequenceId{ 0 };
    std::atomic<bool> isHqTaskRunning{ false };

    template <typename Func>
    void RunBackgroundTask(Func&& task) {
        activeBackgroundThreads++;
        std::thread([this, t = std::forward<Func>(task)]() mutable {
            struct Tracker {
                AppContext* ctx;
                ~Tracker() { ctx->activeBackgroundThreads--; }
            } tracker{ this };

            if (!isShuttingDown) {
                t();
            }
            }).detach();
    }
};

class ViewerApp {
public:
    ViewerApp() = default;
    ~ViewerApp() = default;

    // App entry/message routing
    int Run(HINSTANCE hInstance, int nCmdShow, LPWSTR lpCmdLine);
    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Core methods 
    void CenterImage(bool resetZoom);
    void SetActualSize();
    void SetZoomLevel(float zoom);
    void TriggerHqRender();
    void ToggleFullScreen();
    void UpdateWindowTitle();
    void UpdateEyedropperColor(POINT pt);
    void HandleEyedropperClick();
    void OpenFileAction();
    void LoadImageFromFile(const std::wstring& filePath, bool startAtEnd = false);
    void FinalizeImageLoad(bool success, int foundIndex);
    void OnImageReady(bool success, int seqId);
    void OnDirReady(int seqId);
    void CleanupLoadingThread();
    void CleanupPreloadingThreads();
    void StartPreloading();
    std::vector<std::wstring> ScanDirectory(const std::wstring& directoryPath, int seqId);
    void SaveImage();
    void SaveImageAs();
    void ResizeImageAction();
    void ApplyEffectsToView();
    void CommitCrop();
    void UpdateViewToCurrentFrame();
    void DeleteCurrentImage();
    void HandleDropFiles(HDROP hDrop);
    void HandlePaste();
    void HandleCopy();
    void OpenFileLocationAction();
    void ShowImageProperties();
    void OpenPreferencesDialog();
    void OpenKeybindingsDialog();
    std::wstring GetHotkeyString(WORD hk);
    void OpenZoomDialog();
    void Render();
    void CreateDeviceResources();
    void DiscardDeviceResources();
    void FitImageToWindow();
    void ZoomImage(float factor, POINT pt);
    void RotateImage(bool clockwise);
    void FlipImage();
    bool GetCurrentImageSize(UINT* width, UINT* height);
    ImageProperties GetCurrentOsdProperties();
    void ConvertWindowToImagePoint(POINT pt, float& localX, float& localY);
    void ConvertImageToWindowPoint(float localX, float localY, POINT& pt);
    void UpdateTitleBarTheme(HWND hWnd, BackgroundColor bgColor);
    void ReadSettings(const std::wstring& path, RECT& rect, bool& fullscreen, bool& singleInstance, bool& alwaysOnTop);
    void WriteSettings(const std::wstring& path, const RECT& rect, bool fullscreen, bool singleInstance, bool alwaysOnTop);
    HRESULT CreateDecoderFromFile(const wchar_t* filePath, IWICBitmapDecoder** ppDecoder);
    // Dialog Callbacks
    static INT_PTR CALLBACK PreferencesDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK KeybindingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK ZoomDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PropsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    AppContext m_ctx;

public:
    AppContext& GetContext() { return m_ctx; }
    const AppContext& GetContext() const { return m_ctx; }

private:
    // UI Handlers
    void OnPaint(HWND hWnd);
    bool CheckHotkey(WORD hk, WPARAM wParam);
    void OnKeyDown(WPARAM wParam);
    void OnContextMenu(HWND hWnd, POINT pt);

    // Drawing Helpers
    void DrawOsdOverlay(ID2D1DeviceContext* renderTarget);
    void DrawEyedropperOverlay(ID2D1DeviceContext* renderTarget);

    // Edit Helpers
    HRESULT EncodeAndSaveImage(ComPtr<IWICBitmapSource> source, const std::wstring& filePath, const GUID& containerFormat);
    ComPtr<IWICBitmapSource> GetSaveSource(const GUID& targetFormat);
    void SaveImageWithResize(const std::wstring& filePath, const GUID& containerFormat, UINT newWidth, UINT newHeight);
    ComPtr<IWICBitmapSource> ApplyCropAndTransform(ComPtr<IWICBitmapSource> source);

    // IO Helpers
    bool IsSequenceValid(int seqId);
    HRESULT CreateDecoderFromStream_FullFileRead(IWICImagingFactory* pFactory, const wchar_t* filePath, IWICBitmapDecoder** ppDecoder, int seqId);
};
