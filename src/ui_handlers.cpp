#include "viewer.h"
#include "exif_utils.h"
#include <string>
#include <stdio.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

extern AppContext g_ctx;



static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hWnd, &ps);

    if (g_ctx.isInitialized) {
        Render();
    }

    EndPaint(hWnd, &ps);
}

static void OnKeyDown(WPARAM wParam) {
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    switch (wParam) {
    case VK_RIGHT:
        if (!g_ctx.isAnimated && g_ctx.animationFrameConverters.size() > 1 && g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size() - 1) {
            g_ctx.currentAnimationFrame++;
            UpdateViewToCurrentFrame();
        }
        else if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex + 1) % static_cast<int>(size);
            g_ctx.pendingNavIndex = g_ctx.currentImageIndex;
            g_ctx.startAtEnd = false;
            SetWindowTextW(g_ctx.hWnd, (g_ctx.imageFiles[g_ctx.currentImageIndex] + L"  [Loading...]").c_str());
            SetTimer(g_ctx.hWnd, NAV_DEBOUNCE_TIMER_ID, 150, nullptr);
        }
        break;
    case VK_LEFT:
        if (!g_ctx.isAnimated && g_ctx.animationFrameConverters.size() > 1 && g_ctx.currentAnimationFrame > 0) {
            g_ctx.currentAnimationFrame--;
            UpdateViewToCurrentFrame();
        }
        else if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex - 1 + static_cast<int>(size)) % static_cast<int>(size);
            g_ctx.pendingNavIndex = g_ctx.currentImageIndex;
            g_ctx.startAtEnd = true;
            SetWindowTextW(g_ctx.hWnd, (g_ctx.imageFiles[g_ctx.currentImageIndex] + L"  [Loading...]").c_str());
            SetTimer(g_ctx.hWnd, NAV_DEBOUNCE_TIMER_ID, 150, nullptr);
        }
        break;
    case VK_UP:    RotateImage(true); break;
    case VK_DOWN:  RotateImage(false); break;
    case VK_DELETE: DeleteCurrentImage(); break;
    case VK_F11:   ToggleFullScreen(); break;
    case 'F':      FlipImage(); break;
    case VK_ESCAPE:
        if (g_ctx.isCropMode || g_ctx.isSelectingCropRect || g_ctx.isCropPending || g_ctx.isSelectingOcrRect || g_ctx.isEyedropperActive) {
            g_ctx.isCropMode = false;
            g_ctx.isSelectingCropRect = false;
            g_ctx.isCropPending = false;
            g_ctx.isSelectingOcrRect = false;
            g_ctx.isDraggingOcrRect = false;
            g_ctx.isEyedropperActive = false;
            g_ctx.ocrRectWindow = { 0 };

            // revert any pending views
            ApplyEffectsToView();
            FitImageToWindow();

            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            if (GetCapture() == g_ctx.hWnd) {
                ReleaseCapture();
            }
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        }
        else {
            PostQuitMessage(0);
        }
        break;
    case 'O':      if (ctrlPressed) OpenFileAction(); break;
    case 'Z':
        if (ctrlPressed && !g_ctx.undoStack.empty()) {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.wicConverterOriginal = g_ctx.undoStack.back();
            g_ctx.undoStack.pop_back();
            g_ctx.isCropActive = false;
            g_ctx.cropRectLocal = { 0 };
            ApplyEffectsToView();
            FitImageToWindow();
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        }
        break;
    case 'S':      if (ctrlPressed && (GetKeyState(VK_SHIFT) & 0x8000)) SaveImageAs(); else if (ctrlPressed) SaveImage(); break;
    case 'C':
        if (ctrlPressed) {
            HandleCopy();
        }
        else {
            bool wasCropActive = g_ctx.isCropActive;
            g_ctx.isCropMode = !g_ctx.isCropMode;
            g_ctx.isCropActive = false;
            g_ctx.isCropPending = false;
            g_ctx.isSelectingCropRect = false;

            if (wasCropActive) {
                ApplyEffectsToView();
                FitImageToWindow();
            }

            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
            SetCursor(LoadCursor(nullptr, g_ctx.isCropMode ? IDC_CROSS : IDC_ARROW));
        }
        break;
    case 'V':      if (ctrlPressed) HandlePaste(); break;
    case '0':      if (ctrlPressed) CenterImage(true); break;
    case VK_MULTIPLY: if (ctrlPressed) SetActualSize(); break;
    case VK_RETURN:
        if (g_ctx.isCropPending) {
            g_ctx.isCropActive = true;
            g_ctx.isCropPending = false;
            g_ctx.isCropMode = false;

            CommitCrop();

            ApplyEffectsToView();
            FitImageToWindow();

            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        }
        break;
    case 'I':
        g_ctx.isOsdVisible = !g_ctx.isOsdVisible;
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        break;
    case 'Q':
        PerformOcr();
        break;
    case 'W':
        g_ctx.isSelectingOcrRect = true;
        g_ctx.isDraggingOcrRect = false;
        g_ctx.isCropMode = false;
        g_ctx.isSelectingCropRect = false;
        g_ctx.isCropPending = false;
        g_ctx.isEyedropperActive = false;
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    case VK_ADD:
    case VK_OEM_PLUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(1.25f, centerPt);
        }
        break;
    case VK_SUBTRACT:
    case VK_OEM_MINUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(0.8f, centerPt);
        }
        break;
    case VK_F5:
        if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    }
}



static void OnContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN, L"Open Image\tCtrl+O");
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"Refresh\tF5");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_COPY, L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_NEXT_IMG, L"Next Image\tRight Arrow");
    AppendMenuW(hMenu, MF_STRING, IDM_PREV_IMG, L"Previous Image\tLeft Arrow");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    HMENU hSortMenu = CreatePopupMenu();
    auto addSortItem = [&](UINT id, SortCriteria crit, bool asc, LPCWSTR text) {
        UINT flags = MF_STRING | ((g_ctx.currentSortCriteria == crit && g_ctx.isSortAscending == asc) ? MF_CHECKED : MF_UNCHECKED);
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
    AppendMenuW(hEditMenu, MF_STRING, IDM_ROTATE_CW, L"Rotate Clockwise\tUp Arrow");
    AppendMenuW(hEditMenu, MF_STRING, IDM_ROTATE_CCW, L"Rotate Counter-Clockwise\tDown Arrow");
    AppendMenuW(hEditMenu, MF_STRING, IDM_FLIP, L"Flip\tF");
    AppendMenuW(hEditMenu, MF_STRING | (g_ctx.isGrayscale ? MF_CHECKED : MF_UNCHECKED), IDM_GRAYSCALE, L"Grayscale");
    AppendMenuW(hEditMenu, MF_STRING, IDM_CROP, L"Crop\tC");
    AppendMenuW(hEditMenu, MF_STRING, IDM_RESIZE, L"Resize Image...");
    AppendMenuW(hEditMenu, MF_STRING, IDM_BRIGHTNESS_CONTRAST, L"Image Effects...");
    AppendMenuW(hEditMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hEditMenu, MF_STRING | (g_ctx.isEyedropperActive ? MF_CHECKED : MF_UNCHECKED), IDM_EYEDROPPER, L"Pick Color");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"Edit");

    HMENU hViewMenu = CreatePopupMenu();
    AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_IN, L"Zoom In\tCtrl++");
    AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_OUT, L"Zoom Out\tCtrl+-");
    AppendMenuW(hViewMenu, MF_STRING, IDM_ACTUAL_SIZE, L"Actual Size (100%)\tCtrl+*");
    AppendMenuW(hViewMenu, MF_STRING, IDM_FIT_TO_WINDOW, L"Fit to Window\tCtrl+0");
    AppendMenuW(hViewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hViewMenu, MF_STRING, IDM_FULLSCREEN, L"Full Screen\tF11");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"View");

    AppendMenuW(hMenu, MF_STRING, IDM_OCR, L"Copy Text (OCR)\tQ");
    AppendMenuW(hMenu, MF_STRING, IDM_OCR_AREA, L"Copy Text from Area\tW");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE, L"Save\tCtrl+S");
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE_AS, L"Save As\tCtrl+Shift+S");

    UINT locationFlags = (g_ctx.currentImageIndex != -1) ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(hMenu, locationFlags, IDM_OPEN_LOCATION, L"Open File Location");
    AppendMenuW(hMenu, locationFlags, IDM_PROPERTIES, L"Properties...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_PREFERENCES, L"Preferences...");
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
    case IDM_FIT_TO_WINDOW: FitImageToWindow(); break;
    case IDM_FULLSCREEN:    ToggleFullScreen(); break;
    case IDM_DELETE_IMG:    DeleteCurrentImage(); break;
    case IDM_EXIT:          PostQuitMessage(0); break;
    case IDM_ROTATE_CW:     RotateImage(true); break;
    case IDM_ROTATE_CCW:    RotateImage(false); break;
    case IDM_FLIP:          FlipImage(); break;
    case IDM_GRAYSCALE:
        g_ctx.isGrayscale = !g_ctx.isGrayscale;
        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.d2dBitmap = nullptr;
            g_ctx.animationD2DBitmaps.clear();
        }
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        break;
    case IDM_CROP: {
        bool wasCropActive = g_ctx.isCropActive;
        g_ctx.isCropMode = true;
        g_ctx.isCropActive = false;
        g_ctx.isCropPending = false;
        g_ctx.isSelectingCropRect = false;

        if (wasCropActive) {
            ApplyEffectsToView();
            FitImageToWindow();
        }
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    }
    case IDM_RESIZE:        ResizeImageAction(); break;
    case IDM_BRIGHTNESS_CONTRAST: OpenBrightnessContrastDialog(); break;
    case IDM_EYEDROPPER:
        g_ctx.isEyedropperActive = !g_ctx.isEyedropperActive;
        g_ctx.didCopyColor = false;
        SetCursor(LoadCursor(nullptr, g_ctx.isEyedropperActive ? IDC_CROSS : IDC_ARROW));
        if (g_ctx.isEyedropperActive) SetCapture(g_ctx.hWnd);
        else ReleaseCapture();
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
        break;
    case IDM_OCR:           PerformOcr(); break;
    case IDM_OCR_AREA:
        g_ctx.isSelectingOcrRect = true;
        g_ctx.isDraggingOcrRect = false;
        g_ctx.isCropMode = false;
        g_ctx.isSelectingCropRect = false;
        g_ctx.isCropPending = false;
        g_ctx.isEyedropperActive = false;
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        break;
    case IDM_SAVE:          SaveImage(); break;
    case IDM_SAVE_AS:       SaveImageAs(); break;
    case IDM_OPEN_LOCATION: OpenFileLocationAction(); break;
    case IDM_PROPERTIES:    ShowImageProperties(); break;
    case IDM_PREFERENCES:   OpenPreferencesDialog(); break;

    case IDM_SORT_BY_NAME_ASC:
    case IDM_SORT_BY_NAME_DESC:
    case IDM_SORT_BY_DATE_ASC:
    case IDM_SORT_BY_DATE_DESC:
    case IDM_SORT_BY_SIZE_ASC:
    case IDM_SORT_BY_SIZE_DESC:
    {
        std::wstring currentFile;
        if (g_ctx.currentImageIndex >= 0 && g_ctx.currentImageIndex < static_cast<int>(g_ctx.imageFiles.size())) {
            currentFile = g_ctx.imageFiles[g_ctx.currentImageIndex];
        }

        g_ctx.isSortAscending = (cmd == IDM_SORT_BY_NAME_ASC || cmd == IDM_SORT_BY_DATE_ASC || cmd == IDM_SORT_BY_SIZE_ASC);
        if (cmd == IDM_SORT_BY_NAME_ASC || cmd == IDM_SORT_BY_NAME_DESC) g_ctx.currentSortCriteria = SortCriteria::ByName;
        else if (cmd == IDM_SORT_BY_DATE_ASC || cmd == IDM_SORT_BY_DATE_DESC) g_ctx.currentSortCriteria = SortCriteria::ByDateModified;
        else g_ctx.currentSortCriteria = SortCriteria::ByFileSize;

        if (!g_ctx.currentDirectory.empty()) {
            LoadImageFromFile(currentFile);
        }
        break;
    }
    }
}




LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static POINT dragStart = {};

    switch (message) {
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
            if (abs(bitmapZoom - g_ctx.zoomFactor) < 0.01f) {
                CriticalSectionLock lock(g_ctx.wicMutex);
                if (g_ctx.renderTarget) {
                    ComPtr<ID2D1Bitmap> d2dBitmapHqTemp;
                    if (SUCCEEDED(g_ctx.renderTarget->CreateBitmapFromWicBitmap(pBitmap, nullptr, &d2dBitmapHqTemp))) {
                        g_ctx.d2dBitmapHq = d2dBitmapHqTemp;
                        g_ctx.hqZoomFactor = bitmapZoom;
                        InvalidateRect(hWnd, nullptr, FALSE);
                    }
                }
            }
            pBitmap->Release();
        }
        break;
    }
    case WM_APP_IMAGE_LOAD_FAILED:
        KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
        if (lParam != 0) {
            if (g_ctx.loadSequenceId == (int)lParam) {
                FinalizeImageLoad(false, -1);
            }
        }
        else {
            FinalizeImageLoad(false, -1);
        }
        break;
    case WM_APP_OCR_DONE_TEXT:
        g_ctx.ocrMessage = L"Text copied to clipboard.";
        g_ctx.isOcrMessageVisible = true;
        g_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_DONE_AREA:
        g_ctx.ocrMessage = L"Text from selected area copied.";
        g_ctx.isOcrMessageVisible = true;
        g_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        break;
    case WM_APP_OCR_DONE_NOTEXT:
        g_ctx.ocrMessage = (lParam == 1) ? L"No text found in selected area." : L"No text found on image.";
        g_ctx.isOcrMessageVisible = true;
        g_ctx.ocrMessageStartTime = GetTickCount64();
        SetTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID, 16, nullptr);
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
        MessageBoxW(g_ctx.hWnd, errorMsg.c_str(), L"OCR Error", MB_OK | MB_ICONERROR);
        break;
    }
    case WM_TIMER:
        if (wParam == ANIMATION_TIMER_ID) {
            CriticalSectionLock lock(g_ctx.wicMutex);
            if (g_ctx.isAnimated && !g_ctx.animationFrameDelays.empty()) {
                g_ctx.currentAnimationFrame = (g_ctx.currentAnimationFrame + 1) % g_ctx.animationFrameDelays.size();
                InvalidateRect(hWnd, nullptr, FALSE);
                KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
                SetTimer(g_ctx.hWnd, ANIMATION_TIMER_ID, g_ctx.animationFrameDelays[g_ctx.currentAnimationFrame], nullptr);
            }
        }
        else if (wParam == OCR_MESSAGE_TIMER_ID) {
            ULONGLONG elapsedTime = GetTickCount64() - g_ctx.ocrMessageStartTime;
            if (elapsedTime > 1000) {
                g_ctx.isOcrMessageVisible = false;
                KillTimer(g_ctx.hWnd, OCR_MESSAGE_TIMER_ID);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else if (wParam == LOADING_TIMER_ID) {
            KillTimer(g_ctx.hWnd, LOADING_TIMER_ID);
            if (g_ctx.isLoading) {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        else if (wParam == HQ_RENDER_TIMER_ID) {
            KillTimer(g_ctx.hWnd, HQ_RENDER_TIMER_ID);
            if (g_ctx.isHqPending && g_ctx.wicConverter && !g_ctx.isAnimated) {
                g_ctx.isHqPending = false;
                float targetZoom = g_ctx.zoomFactor;
                ComPtr<IWICFormatConverter> source = g_ctx.wicConverter;

                std::thread([source, targetZoom]() {
                    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
                    ComPtr<IWICImagingFactory> localFactory;
                    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&localFactory)))) {
                        UINT w, h;
                        if (SUCCEEDED(source->GetSize(&w, &h))) {
                            UINT newW = static_cast<UINT>(std::max(1.0f, w * targetZoom));
                            UINT newH = static_cast<UINT>(std::max(1.0f, h * targetZoom));
                            ComPtr<IWICBitmapScaler> scaler;
                            if (SUCCEEDED(localFactory->CreateBitmapScaler(&scaler))) {
                                if (SUCCEEDED(scaler->Initialize(source, newW, newH, WICBitmapInterpolationModeFant))) {
                                    ComPtr<IWICBitmap> hqBitmap;
                                    if (SUCCEEDED(localFactory->CreateBitmapFromSource(scaler, WICBitmapCacheOnLoad, &hqBitmap))) {
                                        IWICBitmap* pRawBitmap = static_cast<IWICBitmap*>(hqBitmap);
                                        pRawBitmap->AddRef();
                                        PostMessage(g_ctx.hWnd, WM_APP_HQ_READY, (WPARAM)pRawBitmap, (LPARAM)(targetZoom * 10000.0f));
                                    }
                                }
                            }
                        }
                    }
                    CoUninitialize();
                    }).detach();
            }
        }
        else if (wParam == NAV_DEBOUNCE_TIMER_ID) {
            KillTimer(g_ctx.hWnd, NAV_DEBOUNCE_TIMER_ID);
            if (g_ctx.pendingNavIndex != -1 && g_ctx.pendingNavIndex < g_ctx.imageFiles.size()) {
                LoadImageFromFile(g_ctx.imageFiles[g_ctx.pendingNavIndex].c_str(), g_ctx.startAtEnd);
                g_ctx.pendingNavIndex = -1;
            }
        }
        else if (wParam == AUTO_REFRESH_TIMER_ID) {
            if (g_ctx.isAutoRefresh && !g_ctx.isLoading && !g_ctx.imageFiles.empty() && g_ctx.currentImageIndex >= 0) {
                const std::wstring& currentFile = g_ctx.imageFiles[g_ctx.currentImageIndex];
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExW(currentFile.c_str(), GetFileExInfoStandard, &fad)) {
                    if (CompareFileTime(&fad.ftLastWriteTime, &g_ctx.lastWriteTime) > 0) {
                        // verify file is not locked
                        HANDLE hFile = CreateFileW(currentFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            CloseHandle(hFile);
                            g_ctx.preserveView = true;
                            LoadImageFromFile(currentFile);
                        }
                    }
                }
            }
        }
        break;
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
        if (g_ctx.isCropMode || g_ctx.isSelectingCropRect || g_ctx.isCropPending || g_ctx.isSelectingOcrRect) {
            bool wasCropActive = g_ctx.isCropActive;
            g_ctx.isCropMode = false;
            g_ctx.isSelectingCropRect = false;
            g_ctx.isCropPending = false;
            g_ctx.isSelectingOcrRect = false;
            g_ctx.isDraggingOcrRect = false;
            g_ctx.ocrRectWindow = { 0 };

            if (wasCropActive) {
                g_ctx.isCropActive = false;
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
        if (g_ctx.isEyedropperActive) {
            HandleEyedropperClick();
            g_ctx.isEyedropperActive = false;
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        if (g_ctx.isSelectingOcrRect) {
            g_ctx.isDraggingOcrRect = true;
            g_ctx.ocrStartPoint = pt;
            g_ctx.ocrRectWindow = D2D1::RectF(
                static_cast<float>(pt.x), static_cast<float>(pt.y),
                static_cast<float>(pt.x), static_cast<float>(pt.y)
            );
            SetCapture(hWnd);
        }
        else if (g_ctx.isCropMode) {
            g_ctx.isCropPending = false;
            g_ctx.isSelectingCropRect = true;
            g_ctx.cropStartPoint = pt;
            g_ctx.cropRectWindow = D2D1::RectF(
                static_cast<float>(pt.x), static_cast<float>(pt.y),
                static_cast<float>(pt.x), static_cast<float>(pt.y)
            );
            SetCapture(hWnd);
        }
        else {
            UINT w = 0, h = 0;
            if (GetCurrentImageSize(&w, &h) && IsPointInImage(pt, {})) {
                g_ctx.isDraggingImage = true;
                dragStart = pt;
                SetCapture(hWnd);
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            }
        }
        break;
    }
    case WM_LBUTTONUP:
        if (g_ctx.isSelectingOcrRect) {
            g_ctx.isSelectingOcrRect = false;
            g_ctx.isDraggingOcrRect = false;
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            ReleaseCapture();

            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            ConvertWindowToImagePoint(g_ctx.ocrStartPoint, x1, y1);
            POINT endPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ConvertWindowToImagePoint(endPoint, x2, y2);

            D2D1_RECT_F ocrRectLocal = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

            UINT imgWidth = 0, imgHeight = 0;
            GetCurrentImageSize(&imgWidth, &imgHeight);

            ocrRectLocal.left = std::max(0.0f, ocrRectLocal.left);
            ocrRectLocal.top = std::max(0.0f, ocrRectLocal.top);
            ocrRectLocal.right = std::min(static_cast<float>(imgWidth), ocrRectLocal.right);
            ocrRectLocal.bottom = std::min(static_cast<float>(imgHeight), ocrRectLocal.bottom);

            g_ctx.ocrRectWindow = { 0 };
            InvalidateRect(hWnd, nullptr, FALSE);

            if (ocrRectLocal.left < ocrRectLocal.right && ocrRectLocal.top < ocrRectLocal.bottom) {
                PerformOcrArea(ocrRectLocal);
            }
        }
        else if (g_ctx.isSelectingCropRect) {
            g_ctx.isSelectingCropRect = false;
            g_ctx.isCropMode = false;
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            ReleaseCapture();

            float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            ConvertWindowToImagePoint(g_ctx.cropStartPoint, x1, y1);
            POINT endPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ConvertWindowToImagePoint(endPoint, x2, y2);

            g_ctx.cropRectLocal = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

            UINT imgWidth = 0, imgHeight = 0;
            GetCurrentImageSize(&imgWidth, &imgHeight);

            g_ctx.cropRectLocal.left = std::max(0.0f, g_ctx.cropRectLocal.left);
            g_ctx.cropRectLocal.top = std::max(0.0f, g_ctx.cropRectLocal.top);
            g_ctx.cropRectLocal.right = std::min(static_cast<float>(imgWidth), g_ctx.cropRectLocal.right);
            g_ctx.cropRectLocal.bottom = std::min(static_cast<float>(imgHeight), g_ctx.cropRectLocal.bottom);

            if (g_ctx.cropRectLocal.left < g_ctx.cropRectLocal.right && g_ctx.cropRectLocal.top < g_ctx.cropRectLocal.bottom) {
                g_ctx.isCropPending = true;
            }
            else {
                g_ctx.isCropPending = false;
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (g_ctx.isDraggingImage) {
            g_ctx.isDraggingImage = false;
            ReleaseCapture();
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_ctx.isEyedropperActive) {
            g_ctx.currentMousePos = pt;
            UpdateEyedropperColor(pt);
            InvalidateRect(hWnd, nullptr, FALSE);
            break;
        }
        if (g_ctx.isDraggingOcrRect) {
            g_ctx.ocrRectWindow.right = static_cast<float>(pt.x);
            g_ctx.ocrRectWindow.bottom = static_cast<float>(pt.y);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (g_ctx.isSelectingCropRect) {
            g_ctx.cropRectWindow.right = static_cast<float>(pt.x);
            g_ctx.cropRectWindow.bottom = static_cast<float>(pt.y);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (g_ctx.isDraggingImage) {
            g_ctx.offsetX += (pt.x - dragStart.x);
            g_ctx.offsetY += (pt.y - dragStart.y);
            dragStart = pt;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            if (g_ctx.isEyedropperActive) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            if (g_ctx.isCropMode || g_ctx.isSelectingCropRect || g_ctx.isCropPending || g_ctx.isSelectingOcrRect) {
                SetCursor(LoadCursor(nullptr, IDC_CROSS));
                return TRUE;
            }
            if (g_ctx.isDraggingImage) {
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
        if (g_ctx.renderTarget) {
            if (wParam == SIZE_MINIMIZED) {
                KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
            }
            else {
                g_ctx.renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
                if (g_ctx.isAnimated && !g_ctx.animationFrameDelays.empty()) {
                    KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
                    SetTimer(g_ctx.hWnd, ANIMATION_TIMER_ID, g_ctx.animationFrameDelays[g_ctx.currentAnimationFrame], nullptr);
                }
            }
        }
        if (wParam != SIZE_MINIMIZED) {
            if (!g_ctx.isLoading) {
                FitImageToWindow();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        break;
    case WM_DESTROY:
        KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
        KillTimer(g_ctx.hWnd, AUTO_REFRESH_TIMER_ID);
        if (g_ctx.hPropsWnd) {
            DestroyWindow(g_ctx.hPropsWnd);
        }
        if (!g_ctx.isFullScreen) {
            GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        }
        WriteSettings(g_ctx.settingsPath, g_ctx.savedRect, g_ctx.startFullScreen, g_ctx.enforceSingleInstance, g_ctx.alwaysOnTop);
        CleanupLoadingThread();
        DiscardDeviceResources();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

