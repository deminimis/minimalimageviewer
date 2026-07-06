#include "viewer.h"
#include <stdio.h>
#include <format>

void ViewerApp::UpdateWindowTitle() {
    const std::wstring appNameAndVersion = L"Minimal Image Viewer v2.0.3";

    if (m_ctx.loadingFilePath.empty()) {
        SetWindowTextW(m_ctx.hWnd, appNameAndVersion.c_str());
        return;
    }

    std::wstring title = m_ctx.loadingFilePath;
    if (m_ctx.animationFrameDelays.size() > 1) {
        title = std::format(L"{} (Frame {}/{}) - {}", m_ctx.loadingFilePath, m_ctx.currentAnimationFrame + 1, m_ctx.animationFrameDelays.size(), appNameAndVersion);
    }
    else {
        title = std::format(L"{} - {}", m_ctx.loadingFilePath, appNameAndVersion);
    }
    SetWindowTextW(m_ctx.hWnd, title.c_str());
}

void ViewerApp::UpdateViewToCurrentFrame() {
    {
        std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
        if (!m_ctx.animationFrameDelays.empty()) {
            m_ctx.currentAnimatedConverter = GetCompositedAnimationFrame(m_ctx.currentAnimationFrame);
            m_ctx.wicConverterOriginal = m_ctx.currentAnimatedConverter;
            m_ctx.wicConverter = m_ctx.currentAnimatedConverter;
            m_ctx.d2dBitmap = nullptr;
        }
    }
    ApplyEffectsToView();
    UpdateWindowTitle();
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::ToggleFullScreen() {
    if (!m_ctx.isFullScreen) {
        m_ctx.savedStyle = GetWindowLong(m_ctx.hWnd, GWL_STYLE);

        m_ctx.windowPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_ctx.hWnd, &m_ctx.windowPlacement);

        HMONITOR hMonitor = MonitorFromWindow(m_ctx.hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);

        SetWindowLong(m_ctx.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

        // Do not make fullscreen topmost
        SetWindowPos(
            m_ctx.hWnd,
            HWND_TOP,
            mi.rcMonitor.left,
            mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW
        );

        m_ctx.isFullScreen = true;
    }
    else {
        SetWindowLong(m_ctx.hWnd, GWL_STYLE, m_ctx.savedStyle | WS_VISIBLE);
        SetWindowPlacement(m_ctx.hWnd, &m_ctx.windowPlacement);

        SetWindowPos(
            m_ctx.hWnd,
            m_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW
        );

        m_ctx.isFullScreen = false;
    }

    FitImageToWindow();
}