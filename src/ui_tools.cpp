#include "viewer.h"
#include <stdio.h>

extern AppContext g_ctx;

void UpdateViewToCurrentFrame() {
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        if (g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size()) {
            g_ctx.wicConverterOriginal = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
        }
    }
    // Re-apply rotation, brightness, etc. to the new frame
    ApplyEffectsToView();
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void UpdateEyedropperColor(POINT pt) {
    g_ctx.didCopyColor = false;
    float localX = 0, localY = 0;
    UINT imgWidth = 0, imgHeight = 0;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        g_ctx.colorStringRgb = L"N/A";
        g_ctx.colorStringHex = L"";
        return;
    }

    ConvertWindowToImagePoint(pt, localX, localY);

    BYTE r = 0, g = 0, b = 0;
    bool inImage = (localX >= 0 && localX < imgWidth && localY >= 0 && localY < imgHeight);
    if (inImage) {
        CriticalSectionLock lock(g_ctx.wicMutex);
        ComPtr<IWICBitmapSource> source;
        if (g_ctx.isAnimated && g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size()) {
            source = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
        }
        else {
            source = g_ctx.wicConverter;
        }

        if (source) {
            WICRect rc = { static_cast<INT>(localX), static_cast<INT>(localY), 1, 1 };
            UINT32 pixelData = 0;
            HRESULT hr = source->CopyPixels(&rc, 4, 4, reinterpret_cast<BYTE*>(&pixelData));
            if (SUCCEEDED(hr)) {
                b = (pixelData) & 0xFF;
                g = (pixelData >> 8) & 0xFF;
                r = (pixelData >> 16) & 0xFF;
                BYTE a = (pixelData >> 24) & 0xFF;

                if (a != 0 && a != 255) {
                    r = (r * 255) / a;
                    g = (g * 255) / a;
                    b = (b * 255) / a;
                }
            }
        }
    }
    else {
        switch (g_ctx.bgColor) {
        case BackgroundColor::Black:
            r = 0;
            g = 0; b = 0;
            break;
        case BackgroundColor::White:
            r = 255;
            g = 255; b = 255;
            break;
        case BackgroundColor::Grey:
            r = 30;
            g = 30; b = 30;
            break;
        case BackgroundColor::Transparent:
            g_ctx.colorStringRgb = L"N/A (Transparent BG)";
            g_ctx.colorStringHex = L"";
            return;
        }
    }

    g_ctx.hoveredColor = RGB(r, g, b);
    wchar_t rgbBuf[32];
    swprintf_s(rgbBuf, L"RGB(%d, %d, %d)", r, g, b);
    g_ctx.colorStringRgb = rgbBuf;

    wchar_t hexBuf[16];
    swprintf_s(hexBuf, L"#%02X%02X%02X", r, g, b);
    g_ctx.colorStringHex = hexBuf;
}

void HandleEyedropperClick() {
    if (g_ctx.colorStringHex.empty()) return;

    if (!OpenClipboard(g_ctx.hWnd)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (g_ctx.colorStringHex.length() + 1) * sizeof(wchar_t));
    if (hMem) {
        LPWSTR pMem = static_cast<LPWSTR>(GlobalLock(hMem));
        if (pMem) {
            wcscpy_s(pMem, g_ctx.colorStringHex.length() + 1, g_ctx.colorStringHex.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
            g_ctx.didCopyColor = true;
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        }
    }
    CloseClipboard();
}

void ToggleFullScreen() {
    if (!g_ctx.isFullScreen) {
        g_ctx.savedStyle = GetWindowLong(g_ctx.hWnd, GWL_STYLE);
        GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        HMONITOR hMonitor = MonitorFromWindow(g_ctx.hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, g_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_ctx.isFullScreen = true;
    }
    else {
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, g_ctx.savedStyle | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, g_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, g_ctx.savedRect.left, g_ctx.savedRect.top,
            g_ctx.savedRect.right - g_ctx.savedRect.left, g_ctx.savedRect.bottom - g_ctx.savedRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_ctx.isFullScreen = false;
    }
    FitImageToWindow();
}