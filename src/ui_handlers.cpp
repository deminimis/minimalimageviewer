#include "viewer.h"
#include <string>
#include <stdio.h>

extern AppContext g_ctx;

void ToggleFullScreen() {
    if (!g_ctx.isFullScreen) {
        g_ctx.savedStyle = GetWindowLong(g_ctx.hWnd, GWL_STYLE);
        GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        HMONITOR hMonitor = MonitorFromWindow(g_ctx.hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_ctx.isFullScreen = true;
    }
    else {
        SetWindowLong(g_ctx.hWnd, GWL_STYLE, g_ctx.savedStyle | WS_VISIBLE);
        SetWindowPos(g_ctx.hWnd, HWND_NOTOPMOST, g_ctx.savedRect.left, g_ctx.savedRect.top,
            g_ctx.savedRect.right - g_ctx.savedRect.left, g_ctx.savedRect.bottom - g_ctx.savedRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_ctx.isFullScreen = false;
    }
    FitImageToWindow();
}

static void OpenFileAction() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = g_ctx.hWnd;
    ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2\0All Files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        LoadImageFromFile(szFile);
    }
}

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
        if (!g_ctx.imageFiles.empty()) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex + 1) % static_cast<int>(size);
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    case VK_LEFT:
        if (!g_ctx.imageFiles.empty()) {
            size_t size = g_ctx.imageFiles.size();
            g_ctx.currentImageIndex = (g_ctx.currentImageIndex - 1 + static_cast<int>(size)) % static_cast<int>(size);
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
        }
        break;
    case VK_UP:    RotateImage(true); break;
    case VK_DOWN:  RotateImage(false); break;
    case VK_DELETE: DeleteCurrentImage(); break;
    case VK_F11:   ToggleFullScreen(); break;
    case VK_ESCAPE:
        if (g_ctx.isFullScreen) ToggleFullScreen();
        else PostQuitMessage(0);
        break;
    case 'O':      if (ctrlPressed) OpenFileAction(); break;
    case 'S':      if (ctrlPressed && (GetKeyState(VK_SHIFT) & 0x8000)) SaveImageAs(); else if (ctrlPressed) SaveImage(); break;
    case 'C':      if (ctrlPressed) HandleCopy(); break;
    case 'V':      if (ctrlPressed) HandlePaste(); break;
    case '0':      if (ctrlPressed) CenterImage(true); break;
    case VK_MULTIPLY: if (ctrlPressed) SetActualSize(); break;
    case VK_OEM_PLUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(1.25f, centerPt);
        }
        break;
    case VK_OEM_MINUS:
        if (ctrlPressed) {
            RECT cr; GetClientRect(g_ctx.hWnd, &cr);
            POINT centerPt = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
            ZoomImage(0.8f, centerPt);
        }
        break;
    }
}

static void OnContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN, L"Open Image\tCtrl+O");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_COPY, L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_NEXT_IMG, L"Next Image\tRight Arrow");
    AppendMenuW(hMenu, MF_STRING, IDM_PREV_IMG, L"Previous Image\tLeft Arrow");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_ROTATE_CW, L"Rotate Clockwise\tUp Arrow");
    AppendMenuW(hMenu, MF_STRING, IDM_ROTATE_CCW, L"Rotate Counter-Clockwise\tDown Arrow");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_IN, L"Zoom In\tCtrl++");
    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_OUT, L"Zoom Out\tCtrl+-");
    AppendMenuW(hMenu, MF_STRING, IDM_ACTUAL_SIZE, L"Actual Size (100%)\tCtrl+*");
    AppendMenuW(hMenu, MF_STRING, IDM_FIT_TO_WINDOW, L"Fit to Window\tCtrl+0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE, L"Save\tCtrl+S");
    AppendMenuW(hMenu, MF_STRING, IDM_SAVE_AS, L"Save As\tCtrl+Shift+S");

    UINT locationFlags = (g_ctx.currentImageIndex != -1) ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(hMenu, locationFlags, IDM_OPEN_LOCATION, L"Open File Location");
    AppendMenuW(hMenu, locationFlags, IDM_PROPERTIES, L"Properties...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    HMENU hBgMenu = CreatePopupMenu();
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::Grey ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_GREY, L"Grey (Default)");
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::Black ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_BLACK, L"Black");
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::White ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_WHITE, L"White");
    AppendMenuW(hBgMenu, MF_STRING | (g_ctx.bgColor == BackgroundColor::Transparent ? MF_CHECKED : MF_UNCHECKED), IDM_BACKGROUND_TRANSPARENT, L"Transparent (Checkerboard)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hBgMenu, L"Background Color");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (g_ctx.startFullScreen ? MF_CHECKED : MF_UNCHECKED), IDM_START_FULLSCREEN, L"Start full screen");
    AppendMenuW(hMenu, MF_STRING | (g_ctx.enforceSingleInstance ? MF_CHECKED : MF_UNCHECKED), IDM_SINGLE_INSTANCE, L"Single instance only");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_FULLSCREEN, L"Full Screen\tF11");
    AppendMenuW(hMenu, MF_STRING, IDM_DELETE_IMG, L"Delete Image\tDelete");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit\tEsc");

    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_OPEN:          OpenFileAction(); break;
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
    case IDM_SAVE:          SaveImage(); break;
    case IDM_SAVE_AS:       SaveImageAs(); break;
    case IDM_OPEN_LOCATION: OpenFileLocationAction(); break;
    case IDM_PROPERTIES:    ShowImageProperties(); break;
    case IDM_BACKGROUND_GREY:
        g_ctx.bgColor = BackgroundColor::Grey;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_BACKGROUND_BLACK:
        g_ctx.bgColor = BackgroundColor::Black;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_BACKGROUND_WHITE:
        g_ctx.bgColor = BackgroundColor::White;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_BACKGROUND_TRANSPARENT:
        g_ctx.bgColor = BackgroundColor::Transparent;
        InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        break;
    case IDM_START_FULLSCREEN:
        g_ctx.startFullScreen = !g_ctx.startFullScreen;
        break;
    case IDM_SINGLE_INSTANCE:
        g_ctx.enforceSingleInstance = !g_ctx.enforceSingleInstance;
        break;
    }
}

static std::wstring FormatFileTime(const FILETIME& ft) {
    SYSTEMTIME stUTC, stLocal;
    if (!FileTimeToSystemTime(&ft, &stUTC)) {
        return L"N/A";
    }
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &stUTC, &stLocal)) {
        return L"N/A";
    }

    wchar_t szDate[256], szTime[256], szDateTime[512];
    GetDateFormatW(LOCALE_USER_DEFAULT, 0, &stLocal, nullptr, szDate, 256);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &stLocal, nullptr, szTime, 256);
    swprintf_s(szDateTime, 512, L"%s  %s", szDate, szTime);
    return szDateTime;
}

static std::wstring FormatFileSize(const LARGE_INTEGER& fileSize) {
    double size = static_cast<double>(fileSize.QuadPart);
    const wchar_t* unit = L"Bytes";
    wchar_t buffer[256];

    if (size >= 1024 * 1024 * 1024) {
        size /= (1024 * 1024 * 1024);
        unit = L"GB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else if (size >= 1024 * 1024) {
        size /= (1024 * 1024);
        unit = L"MB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else if (size >= 1024) {
        size /= 1024;
        unit = L"KB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else {
        swprintf_s(buffer, 256, L"%llu Bytes", fileSize.QuadPart);
    }
    return buffer;
}

static std::wstring GetMetadataString(IWICMetadataQueryReader* pReader, const wchar_t* query) {
    PROPVARIANT propValue;
    PropVariantInit(&propValue);
    std::wstring val = L"N/A";
    if (FAILED(pReader->GetMetadataByName(query, &propValue))) {
        PropVariantClear(&propValue);
        return val;
    }

    wchar_t buffer[256] = { 0 };

    if (wcscmp(query, L"/app1/ifd/exif/{rational=33437}") == 0 && propValue.vt == (VT_UI4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double fstop_val = (double)propValue.caul.pElems[0] / (double)propValue.caul.pElems[1];
        swprintf_s(buffer, 256, L"f/%.1f", fstop_val);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{rational=33434}") == 0 && propValue.vt == (VT_UI4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double num = (double)propValue.caul.pElems[0];
        double den = (double)propValue.caul.pElems[1];
        if (den == 0) den = 1.0;
        if (num == 1 && den > 1) {
            swprintf_s(buffer, 256, L"1/%.0f s", den);
        }
        else {
            swprintf_s(buffer, 256, L"%.4f s", num / den);
        }
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=34855}") == 0 && propValue.vt == VT_UI2) {
        swprintf_s(buffer, 256, L"ISO %u", propValue.uiVal);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{rational=37386}") == 0 && propValue.vt == (VT_UI4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double focal_val = (double)propValue.caul.pElems[0] / (double)propValue.caul.pElems[1];
        swprintf_s(buffer, 256, L"%.0f mm", focal_val);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=41989}") == 0 && propValue.vt == VT_UI2) {
        swprintf_s(buffer, 256, L"%u mm", propValue.uiVal);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{srational=37380}") == 0 && propValue.vt == (VT_I4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double bias_val = (double)propValue.caul.pElems[0] / (double)propValue.caul.pElems[1];
        swprintf_s(buffer, 256, L"%.2f EV", bias_val);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=37383}") == 0 && propValue.vt == VT_UI2) {
        switch (propValue.uiVal) {
        case 0: val = L"Unknown"; break;
        case 1: val = L"Average"; break;
        case 2: val = L"Center Weighted Average"; break;
        case 3: val = L"Spot"; break;
        case 4: val = L"Multi-spot"; break;
        case 5: val = L"Pattern"; break;
        case 6: val = L"Partial"; break;
        default: val = L"Other"; break;
        }
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=37385}") == 0 && propValue.vt == VT_UI2) {
        if (propValue.uiVal & 0x1) val = L"Fired";
        else val = L"Did not fire";
        if (propValue.uiVal & 0x4) val += L", Strobe";
        if (propValue.uiVal & 0x10) val += L", Auto";
        if (propValue.uiVal & 0x40) val += L", Red-eye reduction";
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=34850}") == 0 && propValue.vt == VT_UI2) {
        switch (propValue.uiVal) {
        case 0: val = L"Not defined"; break;
        case 1: val = L"Manual"; break;
        case 2: val = L"Normal program (Auto)"; break;
        case 3: val = L"Aperture priority"; break;
        case 4: val = L"Shutter priority"; break;
        case 5: val = L"Creative program"; break;
        case 6: val = L"Action program"; break;
        case 7: val = L"Portrait mode"; break;
        case 8: val = L"Landscape mode"; break;
        default: val = L"Other"; break;
        }
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=41987}") == 0 && propValue.vt == VT_UI2) {
        switch (propValue.uiVal) {
        case 0: val = L"Auto"; break;
        case 1: val = L"Manual"; break;
        default: val = L"Other"; break;
        }
    }
    else if (SUCCEEDED(PropVariantToString(propValue, buffer, 256))) {
        val = buffer;
    }

    PropVariantClear(&propValue);
    return val;
}

static std::wstring GetContainerFormatName(const GUID& guid) {
    if (guid == GUID_ContainerFormatPng) return L"PNG";
    if (guid == GUID_ContainerFormatJpeg) return L"JPEG";
    if (guid == GUID_ContainerFormatBmp) return L"BMP";
    if (guid == GUID_ContainerFormatGif) return L"GIF";
    if (guid == GUID_ContainerFormatTiff) return L"TIFF";
    if (guid == GUID_ContainerFormatIco) return L"ICO";
    if (guid == GUID_ContainerFormatWmp) return L"HD Photo / JPEG XR";
    if (guid == GUID_ContainerFormatDds) return L"DDS";
    if (guid == GUID_ContainerFormatHeif) return L"HEIF";
    return L"Unknown";
}

static std::wstring GetBitDepth(IWICBitmapFrameDecode* pFrame) {
    WICPixelFormatGUID pixelFormatGuid;
    if (FAILED(pFrame->GetPixelFormat(&pixelFormatGuid))) {
        return L"N/A";
    }

    ComPtr<IWICComponentInfo> componentInfo;
    if (FAILED(g_ctx.wicFactory->CreateComponentInfo(pixelFormatGuid, &componentInfo))) {
        return L"N/A";
    }

    ComPtr<IWICPixelFormatInfo> pixelFormatInfo;
    if (FAILED(componentInfo->QueryInterface(IID_PPV_ARGS(&pixelFormatInfo)))) {
        return L"N/A";
    }

    UINT bpp = 0;
    if (SUCCEEDED(pixelFormatInfo->GetBitsPerPixel(&bpp))) {
        return std::to_wstring(bpp) + L"-bit";
    }
    return L"N/A";
}


LRESULT CALLBACK PropsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
        ImageProperties* pProps = (ImageProperties*)pcs->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pProps);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        ImageProperties* pProps = (ImageProperties*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

        if (pProps) {
            SetBkMode(hdc, TRANSPARENT);
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SelectObject(hdc, hFont);

            int y = 15;
            int x_label = 15;
            int x_value = 160;
            int y_step = 20;

            TextOutW(hdc, x_label, y, L"File Path:", 10);
            TextOutW(hdc, x_value, y, pProps->filePath.c_str(), (int)pProps->filePath.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"Image Format:", 13);
            TextOutW(hdc, x_value, y, pProps->imageFormat.c_str(), (int)pProps->imageFormat.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Dimensions:", 11);
            TextOutW(hdc, x_value, y, pProps->dimensions.c_str(), (int)pProps->dimensions.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Bit Depth:", 10);
            TextOutW(hdc, x_value, y, pProps->bitDepth.c_str(), (int)pProps->bitDepth.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"DPI:", 4);
            TextOutW(hdc, x_value, y, pProps->dpi.c_str(), (int)pProps->dpi.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"File Size:", 10);
            TextOutW(hdc, x_value, y, pProps->fileSize.c_str(), (int)pProps->fileSize.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Attributes:", 11);
            TextOutW(hdc, x_value, y, pProps->attributes.c_str(), (int)pProps->attributes.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Created:", 8);
            TextOutW(hdc, x_value, y, pProps->createdDate.c_str(), (int)pProps->createdDate.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Modified:", 9);
            TextOutW(hdc, x_value, y, pProps->modifiedDate.c_str(), (int)pProps->modifiedDate.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Accessed:", 9);
            TextOutW(hdc, x_value, y, pProps->accessedDate.c_str(), (int)pProps->accessedDate.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"Camera Make:", 12);
            TextOutW(hdc, x_value, y, pProps->cameraMake.c_str(), (int)pProps->cameraMake.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Camera Model:", 13);
            TextOutW(hdc, x_value, y, pProps->cameraModel.c_str(), (int)pProps->cameraModel.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Date Taken:", 11);
            TextOutW(hdc, x_value, y, pProps->dateTaken.c_str(), (int)pProps->dateTaken.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"F-stop:", 7);
            TextOutW(hdc, x_value, y, pProps->fStop.c_str(), (int)pProps->fStop.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Exposure:", 9);
            TextOutW(hdc, x_value, y, pProps->exposureTime.c_str(), (int)pProps->exposureTime.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"ISO:", 4);
            TextOutW(hdc, x_value, y, pProps->iso.c_str(), (int)pProps->iso.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Focal Length:", 13);
            TextOutW(hdc, x_value, y, pProps->focalLength.c_str(), (int)pProps->focalLength.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"35mm Focal Length:", 18);
            TextOutW(hdc, x_value, y, pProps->focalLength35mm.c_str(), (int)pProps->focalLength35mm.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Exposure Bias:", 14);
            TextOutW(hdc, x_value, y, pProps->exposureBias.c_str(), (int)pProps->exposureBias.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Exposure Program:", 17);
            TextOutW(hdc, x_value, y, pProps->exposureProgram.c_str(), (int)pProps->exposureProgram.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"White Balance:", 14);
            TextOutW(hdc, x_value, y, pProps->whiteBalance.c_str(), (int)pProps->whiteBalance.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Metering Mode:", 14);
            TextOutW(hdc, x_value, y, pProps->meteringMode.c_str(), (int)pProps->meteringMode.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Flash:", 6);
            TextOutW(hdc, x_value, y, pProps->flash.c_str(), (int)pProps->flash.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Lens Model:", 11);
            TextOutW(hdc, x_value, y, pProps->lensModel.c_str(), (int)pProps->lensModel.length());
            y += y_step;

            y += y_step / 2;

            TextOutW(hdc, x_label, y, L"Author:", 7);
            TextOutW(hdc, x_value, y, pProps->author.c_str(), (int)pProps->author.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Copyright:", 10);
            TextOutW(hdc, x_value, y, pProps->copyright.c_str(), (int)pProps->copyright.length());
            y += y_step;

            TextOutW(hdc, x_label, y, L"Software:", 9);
            TextOutW(hdc, x_value, y, pProps->software.c_str(), (int)pProps->software.length());
        }
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY: {
        ImageProperties* pProps = (ImageProperties*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        delete pProps;
        g_ctx.hPropsWnd = nullptr;
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void ShowImageProperties() {
    if (g_ctx.hPropsWnd) {
        SetForegroundWindow(g_ctx.hPropsWnd);
        return;
    }

    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        return;
    }

    const std::wstring& filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];
    ImageProperties* pProps = new ImageProperties();
    pProps->filePath = filePath;

    UINT imgWidth, imgHeight;
    if (GetCurrentImageSize(&imgWidth, &imgHeight)) {
        pProps->dimensions = std::to_wstring(imgWidth) + L" x " + std::to_wstring(imgHeight) + L" pixels";
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER fileSize;
        fileSize.HighPart = fad.nFileSizeHigh;
        fileSize.LowPart = fad.nFileSizeLow;
        pProps->fileSize = FormatFileSize(fileSize);
        pProps->createdDate = FormatFileTime(fad.ftCreationTime);
        pProps->modifiedDate = FormatFileTime(fad.ftLastWriteTime);
        pProps->accessedDate = FormatFileTime(fad.ftLastAccessTime);

        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) pProps->attributes += L"Read-only; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) pProps->attributes += L"Hidden; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) pProps->attributes += L"System; ";
        if (pProps->attributes.empty()) pProps->attributes = L"Normal";

    }
    else {
        pProps->fileSize = L"N/A";
        pProps->createdDate = L"N/A";
        pProps->modifiedDate = L"N/A";
        pProps->accessedDate = L"N/A";
        pProps->attributes = L"N/A";
    }

    pProps->imageFormat = L"N/A";
    pProps->bitDepth = L"N/A";
    pProps->dpi = L"N/A";
    pProps->cameraMake = L"N/A";
    pProps->cameraModel = L"N/A";
    pProps->dateTaken = L"N/A";
    pProps->fStop = L"N/A";
    pProps->exposureTime = L"N/A";
    pProps->iso = L"N/A";
    pProps->software = L"N/A";
    pProps->focalLength = L"N/A";
    pProps->focalLength35mm = L"N/A";
    pProps->exposureBias = L"N/A";
    pProps->meteringMode = L"N/A";
    pProps->flash = L"N/A";
    pProps->exposureProgram = L"N/A";
    pProps->whiteBalance = L"N/A";
    pProps->author = L"N/A";
    pProps->copyright = L"N/A";
    pProps->lensModel = L"N/A";

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> metadataReader;

    if (SUCCEEDED(g_ctx.wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) {
        GUID containerFormat;
        if (SUCCEEDED(decoder->GetContainerFormat(&containerFormat))) {
            pProps->imageFormat = GetContainerFormatName(containerFormat);
            if (g_ctx.isAnimated) {
                pProps->imageFormat += L" (Animated)";
            }
        }

        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {

            pProps->bitDepth = GetBitDepth(frame);

            double dpiX, dpiY;
            if (SUCCEEDED(frame->GetResolution(&dpiX, &dpiY))) {
                pProps->dpi = std::to_wstring(static_cast<int>(dpiX + 0.5)) + L" x " + std::to_wstring(static_cast<int>(dpiY + 0.5)) + L" DPI";
            }

            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                pProps->dateTaken = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=36867}");
                pProps->cameraMake = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=271}");
                pProps->cameraModel = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=272}");
                pProps->fStop = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33437}");
                pProps->exposureTime = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33434}");
                pProps->iso = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=34855}");
                pProps->software = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=305}");
                pProps->focalLength = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=37386}");
                pProps->focalLength35mm = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=41989}");
                pProps->exposureBias = GetMetadataString(metadataReader, L"/app1/ifd/exif/{srational=37380}");
                pProps->meteringMode = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=37383}");
                pProps->flash = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=37385}");
                pProps->exposureProgram = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=34850}");
                pProps->whiteBalance = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=41987}");
                pProps->author = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=315}");
                pProps->copyright = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=33432}");
                pProps->lensModel = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=42036}");
            }
        }
    }


    static const wchar_t* PROPS_CLASS_NAME = L"MinimalImageViewerProperties";
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = PropsWndProc;
        wcex.hInstance = g_ctx.hInst;
        wcex.hIcon = LoadIcon(g_ctx.hInst, MAKEINTRESOURCE(IDI_APPICON));
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = PROPS_CLASS_NAME;
        if (RegisterClassExW(&wcex)) {
            classRegistered = true;
        }
    }

    RECT rcParent;
    GetWindowRect(g_ctx.hWnd, &rcParent);
    int wndWidth = 600;
    int wndHeight = 700;
    int xPos = rcParent.left + (rcParent.right - rcParent.left - wndWidth) / 2;
    int yPos = rcParent.top + (rcParent.bottom - rcParent.top - wndHeight) / 2;

    g_ctx.hPropsWnd = CreateWindowW(
        PROPS_CLASS_NAME,
        L"Image Properties",
        (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU) & ~WS_THICKFRAME,
        xPos, yPos, wndWidth, wndHeight,
        g_ctx.hWnd,
        nullptr,
        g_ctx.hInst,
        pProps
    );

    if (g_ctx.hPropsWnd) {
        ShowWindow(g_ctx.hPropsWnd, SW_SHOW);
    }
    else {
        delete pProps;
    }
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static POINT dragStart{};

    switch (message) {
    case WM_APP_IMAGE_LOADED:
        FinalizeImageLoad(true, static_cast<int>(wParam));
        break;
    case WM_APP_IMAGE_LOAD_FAILED:
        KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
        FinalizeImageLoad(false, -1);
        break;
    case WM_TIMER:
        if (wParam == ANIMATION_TIMER_ID) {
            std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
            if (g_ctx.isAnimated && !g_ctx.animationFrameDelays.empty()) {
                g_ctx.currentAnimationFrame = (g_ctx.currentAnimationFrame + 1) % g_ctx.animationFrameDelays.size();
                InvalidateRect(hWnd, nullptr, FALSE);
                KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
                SetTimer(g_ctx.hWnd, ANIMATION_TIMER_ID, g_ctx.animationFrameDelays[g_ctx.currentAnimationFrame], nullptr);
            }
        }
        break;
    case WM_PAINT:
        OnPaint(hWnd);
        break;
    case WM_KEYDOWN:
        OnKeyDown(wParam);
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
        UINT w, h;
        if (GetCurrentImageSize(&w, &h) && IsPointInImage(pt, {})) {
            g_ctx.isDraggingImage = true;
            dragStart = pt;
            SetCapture(hWnd);
            SetCursor(LoadCursor(nullptr, IDC_HAND));
        }
        break;
    }
    case WM_LBUTTONUP:
        if (g_ctx.isDraggingImage) {
            g_ctx.isDraggingImage = false;
            ReleaseCapture();
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (g_ctx.isDraggingImage) {
            g_ctx.offsetX += (pt.x - dragStart.x);
            g_ctx.offsetY += (pt.y - dragStart.y);
            dragStart = pt;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
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
            FitImageToWindow();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;
    case WM_DESTROY:
        KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
        if (g_ctx.hPropsWnd) {
            DestroyWindow(g_ctx.hPropsWnd);
        }
        if (!g_ctx.isFullScreen) {
            GetWindowRect(g_ctx.hWnd, &g_ctx.savedRect);
        }
        WriteSettings(g_ctx.settingsPath, g_ctx.savedRect, g_ctx.startFullScreen, g_ctx.enforceSingleInstance);
        CleanupLoadingThread();
        DiscardDeviceResources();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}