#include "viewer.h"
#include <string>

extern AppContext g_ctx;

void ReadSettings(const std::wstring& path, RECT& rect, bool& fullscreen, bool& singleInstance, bool& alwaysOnTop) {
    auto getInt = [&](LPCWSTR sec, LPCWSTR key, int def) { return GetPrivateProfileIntW(sec, key, def, path.c_str()); };
    fullscreen = getInt(L"Settings", L"StartFullScreen", 0) == 1;
    singleInstance = getInt(L"Settings", L"EnforceSingleInstance", 1) == 1;
    alwaysOnTop = getInt(L"Settings", L"AlwaysOnTop", 0) == 1;

    int bgChoice = getInt(L"Settings", L"BackgroundColor", 0);
    g_ctx.bgColor = static_cast<BackgroundColor>((bgChoice < 0 || bgChoice > 3) ? 0 : bgChoice);

    int zoomChoice = getInt(L"Settings", L"DefaultZoomMode", 0);
    g_ctx.defaultZoomMode = static_cast<DefaultZoomMode>((zoomChoice < 0 || zoomChoice > 1) ? 0 : zoomChoice);

    rect.left = getInt(L"Window", L"left", CW_USEDEFAULT);
    rect.top = getInt(L"Window", L"top", CW_USEDEFAULT);
    rect.right = getInt(L"Window", L"right", CW_USEDEFAULT);
    rect.bottom = getInt(L"Window", L"bottom", CW_USEDEFAULT);
    if (rect.left == CW_USEDEFAULT || rect.top == CW_USEDEFAULT || rect.right == CW_USEDEFAULT || rect.bottom == CW_USEDEFAULT) SetRectEmpty(&rect);
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