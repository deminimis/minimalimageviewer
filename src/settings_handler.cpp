#include "viewer.h"
#include <string>

extern AppContext g_ctx;

void ReadSettings(const std::wstring& path, RECT& rect, bool& fullscreen) {
    fullscreen = GetPrivateProfileIntW(L"Settings", L"StartFullScreen", 0, path.c_str()) == 1;

    int bgChoice = GetPrivateProfileIntW(L"Settings", L"BackgroundColor", 0, path.c_str());
    if (bgChoice < 0 || bgChoice > 3) bgChoice = 0;
    g_ctx.bgColor = static_cast<BackgroundColor>(bgChoice);

    rect.left = GetPrivateProfileIntW(L"Window", L"left", CW_USEDEFAULT, path.c_str());
    rect.top = GetPrivateProfileIntW(L"Window", L"top", CW_USEDEFAULT, path.c_str());
    rect.right = GetPrivateProfileIntW(L"Window", L"right", CW_USEDEFAULT, path.c_str());
    rect.bottom = GetPrivateProfileIntW(L"Window", L"bottom", CW_USEDEFAULT, path.c_str());

    if (rect.left == CW_USEDEFAULT || rect.top == CW_USEDEFAULT || rect.right == CW_USEDEFAULT || rect.bottom == CW_USEDEFAULT) {
        SetRectEmpty(&rect);
    }
}

void WriteSettings(const std::wstring& path, const RECT& rect, bool fullscreen) {
    std::wstring fs_val = fullscreen ? L"1" : L"0";
    WritePrivateProfileStringW(L"Settings", L"StartFullScreen", fs_val.c_str(), path.c_str());

    std::wstring bg_val = std::to_wstring(static_cast<int>(g_ctx.bgColor));
    WritePrivateProfileStringW(L"Settings", L"BackgroundColor", bg_val.c_str(), path.c_str());

    if (!IsRectEmpty(&rect) && !IsIconic(g_ctx.hWnd) && !IsZoomed(g_ctx.hWnd)) {
        WritePrivateProfileStringW(L"Window", L"left", std::to_wstring(rect.left).c_str(), path.c_str());
        WritePrivateProfileStringW(L"Window", L"top", std::to_wstring(rect.top).c_str(), path.c_str());
        WritePrivateProfileStringW(L"Window", L"right", std::to_wstring(rect.right).c_str(), path.c_str());
        WritePrivateProfileStringW(L"Window", L"bottom", std::to_wstring(rect.bottom).c_str(), path.c_str());
    }
}