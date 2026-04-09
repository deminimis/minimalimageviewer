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

    g_ctx.hotkeys[Act_Next] = getInt(L"Keys", L"Next", VK_RIGHT);
    g_ctx.hotkeys[Act_Prev] = getInt(L"Keys", L"Prev", VK_LEFT);
    g_ctx.hotkeys[Act_ZoomIn] = getInt(L"Keys", L"ZoomIn", MAKEWORD(VK_ADD, HOTKEYF_CONTROL));
    g_ctx.hotkeys[Act_ZoomOut] = getInt(L"Keys", L"ZoomOut", MAKEWORD(VK_SUBTRACT, HOTKEYF_CONTROL));
    g_ctx.hotkeys[Act_Fit] = getInt(L"Keys", L"Fit", MAKEWORD('0', HOTKEYF_CONTROL));
    g_ctx.hotkeys[Act_Actual] = getInt(L"Keys", L"Actual", MAKEWORD(VK_MULTIPLY, HOTKEYF_CONTROL));
    g_ctx.hotkeys[Act_Fullscreen] = getInt(L"Keys", L"Fullscreen", VK_F11);
    g_ctx.hotkeys[Act_RotateCW] = getInt(L"Keys", L"RotateCW", VK_UP);
    g_ctx.hotkeys[Act_RotateCCW] = getInt(L"Keys", L"RotateCCW", VK_DOWN);
    g_ctx.hotkeys[Act_Flip] = getInt(L"Keys", L"Flip", 'F');
    g_ctx.hotkeys[Act_Crop] = getInt(L"Keys", L"Crop", 'C');
    g_ctx.hotkeys[Act_Exit] = getInt(L"Keys", L"Exit", VK_ESCAPE);
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

    writeInt(L"Keys", L"Next", g_ctx.hotkeys[Act_Next]);
    writeInt(L"Keys", L"Prev", g_ctx.hotkeys[Act_Prev]);
    writeInt(L"Keys", L"ZoomIn", g_ctx.hotkeys[Act_ZoomIn]);
    writeInt(L"Keys", L"ZoomOut", g_ctx.hotkeys[Act_ZoomOut]);
    writeInt(L"Keys", L"Fit", g_ctx.hotkeys[Act_Fit]);
    writeInt(L"Keys", L"Actual", g_ctx.hotkeys[Act_Actual]);
    writeInt(L"Keys", L"Fullscreen", g_ctx.hotkeys[Act_Fullscreen]);
    writeInt(L"Keys", L"RotateCW", g_ctx.hotkeys[Act_RotateCW]);
    writeInt(L"Keys", L"RotateCCW", g_ctx.hotkeys[Act_RotateCCW]);
    writeInt(L"Keys", L"Flip", g_ctx.hotkeys[Act_Flip]);
    writeInt(L"Keys", L"Crop", g_ctx.hotkeys[Act_Crop]);
    writeInt(L"Keys", L"Exit", g_ctx.hotkeys[Act_Exit]);

    if (!IsRectEmpty(&rect) && !IsIconic(g_ctx.hWnd) && !IsZoomed(g_ctx.hWnd)) {
        writeInt(L"Window", L"top", rect.top);
        writeInt(L"Window", L"right", rect.right);
        writeInt(L"Window", L"bottom", rect.bottom);
    }
}