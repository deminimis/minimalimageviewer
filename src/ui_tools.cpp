#include "viewer.h"
#include <stdio.h>



void ViewerApp::UpdateWindowTitle() {
    if (m_ctx.loadingFilePath.empty()) {
        SetWindowTextW(m_ctx.hWnd, L"Minimal Image Viewer");
        return;
    }
    std::wstring title = m_ctx.loadingFilePath;
    if (m_ctx.animationFrameConverters.size() > 1) {
        title += L" (Frame " + std::to_wstring(m_ctx.currentAnimationFrame + 1) + L"/" + std::to_wstring(m_ctx.animationFrameConverters.size()) + L")";
    }
    SetWindowTextW(m_ctx.hWnd, title.c_str());
}

void ViewerApp::UpdateViewToCurrentFrame() {
    {
        CriticalSectionLock lock(m_ctx.wicMutex);
        if (m_ctx.currentAnimationFrame < m_ctx.animationFrameConverters.size()) {
            m_ctx.wicConverterOriginal = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
        }
    }
    // Re-apply rotation, flip, grayscale, etc. to the new frame
    ApplyEffectsToView();
    UpdateWindowTitle();
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::UpdateEyedropperColor(POINT pt) {
    m_ctx.didCopyColor = false;
    float localX = 0, localY = 0;
    UINT imgWidth = 0, imgHeight = 0;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        m_ctx.colorStringRgb = L"N/A";
        m_ctx.colorStringHex = L"";
        return;
    }

    ConvertWindowToImagePoint(pt, localX, localY);

    BYTE r = 0, g = 0, b = 0;
    bool inImage = (localX >= 0 && localX < imgWidth && localY >= 0 && localY < imgHeight);
    if (inImage) {
        CriticalSectionLock lock(m_ctx.wicMutex);
        ComPtr<IWICBitmapSource> source;
        if (m_ctx.isAnimated && m_ctx.currentAnimationFrame < m_ctx.animationFrameConverters.size()) {
            source = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
        }
        else {
            source = m_ctx.wicConverter;
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
        switch (m_ctx.bgColor) {
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
            m_ctx.colorStringRgb = L"N/A (Transparent BG)";
            m_ctx.colorStringHex = L"";
            return;
        }
    }

    m_ctx.hoveredColor = RGB(r, g, b);
    wchar_t rgbBuf[32];
    swprintf_s(rgbBuf, L"RGB(%d, %d, %d)", r, g, b);
    m_ctx.colorStringRgb = rgbBuf;

    wchar_t hexBuf[16];
    swprintf_s(hexBuf, L"#%02X%02X%02X", r, g, b);
    m_ctx.colorStringHex = hexBuf;
}

void ViewerApp::HandleEyedropperClick() {
    if (m_ctx.colorStringHex.empty()) return;

    if (!OpenClipboard(m_ctx.hWnd)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (m_ctx.colorStringHex.length() + 1) * sizeof(wchar_t));
    if (hMem) {
        LPWSTR pMem = static_cast<LPWSTR>(GlobalLock(hMem));
        if (pMem) {
            wcscpy_s(pMem, m_ctx.colorStringHex.length() + 1, m_ctx.colorStringHex.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
            m_ctx.didCopyColor = true;
            InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
        }
    }
    CloseClipboard();
}

void ViewerApp::ToggleFullScreen() {
    if (!m_ctx.isFullScreen) {
        m_ctx.savedStyle = GetWindowLong(m_ctx.hWnd, GWL_STYLE);
        GetWindowRect(m_ctx.hWnd, &m_ctx.savedRect);
        HMONITOR hMonitor = MonitorFromWindow(m_ctx.hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);
        SetWindowLong(m_ctx.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_ctx.hWnd, m_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_ctx.isFullScreen = true;
    }
    else {
        SetWindowLong(m_ctx.hWnd, GWL_STYLE, m_ctx.savedStyle | WS_VISIBLE);
        SetWindowPos(m_ctx.hWnd, m_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, m_ctx.savedRect.left, m_ctx.savedRect.top,
            m_ctx.savedRect.right - m_ctx.savedRect.left, m_ctx.savedRect.bottom - m_ctx.savedRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_ctx.isFullScreen = false;
    }
    FitImageToWindow();
}