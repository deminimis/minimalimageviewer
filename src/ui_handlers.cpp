#include "viewer.h"
#include "exif_utils.h"
#include <string>
#include <stdio.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")





void ViewerApp::OnPaint(HWND hWnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hWnd, &ps);

    if (m_ctx.isInitialized) {
        Render();
    }

    EndPaint(hWnd, &ps);
}

bool ViewerApp::CheckHotkey(WORD hk, WPARAM wParam) {
    if (!hk || LOBYTE(hk) != wParam) return false;
    BYTE mods = HIBYTE(hk);
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return (ctrl == ((mods & HOTKEYF_CONTROL) != 0) &&
        shift == ((mods & HOTKEYF_SHIFT) != 0) &&
        alt == ((mods & HOTKEYF_ALT) != 0));
}

void ViewerApp::OnKeyDown(WPARAM wParam) {
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    auto isKey = [&](ActionID act) { return CheckHotkey(m_ctx.hotkeys[act], wParam); };

    if (isKey(Act_Next)) {
        if (!m_ctx.imageFiles.empty() && m_ctx.currentImageIndex != -1) {
            size_t size = m_ctx.imageFiles.size();
            m_ctx.currentImageIndex = (m_ctx.currentImageIndex + 1) % static_cast<int>(size);
            m_ctx.pendingNavIndex = m_ctx.currentImageIndex;
            m_ctx.startAtEnd = false;
            std::wstring title = PathFindFileNameW(m_ctx.imageFiles[m_ctx.currentImageIndex].c_str());
            title += L"  [Loading...]";
            SetWindowTextW(m_ctx.hWnd, title.c_str());
            SetTimer(m_ctx.hWnd, NAV_DEBOUNCE_TIMER_ID, 150, nullptr);
        }
    }
    else if (isKey(Act_Prev)) {
        if (!m_ctx.imageFiles.empty() && m_ctx.currentImageIndex != -1) {
            size_t size = m_ctx.imageFiles.size();
            m_ctx.currentImageIndex = (m_ctx.currentImageIndex - 1 + static_cast<int>(size)) % static_cast<int>(size);
            m_ctx.pendingNavIndex = m_ctx.currentImageIndex;
            m_ctx.startAtEnd = true;
            std::wstring title = PathFindFileNameW(m_ctx.imageFiles[m_ctx.currentImageIndex].c_str());
            title += L"  [Loading...]";
            SetWindowTextW(m_ctx.hWnd, title.c_str());
            SetTimer(m_ctx.hWnd, NAV_DEBOUNCE_TIMER_ID, 150, nullptr);
        }
    }
    else if (isKey(Act_ZoomIn)) {
        RECT cr; GetClientRect(m_ctx.hWnd, &cr);
        POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
        ZoomImage(1.25f, centerPt);
    }
    else if (isKey(Act_ZoomOut)) {
        RECT cr; GetClientRect(m_ctx.hWnd, &cr);
        POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
        ZoomImage(0.8f, centerPt);
    }
    else if (isKey(Act_Fit)) { FitImageToWindow(); }
    else if (isKey(Act_Actual)) { SetActualSize(); }
    else if (isKey(Act_CustomZoom)) { OpenZoomDialog(); }
    else if (isKey(Act_Fullscreen)) { ToggleFullScreen(); }
    else if (isKey(Act_RotateCW)) { RotateImage(true); }
    else if (isKey(Act_RotateCCW)) { RotateImage(false); }
    else if (isKey(Act_Flip)) { FlipImage(); }
    else if (isKey(Act_Crop)) {
        bool wasCropActive = m_ctx.isCropActive;
        m_ctx.isCropMode = !m_ctx.isCropMode;
        m_ctx.isCropActive = false; m_ctx.isCropPending = false; m_ctx.isSelectingCropRect = false;
        if (wasCropActive) { ApplyEffectsToView(); FitImageToWindow(); }
        InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
        SetCursor(LoadCursor(nullptr, m_ctx.isCropMode ? IDC_CROSS : IDC_ARROW));
    }
    else if (isKey(Act_Exit)) {
        if (m_ctx.isCropMode || m_ctx.isSelectingCropRect || m_ctx.isCropPending || m_ctx.isSelectingOcrRect || m_ctx.isEyedropperActive) {
            m_ctx.isCropMode = false; m_ctx.isSelectingCropRect = false; m_ctx.isCropPending = false;
            m_ctx.isSelectingOcrRect = false; m_ctx.isDraggingOcrRect = false; m_ctx.isEyedropperActive = false;
            m_ctx.ocrRectWindow = { 0 }; ApplyEffectsToView(); FitImageToWindow();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            if (GetCapture() == m_ctx.hWnd) ReleaseCapture();
            InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
        }
        else {
            PostQuitMessage(0);
        }
    }
    else {
        switch (wParam) {
        case VK_ESCAPE: 
            if (m_ctx.isCropMode || m_ctx.isSelectingCropRect || m_ctx.isCropPending || m_ctx.isSelectingOcrRect || m_ctx.isEyedropperActive) {
                m_ctx.isCropMode = false; m_ctx.isSelectingCropRect = false; m_ctx.isCropPending = false;
                m_ctx.isSelectingOcrRect = false; m_ctx.isDraggingOcrRect = false; m_ctx.isEyedropperActive = false;
                m_ctx.ocrRectWindow = { 0 }; ApplyEffectsToView(); FitImageToWindow();
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                if (GetCapture() == m_ctx.hWnd) ReleaseCapture();
                InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
            } break;
        case VK_DELETE: DeleteCurrentImage(); break;
        case 'O': if (ctrlPressed) OpenFileAction(); break;
        case 'Z':
            if (ctrlPressed && !m_ctx.undoStack.empty()) {
                CriticalSectionLock lock(m_ctx.wicMutex);
                m_ctx.wicConverterOriginal = m_ctx.undoStack.back();
                m_ctx.undoStack.pop_back(); m_ctx.isCropActive = false; m_ctx.cropRectLocal = { 0 };
                ApplyEffectsToView(); FitImageToWindow(); InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
            } break;
        case 'S': if (ctrlPressed && (GetKeyState(VK_SHIFT) & 0x8000)) SaveImageAs(); else if (ctrlPressed) SaveImage(); break;
        case 'C': if (ctrlPressed) HandleCopy(); break;
        case 'V': if (ctrlPressed) HandlePaste(); break;
        case '0': if (ctrlPressed) CenterImage(true); break;
        case VK_RETURN:
            if (m_ctx.isCropPending) {
                m_ctx.isCropActive = true; m_ctx.isCropPending = false; m_ctx.isCropMode = false;
                CommitCrop(); ApplyEffectsToView(); FitImageToWindow(); InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
            } break;
        case 'I': m_ctx.isOsdVisible = !m_ctx.isOsdVisible;
            InvalidateRect(m_ctx.hWnd, nullptr, FALSE); break;
        case 'Q': PerformOcr(); break;
        case 'W': m_ctx.isSelectingOcrRect = true; m_ctx.isDraggingOcrRect = false; m_ctx.isCropMode = false;
            m_ctx.isSelectingCropRect = false; m_ctx.isCropPending = false; m_ctx.isEyedropperActive = false; SetCursor(LoadCursor(nullptr, IDC_CROSS)); break;
        case VK_SPACE:
            // Gif playback controls
            if (m_ctx.animationFrameConverters.size() > 1) {
                if (shiftPressed) {
                    // Shift+Space: Resume playback 
                    if (!m_ctx.isAnimated) {
                        m_ctx.isAnimated = true;
                        UINT delay = m_ctx.animationFrameDelays[m_ctx.currentAnimationFrame];
                        SetTimer(m_ctx.hWnd, ANIMATION_TIMER_ID, delay > 0 ? delay : 100, nullptr);
                    }
                }
                else {
                    if (m_ctx.isAnimated) {
                        // Pause 
                        m_ctx.isAnimated = false;
                        KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
                    }
                    else {
                        // Advance one frame and loop
                        m_ctx.currentAnimationFrame = (m_ctx.currentAnimationFrame + 1) % m_ctx.animationFrameConverters.size();
                        UpdateViewToCurrentFrame();
                    }
                }
            }
            break;
        case VK_F5:
            if (!m_ctx.imageFiles.empty() && m_ctx.currentImageIndex != -1) {
                std::wstring currentFile = m_ctx.imageFiles[m_ctx.currentImageIndex];
                m_ctx.imageFiles.clear(); // Clear cache to force directory rescan
                LoadImageFromFile(currentFile);
            }
            break;
        }
    }
}



void ViewerApp::OnContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();

    auto addAction = [&](HMENU menu, UINT id, ActionID act, const wchar_t* text) {
        std::wstring hk = GetHotkeyString(m_ctx.hotkeys[act]);
        std::wstring label = text;
        if (!hk.empty()) label += L"\t" + hk;
        AppendMenuW(menu, MF_STRING, id, label.c_str());
        };

    AppendMenuW(hMenu, MF_STRING, IDM_OPEN, L"Open Image\tCtrl+O");
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"Refresh\tF5");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_COPY, L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    addAction(hMenu, IDM_NEXT_IMG, Act_Next, L"Next Image");
    addAction(hMenu, IDM_PREV_IMG, Act_Prev, L"Previous Image");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    HMENU hSortMenu = CreatePopupMenu();
    auto addSortItem = [&](UINT id, SortCriteria crit, bool asc, LPCWSTR text) {
        UINT flags = MF_STRING | ((m_ctx.currentSortCriteria == crit && m_ctx.isSortAscending == asc) ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(hSortMenu, flags, id, text);
        };
    addSortItem(IDM_SORT_BY_NAME_ASC, SortCriteria::ByName, true, L"Name (Ascending)");
    addSortItem(IDM_SORT_BY_NAME_DESC, SortCriteria::ByName, false, L"Name (Descending)");
    addSortItem(IDM_SORT_BY_DATE_ASC, SortCriteria::ByDateModified, true, L"Date Modified (Ascending)");
    addSortItem(IDM_SORT_BY_DATE_DESC, SortCriteria::ByDateModified, false, L"Date Modified (Descending)");
    addSortItem(IDM_SORT_BY_SIZE_ASC, SortCriteria::ByFileSize, true, L"File Size (Ascending)");
    addSortItem(IDM_SORT_BY_SIZE_DESC, SortCriteria::ByFileSize, false, L"File Size (Descending)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSortMenu, L"Sort By");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    HMENU hEditMenu = CreatePopupMenu();
    addAction(hEditMenu, IDM_ROTATE_CW, Act_RotateCW, L"Rotate Clockwise");
    addAction(hEditMenu, IDM_ROTATE_CCW, Act_RotateCCW, L"Rotate Counter-Clockwise");
    addAction(hEditMenu, IDM_FLIP, Act_Flip, L"Flip");
    AppendMenuW(hEditMenu, MF_STRING | (m_ctx.isGrayscale ? MF_CHECKED : MF_UNCHECKED), IDM_GRAYSCALE, L"Grayscale");
    addAction(hEditMenu, IDM_CROP, Act_Crop, L"Crop");
    AppendMenuW(hEditMenu, MF_STRING, IDM_RESIZE, L"Resize Image...");
    AppendMenuW(hEditMenu, MF_STRING, IDM_BRIGHTNESS_CONTRAST, L"Image Effects...");
    AppendMenuW(hEditMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hEditMenu, MF_STRING | (m_ctx.isEyedropperActive ? MF_CHECKED : MF_UNCHECKED), IDM_EYEDROPPER, L"Pick Color");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"Edit");

    HMENU hViewMenu = CreatePopupMenu();
    addAction(hViewMenu, IDM_ZOOM_IN, Act_ZoomIn, L"Zoom In");
    addAction(hViewMenu, IDM_ZOOM_OUT, Act_ZoomOut, L"Zoom Out");
    addAction(hViewMenu, IDM_ACTUAL_SIZE, Act_Actual, L"Actual Size (100%)");
    AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_200, L"Zoom 200%");
    AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_300, L"Zoom 300%");
    addAction(hViewMenu, IDM_FIT_TO_WINDOW, Act_Fit, L"Fit to Window");
    AppendMenuW(hViewMenu, MF_SEPARATOR, 0, nullptr);
    addAction(hViewMenu, IDM_FULLSCREEN, Act_Fullscreen, L"Full Screen");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"View");

    AppendMenuW(hMenu, MF_STRING, IDM_OCR, L"Copy Text (OCR)\tQ");
    AppendMenuW(hMenu, MF_STRING, IDM_OCR_AREA, L"Copy Text from Area\tW");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE, L"Save\tCtrl+S");
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE_AS, L"Save As\tCtrl+Shift+S");

    UINT locationFlags = (m_ctx.currentImageIndex != -1) ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(hMenu, locationFlags, IDM_OPEN_LOCATION, L"Open File Location");
    AppendMenuW(hMenu, locationFlags, IDM_PROPERTIES, L"Properties...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_PREFERENCES, L"Preferences...");
    AppendMenuW(hMenu, MF_STRING, IDM_KEYBINDINGS, L"Keybindings...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING, IDM_DELETE_IMG, L"Delete Image\tDelete");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit\tEsc");

    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_OPEN:          OpenFileAction(); break;
    case IDM_REFRESH:       OnKeyDown(VK_F5); break;
    case IDM_COPY:          HandleCopy(); break;
    case IDM_PASTE:         HandlePaste(); break;
    case IDM_NEXT_IMG:      OnKeyDown(VK_RIGHT); break;
    case IDM_PREV_IMG:      OnKeyDown(VK_LEFT); break;
    case IDM_ZOOM_IN: {
        POINT clientPt = pt;
        ScreenToClient(hWnd, &clientPt);
        ZoomImage(1.25f, clientPt);
        break;
    }
    case IDM_ZOOM_OUT: {
        POINT clientPt = pt;
        ScreenToClient(hWnd, &clientPt);
        ZoomImage(0.8f, clientPt);
        break;
    }
    case IDM_ACTUAL_SIZE:   SetActualSize(); break;
    case IDM_ZOOM_200:      SetZoomLevel(2.0f); break;
    case IDM_ZOOM_300:      SetZoomLevel(3.0f); break;
    case IDM_FIT_TO_WINDOW: FitImageToWindow(); break;
    case IDM_FULLSCREEN:    ToggleFullScreen(); break;
    case IDM_DELETE_IMG:    DeleteCurrentImage(); break;
    case IDM_EXIT:          PostQuitMessage(0); break;
    case IDM_ROTATE_CW:     RotateImage(true); break;
    case IDM_ROTATE_CCW:    RotateImage(false); break;
    case IDM_FLIP:          FlipImage(); break;
    case IDM_GRAYSCALE:
        m_ctx.isGrayscale = !m_ctx.isGrayscale;
        InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
        break;
    case IDM_CROP: {
        bool wasCropActive = m_ctx.isCropActive;
        m_ctx.isCropMode = true;
        m_ctx.isCropActive = false;
        m_ctx.isCropPending = false;
        m_ctx.isSelectingCropRect = false;

        if (wasCropActive) {
            ApplyEffectsToView();
            FitImageToWindow();
        }
        InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    }
    case IDM_RESIZE:        ResizeImageAction(); break;
    case IDM_BRIGHTNESS_CONTRAST: OpenBrightnessContrastDialog(); break;
    case IDM_EYEDROPPER:
        m_ctx.isEyedropperActive = !m_ctx.isEyedropperActive;
        m_ctx.didCopyColor = false;
        SetCursor(LoadCursor(nullptr, m_ctx.isEyedropperActive ? IDC_CROSS : IDC_ARROW));
        if (m_ctx.isEyedropperActive) SetCapture(m_ctx.hWnd);
        else ReleaseCapture();
        InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
        break;
    case IDM_OCR:           PerformOcr(); break;
    case IDM_OCR_AREA:
        m_ctx.isSelectingOcrRect = true;
        m_ctx.isDraggingOcrRect = false;
        m_ctx.isCropMode = false;
        m_ctx.isSelectingCropRect = false;
        m_ctx.isCropPending = false;
        m_ctx.isEyedropperActive = false;
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    case IDM_SAVE:          SaveImage(); break;
    case IDM_SAVE_AS:       SaveImageAs(); break;
    case IDM_OPEN_LOCATION: OpenFileLocationAction(); break;
    case IDM_PROPERTIES:    ShowImageProperties(); break;
    case IDM_PREFERENCES:   OpenPreferencesDialog(); break;
    case IDM_KEYBINDINGS:   OpenKeybindingsDialog(); break;

    case IDM_SORT_BY_NAME_ASC:
    case IDM_SORT_BY_NAME_DESC:
    case IDM_SORT_BY_DATE_ASC:
    case IDM_SORT_BY_DATE_DESC:
    case IDM_SORT_BY_SIZE_ASC:
    case IDM_SORT_BY_SIZE_DESC:
    {
        std::wstring currentFile;
        if (m_ctx.currentImageIndex >= 0 && m_ctx.currentImageIndex < static_cast<int>(m_ctx.imageFiles.size())) {
            currentFile = m_ctx.imageFiles[m_ctx.currentImageIndex];
        }

        m_ctx.isSortAscending = (cmd == IDM_SORT_BY_NAME_ASC || cmd == IDM_SORT_BY_DATE_ASC || cmd == IDM_SORT_BY_SIZE_ASC);
        if (cmd == IDM_SORT_BY_NAME_ASC || cmd == IDM_SORT_BY_NAME_DESC) m_ctx.currentSortCriteria = SortCriteria::ByName;
        else if (cmd == IDM_SORT_BY_DATE_ASC || cmd == IDM_SORT_BY_DATE_DESC) m_ctx.currentSortCriteria = SortCriteria::ByDateModified;
        else m_ctx.currentSortCriteria = SortCriteria::ByFileSize;

        if (!m_ctx.currentDirectory.empty()) {
            m_ctx.imageFiles.clear(); // force directory rescan
            LoadImageFromFile(currentFile);
        }
        break;
    }
    }
}




LRESULT CALLBACK ViewerApp::StaticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ViewerApp* pApp = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pApp = reinterpret_cast<ViewerApp*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pApp));
    }
    else {
        pApp = reinterpret_cast<ViewerApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pApp) {
        return pApp->WndProc(hWnd, message, wParam, lParam);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT ViewerApp::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static POINT dragStart = {};
    switch (message) {
    case WM_KILLFOCUS:
        if (m_ctx.isEyedropperActive) {
            m_ctx.isEyedropperActive = false;
            ReleaseCapture();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_SYSKEYDOWN:
        if (m_ctx.isEyedropperActive) {
            m_ctx.isEyedropperActive = false;
            ReleaseCapture();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_APP_IMAGE_READY:
        OnImageReady(wParam != 0, (int)lParam);
        break;
    case WM_APP_DIR_READY:
        OnDirReady((int)lParam);
        break;
    case WM_APP_IMAGE_LOADED:
        FinalizeImageLoad(true, static_cast<int>(wParam));
        break;
    case WM_APP_HQ_READY: {
        IWICBitmap* pBitmap = reinterpret_cast<IWICBitmap*>(wParam);
        if (pBitmap) {
            float bitmapZoom = static_cast<float>(lParam) / 10000.0f;
            if (abs(bitmapZoom - m_ctx.zoomFactor) < 0.01f) {
                CriticalSectionLock lock(m_ctx.wicMutex);
                if (m_ctx.renderTarget) {
                    ComPtr<ID2D1Bitmap> d2dBitmapHqTemp;
                    if (SUCCEEDED(m_ctx.renderTarget->CreateBitmapFromWicBitmap(pBitmap, nullptr, &d2dBitmapHqTemp))) {
                        m_ctx.d2dBitmapHq = d2dBitmapHqTemp;
                        m_ctx.hqZoomFactor = bitmapZoom;
                        InvalidateRect(hWnd, nullptr, FALSE);
                    }
                }
            }
            pBitmap->Release();
        }
        break;
    }
    case WM_APP_IMAGE_LOAD_FAILED:
        KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
        if (lParam != 0) {
            if (m_ctx.loadSequenceId == (int)lParam) {
                FinalizeImageLoad(false, -1);
            }
        }
        else {
            FinalizeImageLoad(false, -1);
        }
        break;
    case WM_APP_OCR_DONE_TEXT:
        m_ctx.ocrMessage = L"Text copied to clipboard.";
        m_ctx.isOcrMessageVisible = true;
        m_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(m_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_DONE_AREA:
        m_ctx.ocrMessage = L"Text from selected area copied.";
        m_ctx.isOcrMessageVisible = true;
        m_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(m_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_DONE_NOTEXT:
        m_ctx.ocrMessage = (lParam == 1) ? L"No text found in selected area." : L"No text found on image.";
        m_ctx.isOcrMessageVisible = true;
        m_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(m_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_FAILED: {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        std::wstring errorMsg = L"An unknown error occurred during OCR.";
        switch (lParam) {
        case 1: errorMsg = L"OCR failed (hresult_error)."; break;
        case 2: errorMsg = L"An unknown error occurred during OCR."; break;
        case 3: errorMsg = L"Could not open clipboard."; break;
        case 4: errorMsg = L"Invalid selection area."; break;
        case 5: errorMsg = L"OCR engine not available."; break;
        }
        MessageBoxW(m_ctx.hWnd, errorMsg.c_str(), L"OCR Error", MB_OK | MB_ICONERROR);
        break;
    }
    case WM_TIMER:
        if (wParam == ANIMATION_TIMER_ID) {
            CriticalSectionLock lock(m_ctx.wicMutex);
            if (m_ctx.isAnimated && !m_ctx.animationFrameDelays.empty()) {

                UINT currentDelay = m_ctx.animationFrameDelays[m_ctx.currentAnimationFrame];
                m_ctx.currentAnimationFrame = (m_ctx.currentAnimationFrame + 1) % m_ctx.animationFrameDelays.size();
                UINT nextDelay = m_ctx.animationFrameDelays[m_ctx.currentAnimationFrame];

                UpdateWindowTitle();
                InvalidateRect(hWnd, nullptr, FALSE);

                if (currentDelay != nextDelay) {
                    KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
                    SetTimer(m_ctx.hWnd, ANIMATION_TIMER_ID, nextDelay, nullptr);
                }
            }
        }
        else if (wParam == OCR_MESSAGE_TIMER_ID) {
            ULONGLONG elapsedTime = GetTickCount64() - m_ctx.ocrMessageStartTime;
            if (elapsedTime > 1000) {
                m_ctx.isOcrMessageVisible = false;
                KillTimer(m_ctx.hWnd, OCR_MESSAGE_TIMER_ID);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else if (wParam == LOADING_TIMER_ID) {
            KillTimer(m_ctx.hWnd, LOADING_TIMER_ID);
            if (m_ctx.isLoading) {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else if (wParam == HQ_RENDER_TIMER_ID) {
            if (m_ctx.isHqTaskRunning) {
                // Delay background hq_render
                SetTimer(m_ctx.hWnd, HQ_RENDER_TIMER_ID, 100, nullptr);
                return 0; 
            }

            KillTimer(m_ctx.hWnd, HQ_RENDER_TIMER_ID);
            if (m_ctx.isHqPending && m_ctx.wicConverter && !m_ctx.isAnimated) {
                m_ctx.isHqPending = false;
                float targetZoom = m_ctx.zoomFactor;

                // Abort if target zoom > 1x
                if (targetZoom >= 1.0f) return 0; 

                ComPtr<IWICFormatConverter> source = m_ctx.wicConverter;
                int currentSeq = ++m_ctx.hqRenderSequenceId;

                m_ctx.isHqTaskRunning = true;
                m_ctx.RunBackgroundTask([this, source, targetZoom, currentSeq]() {
                    struct HqTaskState {
                        AppContext* ctx;
                        ~HqTaskState() { ctx->isHqTaskRunning = false; }
                    } taskState{ &m_ctx };

                    if (currentSeq != m_ctx.hqRenderSequenceId) return;

                    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;

                    { 
                        ComPtr<IWICImagingFactory> localFactory;

                        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&localFactory)))) {
                            UINT w, h;
                            if (SUCCEEDED(source->GetSize(&w, &h))) {
                                if (currentSeq != m_ctx.hqRenderSequenceId) { return; }

                                UINT newW = static_cast<UINT>(std::max(1.0f, w * targetZoom));
                                UINT newH = static_cast<UINT>(std::max(1.0f, h * targetZoom));
                                ComPtr<IWICBitmapScaler> scaler;
                                if (SUCCEEDED(localFactory->CreateBitmapScaler(&scaler))) {
                                    if (SUCCEEDED(scaler->Initialize(source.Get(), newW, newH, WICBitmapInterpolationModeFant))) {
                                        if (currentSeq != m_ctx.hqRenderSequenceId) { return; }

                                        ComPtr<IWICBitmap> hqBitmap;
                                        if (SUCCEEDED(localFactory->CreateBitmapFromSource(scaler.Get(), WICBitmapCacheOnLoad, &hqBitmap))) {
                                            if (currentSeq != m_ctx.hqRenderSequenceId) { return; }

                                            IWICBitmap* pRawBitmap = hqBitmap.Get();
                                            pRawBitmap->AddRef();

                                            if (!PostMessage(m_ctx.hWnd, WM_APP_HQ_READY, (WPARAM)pRawBitmap, (LPARAM)(targetZoom * 10000.0f))) {
                                                pRawBitmap->Release();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } 

                    CoUninitialize();
                    });
            }
        }
        else if (wParam == NAV_DEBOUNCE_TIMER_ID) {
            KillTimer(m_ctx.hWnd, NAV_DEBOUNCE_TIMER_ID);
            if (m_ctx.pendingNavIndex != -1 && m_ctx.pendingNavIndex < m_ctx.imageFiles.size()) {
                LoadImageFromFile(m_ctx.imageFiles[m_ctx.pendingNavIndex].c_str(), m_ctx.startAtEnd);
                m_ctx.pendingNavIndex = -1;
            }
        }
        else if (wParam == AUTO_REFRESH_TIMER_ID) {
            if (m_ctx.isAutoRefresh && !m_ctx.isLoading && !m_ctx.imageFiles.empty() && m_ctx.currentImageIndex >= 0) {
                const std::wstring& currentFile = m_ctx.imageFiles[m_ctx.currentImageIndex];
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExW(currentFile.c_str(), GetFileExInfoStandard, &fad)) {
                    if (CompareFileTime(&fad.ftLastWriteTime, &m_ctx.lastWriteTime) > 0) {
                        // verify file is not locked
                        HANDLE hFile = CreateFileW(currentFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            CloseHandle(hFile);
                            m_ctx.preserveView = true;
                            m_ctx.imageFiles.clear(); // force directory rescan
                            LoadImageFromFile(currentFile);
                        }
                    }
                }
            }
        }
        break;
    case WM_DPICHANGED: {
        RECT* prcNewWindow = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr,
            prcNewWindow->left, prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        DiscardDeviceResources(); // recreate text/brush at new dpi. 
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; 
    case WM_PAINT:
        OnPaint(hWnd);
        break;
    case WM_KEYDOWN:
        OnKeyDown(wParam);
        break;
    case WM_KEYUP:
        break;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        ZoomImage(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.1f : 0.9f, pt);
        break;
    }
    case WM_LBUTTONDBLCLK:
        FitImageToWindow();
        break;
    case WM_RBUTTONUP: {
        if (m_ctx.isCropMode || m_ctx.isSelectingCropRect || m_ctx.isCropPending || m_ctx.isSelectingOcrRect) {
            bool wasCropActive = m_ctx.isCropActive;
            m_ctx.isCropMode = false;
            m_ctx.isSelectingCropRect = false;
            m_ctx.isCropPending = false;
            m_ctx.isSelectingOcrRect = false;
            m_ctx.isDraggingOcrRect = false;
            m_ctx.ocrRectWindow = { 0 };

            if (wasCropActive) {
                m_ctx.isCropActive = false;
                ApplyEffectsToView();
                FitImageToWindow();
            }
            else {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            if (GetCapture() == hWnd) {
                ReleaseCapture();
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ClientToScreen(hWnd, &pt);
        OnContextMenu(hWnd, pt);
        break;
    }
    case WM_DROPFILES:
        HandleDropFiles(reinterpret_cast<HDROP>(wParam));
        break;
    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (m_ctx.isEyedropperActive) {
            HandleEyedropperClick();
            m_ctx.isEyedropperActive = false;
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        if (m_ctx.isSelectingOcrRect) {
            m_ctx.isDraggingOcrRect = true;
            m_ctx.ocrStartPoint = pt;
            m_ctx.ocrRectWindow = D2D1::RectF(
                static_cast<float>(pt.x), static_cast<float>(pt.y),
                static_cast<float>(pt.x), static_cast<float>(pt.y)
            );
            SetCapture(hWnd);
        }
        else if (m_ctx.isCropMode) {
            m_ctx.isCropPending = false;
            m_ctx.isSelectingCropRect = true;
            m_ctx.cropStartPoint = pt;
            m_ctx.cropRectWindow = D2D1::RectF(
                static_cast<float>(pt.x), static_cast<float>(pt.y),
                static_cast<float>(pt.x), static_cast<float>(pt.y)
            );
            SetCapture(hWnd);
        }
        else {
            UINT w = 0, h = 0;
            if (GetCurrentImageSize(&w, &h)) {
                m_ctx.isDraggingImage = true;
                dragStart = pt;
                SetCapture(hWnd);
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            }
        }
        break;
    }
    case WM_LBUTTONUP:
        if (m_ctx.isSelectingOcrRect) {
            m_ctx.isSelectingOcrRect = false;
            m_ctx.isDraggingOcrRect = false;
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            ReleaseCapture();

            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            ConvertWindowToImagePoint(m_ctx.ocrStartPoint, x1, y1);
            POINT endPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ConvertWindowToImagePoint(endPoint, x2, y2);

            D2D1_RECT_F ocrRectLocal = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

            UINT imgWidth = 0, imgHeight = 0;
            GetCurrentImageSize(&imgWidth, &imgHeight);

            ocrRectLocal.left = std::max(0.0f, ocrRectLocal.left);
            ocrRectLocal.top = std::max(0.0f, ocrRectLocal.top);
            ocrRectLocal.right = std::min(static_cast<float>(imgWidth), ocrRectLocal.right);
            ocrRectLocal.bottom = std::min(static_cast<float>(imgHeight), ocrRectLocal.bottom);

            m_ctx.ocrRectWindow = { 0 };
            InvalidateRect(hWnd, nullptr, FALSE);

            if (ocrRectLocal.left < ocrRectLocal.right && ocrRectLocal.top < ocrRectLocal.bottom) {
                PerformOcrArea(ocrRectLocal);
            }
        }
        else if (m_ctx.isSelectingCropRect) {
            m_ctx.isSelectingCropRect = false;
            m_ctx.isCropMode = false;
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            ReleaseCapture();

            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            ConvertWindowToImagePoint(m_ctx.cropStartPoint, x1, y1);
            POINT endPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ConvertWindowToImagePoint(endPoint, x2, y2);

            m_ctx.cropRectLocal = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

            UINT imgWidth = 0, imgHeight = 0;
            GetCurrentImageSize(&imgWidth, &imgHeight);

            m_ctx.cropRectLocal.left = std::max(0.0f, m_ctx.cropRectLocal.left);
            m_ctx.cropRectLocal.top = std::max(0.0f, m_ctx.cropRectLocal.top);
            m_ctx.cropRectLocal.right = std::min(static_cast<float>(imgWidth), m_ctx.cropRectLocal.right);
            m_ctx.cropRectLocal.bottom = std::min(static_cast<float>(imgHeight), m_ctx.cropRectLocal.bottom);

            if (m_ctx.cropRectLocal.left < m_ctx.cropRectLocal.right && m_ctx.cropRectLocal.top < m_ctx.cropRectLocal.bottom) {
                m_ctx.isCropPending = true;
            }
            else {
                m_ctx.isCropPending = false;
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (m_ctx.isDraggingImage) {
            m_ctx.isDraggingImage = false;
            ReleaseCapture();
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (m_ctx.isEyedropperActive) {
            m_ctx.currentMousePos = pt;
            UpdateEyedropperColor(pt);
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        if (m_ctx.isDraggingOcrRect) {
            m_ctx.ocrRectWindow.right = static_cast<float>(pt.x);
            m_ctx.ocrRectWindow.bottom = static_cast<float>(pt.y);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (m_ctx.isSelectingCropRect) {
            m_ctx.cropRectWindow.right = static_cast<float>(pt.x);
            m_ctx.cropRectWindow.bottom = static_cast<float>(pt.y);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (m_ctx.isDraggingImage) {
            m_ctx.offsetX += (pt.x - dragStart.x);
            m_ctx.offsetY += (pt.y - dragStart.y);
            dragStart = pt;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            if (m_ctx.isEyedropperActive) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            if (m_ctx.isCropMode || m_ctx.isSelectingCropRect || m_ctx.isCropPending || m_ctx.isSelectingOcrRect) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            if (m_ctx.isDraggingImage) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    case WM_COPYDATA: {
        PCOPYDATASTRUCT pcds = reinterpret_cast<PCOPYDATASTRUCT>(lParam);
        if (pcds && pcds->dwData == 1) {
            wchar_t filePath[MAX_PATH];
            wcscpy_s(filePath, MAX_PATH, static_cast<wchar_t*>(pcds->lpData));
            PathUnquoteSpacesW(filePath);
            LoadImageFromFile(filePath);
        }
        return TRUE;
    }
    case WM_SIZE:
        if (m_ctx.renderTarget) {
            if (wParam == SIZE_MINIMIZED) {
                KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
            }
            else {
                if (m_ctx.swapChain) {
                    m_ctx.renderTarget->SetTarget(nullptr);
                    m_ctx.swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                    ComPtr<IDXGISurface> backBuffer;
                    m_ctx.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
                    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                    ComPtr<ID2D1Bitmap1> targetBmp;
                    m_ctx.renderTarget->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bmpProps, &targetBmp);
                    m_ctx.renderTarget->SetTarget(targetBmp.Get());
                }
                if (m_ctx.isAnimated && !m_ctx.animationFrameDelays.empty()) {
                    KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
                    SetTimer(m_ctx.hWnd, ANIMATION_TIMER_ID, m_ctx.animationFrameDelays[m_ctx.currentAnimationFrame], nullptr);
                }
            }
        }
        if (wParam != SIZE_MINIMIZED) {
            if (!m_ctx.isLoading) {
                FitImageToWindow();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        break;
    case WM_DESTROY:
        KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
        KillTimer(m_ctx.hWnd, AUTO_REFRESH_TIMER_ID);
        if (m_ctx.hPropsWnd) {
            DestroyWindow(m_ctx.hPropsWnd);
        }
        if (!m_ctx.isFullScreen) {
            GetWindowRect(m_ctx.hWnd, &m_ctx.savedRect);
        }
        WriteSettings(m_ctx.settingsPath, m_ctx.savedRect, m_ctx.startFullScreen, m_ctx.enforceSingleInstance, m_ctx.alwaysOnTop);
        CleanupLoadingThread();
        DiscardDeviceResources();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

