#include "viewer.h"
#include <string>

extern AppContext g_ctx;

void ReadSettings(const std::wstring& path, RECT& rect, bool& fullscreen, bool& singleInstance, bool& alwaysOnTop) {
    fullscreen = GetPrivateProfileIntW(L"Settings", L"StartFullScreen", 0, path.c_str()) == 1;
    singleInstance = GetPrivateProfileIntW(L"Settings", L"EnforceSingleInstance", 1, path.c_str()) == 1;
    alwaysOnTop = GetPrivateProfileIntW(L"Settings", L"AlwaysOnTop", 0, path.c_str()) == 1;

    int bgChoice = GetPrivateProfileIntW(L"Settings", L"BackgroundColor", 0, path.c_str());
    if (bgChoice < 0 || bgChoice > 3) bgChoice = 0;
    g_ctx.bgColor = static_cast<BackgroundColor>(bgChoice);

    int zoomChoice = GetPrivateProfileIntW(L"Settings", L"DefaultZoomMode", 0, path.c_str());
    if (zoomChoice < 0 || zoomChoice > 1) zoomChoice = 0;
    g_ctx.defaultZoomMode = static_cast<DefaultZoomMode>(zoomChoice);

    rect.left = GetPrivateProfileIntW(L"Window", L"left", CW_USEDEFAULT, path.c_str());
    rect.top = GetPrivateProfileIntW(L"Window", L"top", CW_USEDEFAULT, path.c_str());
    rect.right = GetPrivateProfileIntW(L"Window", L"right", CW_USEDEFAULT, path.c_str());
    rect.bottom = GetPrivateProfileIntW(L"Window", L"bottom", CW_USEDEFAULT, path.c_str());

    if (rect.left == CW_USEDEFAULT || rect.top == CW_USEDEFAULT || rect.right == CW_USEDEFAULT || rect.bottom == CW_USEDEFAULT) {
        SetRectEmpty(&rect);
    }
}

void WriteSettings(const std::wstring& path, const RECT& rect, bool fullscreen, bool singleInstance, bool alwaysOnTop) {
    auto writeInt = [&](LPCWSTR section, LPCWSTR key, int val) {
        WritePrivateProfileStringW(section, key, std::to_wstring(val).c_str(), path.c_str());
        };

    writeInt(L"Settings", L"StartFullScreen", fullscreen ? 1 : 0);
    writeInt(L"Settings", L"EnforceSingleInstance", singleInstance ? 1 : 0);
    writeInt(L"Settings", L"AlwaysOnTop", alwaysOnTop ? 1 : 0);
    writeInt(L"Settings", L"BackgroundColor", static_cast<int>(g_ctx.bgColor));
    writeInt(L"Settings", L"DefaultZoomMode", static_cast<int>(g_ctx.defaultZoomMode));

    if (!IsRectEmpty(&rect) && !IsIconic(g_ctx.hWnd) && !IsZoomed(g_ctx.hWnd)) {
        writeInt(L"Window", L"left", rect.left);
        writeInt(L"Window", L"top", rect.top);
        writeInt(L"Window", L"right", rect.right);
        writeInt(L"Window", L"bottom", rect.bottom);
    }
}