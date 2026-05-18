#include "viewer.h"
#include <string>



void ViewerApp::ReadSettings(const std::wstring& path, RECT& rect, bool& fullscreen, bool& singleInstance, bool& alwaysOnTop) {
    auto getInt = [&](LPCWSTR sec, LPCWSTR key, int def) { return GetPrivateProfileIntW(sec, key, def, path.c_str()); };
    fullscreen = getInt(L"Settings", L"StartFullScreen", 0) == 1;
    singleInstance = getInt(L"Settings", L"EnforceSingleInstance", 1) == 1;
    m_ctx.alwaysOnTop = getInt(L"Settings", L"AlwaysOnTop", 0) == 1;
    m_ctx.smoothScaling = getInt(L"Settings", L"SmoothScaling", 1) == 1;
    m_ctx.enableFadeAnimation = getInt(L"Settings", L"EnableFadeAnimation", 1) == 1;
    m_ctx.isOsdVisible = getInt(L"Settings", L"ShowOSD", 0) == 1;

    int bgChoice = getInt(L"Settings", L"BackgroundColor", 0);
    m_ctx.bgColor = static_cast<BackgroundColor>((bgChoice < 0 || bgChoice > 3) ? 0 : bgChoice);

    int zoomChoice = getInt(L"Settings", L"DefaultZoomMode", 0);
    m_ctx.defaultZoomMode = static_cast<DefaultZoomMode>((zoomChoice < 0 || zoomChoice > 1) ? 0 : zoomChoice);

    rect.left = getInt(L"Window", L"left", CW_USEDEFAULT);
    rect.top = getInt(L"Window", L"top", CW_USEDEFAULT);
    rect.right = getInt(L"Window", L"right", CW_USEDEFAULT);
    rect.bottom = getInt(L"Window", L"bottom", CW_USEDEFAULT);
    if (rect.left == CW_USEDEFAULT || rect.top == CW_USEDEFAULT || rect.right == CW_USEDEFAULT || rect.bottom == CW_USEDEFAULT) SetRectEmpty(&rect);

    const wchar_t* keyNames[Act_Count] = { L"Next", L"Prev", L"ZoomIn", L"ZoomOut", L"Fit", L"Actual", L"Fullscreen", L"RotateCW", L"RotateCCW", L"Flip", L"Crop", L"CustomZoom", L"Exit" };
    const WORD defaultKeys[Act_Count] = { VK_RIGHT, VK_LEFT, MAKEWORD(VK_ADD, HOTKEYF_CONTROL), MAKEWORD(VK_SUBTRACT, HOTKEYF_CONTROL), MAKEWORD('0', HOTKEYF_CONTROL), MAKEWORD(VK_MULTIPLY, HOTKEYF_CONTROL), VK_F11, VK_UP, VK_DOWN, 'F', 'C', MAKEWORD('Z', HOTKEYF_CONTROL | HOTKEYF_SHIFT), VK_ESCAPE };

    for (int i = 0; i < Act_Count; ++i) {
        m_ctx.hotkeys[i] = getInt(L"Keys", keyNames[i], defaultKeys[i]);
    }
}

void ViewerApp::WriteSettings(const std::wstring& path, const RECT& rect, bool fullscreen, bool singleInstance, bool alwaysOnTop) {
    auto writeInt = [&](LPCWSTR section, LPCWSTR key, int val) {
        WritePrivateProfileStringW(section, key, std::to_wstring(val).c_str(), path.c_str());
        };

    writeInt(L"Settings", L"StartFullScreen", fullscreen ? 1 : 0);
    writeInt(L"Settings", L"EnforceSingleInstance", singleInstance ? 1 : 0);
    writeInt(L"Settings", L"AlwaysOnTop", alwaysOnTop ? 1 : 0);
    writeInt(L"Settings", L"SmoothScaling", m_ctx.smoothScaling ? 1 : 0);
    writeInt(L"Settings", L"EnableFadeAnimation", m_ctx.enableFadeAnimation ? 1 : 0);
    writeInt(L"Settings", L"ShowOSD", m_ctx.isOsdVisible ? 1 : 0);
    writeInt(L"Settings", L"BackgroundColor", static_cast<int>(m_ctx.bgColor));
    writeInt(L"Settings", L"DefaultZoomMode", static_cast<int>(m_ctx.defaultZoomMode));

    const wchar_t* keyNames[Act_Count] = { L"Next", L"Prev", L"ZoomIn", L"ZoomOut", L"Fit", L"Actual", L"Fullscreen", L"RotateCW", L"RotateCCW", L"Flip", L"Crop", L"CustomZoom", L"Exit" };
    for (int i = 0; i < Act_Count; ++i) {
        writeInt(L"Keys", keyNames[i], m_ctx.hotkeys[i]);
    }

    if (!IsRectEmpty(&rect) && !IsIconic(m_ctx.hWnd) && !IsZoomed(m_ctx.hWnd)) {
        writeInt(L"Window", L"top", rect.top);
        writeInt(L"Window", L"right", rect.right);
        writeInt(L"Window", L"bottom", rect.bottom);
    }
}