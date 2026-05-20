#include "viewer.h"
#include <string>



void ViewerApp::ReadSettings(const std::wstring& path, WINDOWPLACEMENT& wp, bool& fullscreen, bool& singleInstance, bool& alwaysOnTop) {
    auto getInt = [&](LPCWSTR sec, LPCWSTR key, int def) { return GetPrivateProfileIntW(sec, key, def, path.c_str());
        };
    fullscreen = getInt(L"Settings", L"StartFullScreen", 0) == 1;
    singleInstance = getInt(L"Settings", L"EnforceSingleInstance", 1) == 1;
    m_ctx.alwaysOnTop = getInt(L"Settings", L"AlwaysOnTop", 0) == 1;
    m_ctx.smoothScaling = getInt(L"Settings", L"SmoothScaling", 1) == 1;
    m_ctx.enableFadeAnimation = getInt(L"Settings", L"EnableFadeAnimation", 1) == 1;
    m_ctx.isOsdVisible = getInt(L"Settings", L"ShowOSD", 0) == 1;
    m_ctx.askToDelete = getInt(L"Settings", L"AskToDelete", 1) == 1;
    m_ctx.isAutoRefresh = getInt(L"Settings", L"AutoRefresh", 0) == 1;

    int bgChoice = getInt(L"Settings", L"BackgroundColor", 0);
    m_ctx.bgColor = static_cast<BackgroundColor>((bgChoice < 0 || bgChoice > 3) ? 0 : bgChoice);

    int zoomChoice = getInt(L"Settings", L"DefaultZoomMode", 0);
    m_ctx.defaultZoomMode = static_cast<DefaultZoomMode>((zoomChoice < 0 || zoomChoice > 1) ? 0 : zoomChoice);

    wp.length = sizeof(WINDOWPLACEMENT);
    wp.rcNormalPosition.left = CW_USEDEFAULT;
    wp.showCmd = SW_SHOWNORMAL;
    // Setup default before attempting read
    GetPrivateProfileStructW(L"Window", L"Placement", &wp, sizeof(WINDOWPLACEMENT), path.c_str());

    const wchar_t* keyNames[Act_Count] = { L"Next", L"Prev", L"ZoomIn", L"ZoomOut", L"Fit", L"Actual", L"Fullscreen", L"RotateCW", L"RotateCCW", L"Flip", L"Crop", L"CustomZoom", L"Exit" };
    const WORD defaultKeys[Act_Count] = { VK_RIGHT, VK_LEFT, MAKEWORD(VK_ADD, HOTKEYF_CONTROL), MAKEWORD(VK_SUBTRACT, HOTKEYF_CONTROL), MAKEWORD('0', HOTKEYF_CONTROL), MAKEWORD(VK_MULTIPLY, HOTKEYF_CONTROL), VK_F11, VK_UP, VK_DOWN, 'F', 'C', MAKEWORD('Z', HOTKEYF_CONTROL | HOTKEYF_SHIFT), VK_ESCAPE };

    for (int i = 0; i < Act_Count; ++i) {
        m_ctx.hotkeys[i] = getInt(L"Keys", keyNames[i], defaultKeys[i]);
    }
}

void ViewerApp::WriteSettings(const std::wstring& path, const WINDOWPLACEMENT& wp, bool fullscreen, bool singleInstance, bool alwaysOnTop) {
    auto writeInt = [&](LPCWSTR section, LPCWSTR key, int val) {
        WritePrivateProfileStringW(section, key, std::to_wstring(val).c_str(), path.c_str());
        };

    writeInt(L"Settings", L"StartFullScreen", fullscreen ? 1 : 0);
    writeInt(L"Settings", L"EnforceSingleInstance", singleInstance ? 1 : 0);
    writeInt(L"Settings", L"AlwaysOnTop", alwaysOnTop ? 1 : 0);
    writeInt(L"Settings", L"SmoothScaling", m_ctx.smoothScaling ? 1 : 0);
    writeInt(L"Settings", L"EnableFadeAnimation", m_ctx.enableFadeAnimation ? 1 : 0);
    writeInt(L"Settings", L"ShowOSD", m_ctx.isOsdVisible ? 1 : 0);
    writeInt(L"Settings", L"AskToDelete", m_ctx.askToDelete ? 1 : 0);
    writeInt(L"Settings", L"AutoRefresh", m_ctx.isAutoRefresh ? 1 : 0);
    writeInt(L"Settings", L"BackgroundColor", static_cast<int>(m_ctx.bgColor));
    writeInt(L"Settings", L"DefaultZoomMode", static_cast<int>(m_ctx.defaultZoomMode));

    const wchar_t* keyNames[Act_Count] = { L"Next", L"Prev", L"ZoomIn", L"ZoomOut", L"Fit", L"Actual", L"Fullscreen", L"RotateCW", L"RotateCCW", L"Flip", L"Crop", L"CustomZoom", L"Exit" };
    for (int i = 0; i < Act_Count; ++i) {
        writeInt(L"Keys", keyNames[i], m_ctx.hotkeys[i]);
    }

    if (!IsIconic(m_ctx.hWnd) && wp.rcNormalPosition.left != CW_USEDEFAULT) {
        WritePrivateProfileStructW(L"Window", L"Placement", const_cast<WINDOWPLACEMENT*>(&wp), sizeof(WINDOWPLACEMENT), path.c_str());
    }

    // Force flush INI cache 
    WritePrivateProfileStringW(NULL, NULL, NULL, path.c_str());
}

void ViewerApp::UpdateAcceleratorTable() {
    m_ctx.hAccelTable.reset();

    std::vector<ACCEL> accels;
    auto addAccel = [&](WORD virtKey, BYTE modifiers, WORD cmd) {
        ACCEL a = {};
        a.cmd = cmd;
        a.key = virtKey;
        a.fVirt = FVIRTKEY;
        if (modifiers & HOTKEYF_CONTROL) a.fVirt |= FCONTROL;
        if (modifiers & HOTKEYF_SHIFT) a.fVirt |= FSHIFT;
        if (modifiers & HOTKEYF_ALT) a.fVirt |= FALT;
        accels.push_back(a);
        };

    // Map configurable actions dynamically
    WORD actionCmds[Act_Count] = {
        IDM_NEXT_IMG, IDM_PREV_IMG, IDM_ZOOM_IN, IDM_ZOOM_OUT, IDM_FIT_TO_WINDOW,
        IDM_ACTUAL_SIZE, IDM_FULLSCREEN, IDM_ROTATE_CW, IDM_ROTATE_CCW, IDM_FLIP,
        IDM_CROP, IDM_CUSTOM_ZOOM, IDM_EXIT
    };

    for (int i = 0; i < Act_Count; ++i) {
        if (m_ctx.hotkeys[i]) {
            addAccel(LOBYTE(m_ctx.hotkeys[i]), HIBYTE(m_ctx.hotkeys[i]), actionCmds[i]);
        }
    }

    // Fallback shortcuts
    addAccel(VK_DELETE, 0, IDM_DELETE_IMG);
    addAccel('O', HOTKEYF_CONTROL, IDM_OPEN);
    addAccel('Z', HOTKEYF_CONTROL, IDM_UNDO);
    addAccel('S', HOTKEYF_CONTROL, IDM_SAVE);
    addAccel('S', HOTKEYF_CONTROL | HOTKEYF_SHIFT, IDM_SAVE_AS);
    addAccel('C', HOTKEYF_CONTROL, IDM_COPY);
    addAccel('V', HOTKEYF_CONTROL, IDM_PASTE);
    addAccel('0', HOTKEYF_CONTROL, IDM_CENTER_IMAGE);
    addAccel(VK_RETURN, 0, IDM_COMMIT_CROP);
    addAccel('I', 0, IDM_TOGGLE_OSD);
    addAccel(VK_SPACE, 0, IDM_PLAY_PAUSE);
    addAccel(VK_SPACE, HOTKEYF_SHIFT, IDM_RESUME_ANIM);
    addAccel(VK_RIGHT, HOTKEYF_SHIFT, IDM_ANIM_NEXT_FRAME);
    addAccel(VK_LEFT, HOTKEYF_SHIFT, IDM_ANIM_PREV_FRAME);
    addAccel(VK_UP, HOTKEYF_SHIFT, IDM_ANIM_FIRST_FRAME);
    addAccel(VK_DOWN, HOTKEYF_SHIFT, IDM_ANIM_FIRST_FRAME);
    addAccel(VK_F5, 0, IDM_REFRESH);

    if (!accels.empty()) {
        m_ctx.hAccelTable.reset(CreateAcceleratorTableW(accels.data(), static_cast<int>(accels.size())));
    }
}