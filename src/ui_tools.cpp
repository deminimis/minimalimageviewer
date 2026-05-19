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
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
        if (m_ctx.currentAnimationFrame < m_ctx.animationFrameConverters.size()) {
            m_ctx.wicConverterOriginal = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
        }
    }
    // Re-apply rotation, flip, grayscale, etc. to the new frame
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
        SetWindowPos(m_ctx.hWnd, m_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_ctx.isFullScreen = true;
    }
    else {
        SetWindowLong(m_ctx.hWnd, GWL_STYLE, m_ctx.savedStyle | WS_VISIBLE);
        SetWindowPlacement(m_ctx.hWnd, &m_ctx.windowPlacement);
        SetWindowPos(m_ctx.hWnd, m_ctx.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_ctx.isFullScreen = false;
    }
    FitImageToWindow();
}