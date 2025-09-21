#pragma once
#define NOMINMAX
#include <windows.h>
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

struct AppContext {
    HINSTANCE hInst = nullptr;
    HWND hWnd = nullptr;
    HBITMAP hBitmap = nullptr;
    ComPtr<IWICImagingFactory> wicFactory = nullptr;
    std::vector<std::wstring> imageFiles;
    int currentImageIndex = -1;
    float zoomFactor = 1.0f;
    int rotationAngle = 0;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool isFullScreen = false;
    LONG savedStyle = 0;
    RECT savedRect = { 0 };
    bool showFilePath = false;
    std::wstring currentFilePathOverride;
    bool isHoveringClose = false;
    bool startFullScreen = false;
};

void CenterImage(bool resetZoom);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void LoadImageFromFile(const wchar_t* filePath);
void GetImagesInDirectory(const wchar_t* filePath);
void SaveImage();
void SaveImageAs();
void DeleteCurrentImage();
void HandleDropFiles(HDROP hDrop);
void HandlePaste();
void HandleCopy();
void OpenFileLocationAction();
void DrawImage(HDC hdc, const RECT& clientRect, const AppContext& ctx);
void FitImageToWindow();
void ZoomImage(float factor);
void RotateImage(bool clockwise);
bool IsPointInImage(POINT pt, const RECT& clientRect);

void ReadSettings();
void WriteSettings();
void RegisterApp();
void UnregisterApp();
bool IsAppRegistered();