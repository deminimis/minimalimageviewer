#include "viewer.h"

extern AppContext g_ctx;

static bool IsImageFile(const wchar_t* filePath) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_ctx.wicFactory->CreateDecoderFromFilename(
        filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder
    );
    return SUCCEEDED(hr);
}

void LoadImageFromFile(const wchar_t* filePath) {
    if (g_ctx.hBitmap) {
        DeleteObject(g_ctx.hBitmap);
        g_ctx.hBitmap = nullptr;
    }
    g_ctx.currentFilePathOverride.clear();

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;

    HRESULT hr = g_ctx.wicFactory->CreateDecoderFromFilename(
        filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder
    );

    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = g_ctx.wicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.f, WICBitmapPaletteTypeCustom
        );
    }
    if (SUCCEEDED(hr)) {
        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);

        BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC screenDC = GetDC(nullptr);
        g_ctx.hBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screenDC);

        if (g_ctx.hBitmap && bits) {
            hr = converter->CopyPixels(nullptr, static_cast<UINT>(width) * 4, static_cast<UINT>(width) * height * 4, static_cast<BYTE*>(bits));
        }
        else {
            hr = E_OUTOFMEMORY;
        }
    }

    if (FAILED(hr) && g_ctx.hBitmap) {
        DeleteObject(g_ctx.hBitmap);
        g_ctx.hBitmap = nullptr;
    }
    CenterImage(true);
}

void GetImagesInDirectory(const wchar_t* filePath) {
    g_ctx.imageFiles.clear();

    wchar_t folder[MAX_PATH] = { 0 };
    wcscpy_s(folder, MAX_PATH, filePath);
    PathRemoveFileSpecW(folder);

    WIN32_FIND_DATAW fd{};
    wchar_t searchPath[MAX_PATH] = { 0 };
    PathCombineW(searchPath, folder, L"*.*");

    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                wchar_t fullPath[MAX_PATH] = { 0 };
                PathCombineW(fullPath, folder, fd.cFileName);
                if (IsImageFile(fullPath)) {
                    g_ctx.imageFiles.push_back(fullPath);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    auto it = std::find_if(g_ctx.imageFiles.begin(), g_ctx.imageFiles.end(),
        [&](const std::wstring& s) { return _wcsicmp(s.c_str(), filePath) == 0; }
    );
    g_ctx.currentImageIndex = (it != g_ctx.imageFiles.end()) ? static_cast<int>(std::distance(g_ctx.imageFiles.begin(), it)) : -1;
}

void DeleteCurrentImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) return;

    const std::wstring& filePathToDelete = g_ctx.imageFiles[g_ctx.currentImageIndex];
    std::wstring msg = L"Are you sure you want to move this file to the Recycle Bin?\n\n" + filePathToDelete;

    if (MessageBoxW(g_ctx.hWnd, msg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        std::vector<wchar_t> pFromBuffer(filePathToDelete.length() + 2, 0);
        wcscpy_s(pFromBuffer.data(), pFromBuffer.size(), filePathToDelete.c_str());

        SHFILEOPSTRUCTW sfos = { 0 };
        sfos.hwnd = g_ctx.hWnd;
        sfos.wFunc = FO_DELETE;
        sfos.pFrom = pFromBuffer.data();
        sfos.fFlags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION;

        if (SHFileOperationW(&sfos) == 0 && !sfos.fAnyOperationsAborted) {
            g_ctx.imageFiles.erase(g_ctx.imageFiles.begin() + g_ctx.currentImageIndex);
            if (g_ctx.imageFiles.empty()) {
                if (g_ctx.hBitmap) DeleteObject(g_ctx.hBitmap);
                g_ctx.hBitmap = nullptr;
                g_ctx.currentImageIndex = -1;
                InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
            }
            else {
                if (g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
                    g_ctx.currentImageIndex = 0;
                }
                LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
            }
        }
        else {
            MessageBoxW(g_ctx.hWnd, L"Failed to delete the file.", L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

static ComPtr<IWICBitmapSource> GetSaveSource(const GUID& targetFormat) {
    if (!g_ctx.hBitmap) return nullptr;

    ComPtr<IWICBitmapSource> source;
    ComPtr<IWICBitmap> wicBitmap;
    if (FAILED(g_ctx.wicFactory->CreateBitmapFromHBITMAP(g_ctx.hBitmap, nullptr, WICBitmapUseAlpha, &wicBitmap))) {
        return nullptr;
    }
    source = wicBitmap;

    if (g_ctx.rotationAngle != 0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (SUCCEEDED(g_ctx.wicFactory->CreateBitmapFlipRotator(&rotator))) {
            WICBitmapTransformOptions options = WICBitmapTransformRotate0;
            switch (g_ctx.rotationAngle) {
            case 90:  options = WICBitmapTransformRotate90;  break;
            case 180: options = WICBitmapTransformRotate180; break;
            case 270: options = WICBitmapTransformRotate270; break;
            }
            if (SUCCEEDED(rotator->Initialize(source, options))) {
                source = rotator;
            }
        }
    }

    WICPixelFormatGUID sourcePixelFormat{};
    if (FAILED(source->GetPixelFormat(&sourcePixelFormat))) return nullptr;

    if (targetFormat == GUID_ContainerFormatJpeg && sourcePixelFormat != GUID_WICPixelFormat24bppBGR) {
        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&converter))) {
            if (SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut))) {
                source = converter;
            }
        }
    }
    return source;
}

void SaveImageAs() {
    if (!g_ctx.hBitmap) return;

    wchar_t szFile[MAX_PATH] = L"Untitled.png";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_ctx.hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"PNG File (*.png)\0*.png\0JPEG File (*.jpg)\0*.jpg\0BMP File (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;

    GUID containerFormat = GUID_ContainerFormatPng;
    const wchar_t* ext = PathFindExtensionW(ofn.lpstrFile);
    if (ext) {
        if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) containerFormat = GUID_ContainerFormatJpeg;
        else if (_wcsicmp(ext, L".bmp") == 0) containerFormat = GUID_ContainerFormatBmp;
    }

    ComPtr<IWICBitmapSource> source = GetSaveSource(containerFormat);
    if (!source) {
        MessageBoxW(g_ctx.hWnd, L"Could not get image source to save.", L"Save Error", MB_ICONERROR);
        return;
    }

    HRESULT hr = E_FAIL;
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;

        hr = g_ctx.wicFactory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(ofn.lpstrFile, GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = g_ctx.wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
        if (SUCCEEDED(hr)) hr = frame->Initialize(props);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(source, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
    }

    if (SUCCEEDED(hr)) {
        LoadImageFromFile(ofn.lpstrFile);
        GetImagesInDirectory(ofn.lpstrFile);
    }
    else {
        MessageBoxW(g_ctx.hWnd, L"Failed to save image.", L"Save As Error", MB_ICONERROR);
    }
}

void SaveImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        SaveImageAs();
        return;
    }

    const auto& originalPath = g_ctx.imageFiles[g_ctx.currentImageIndex];

    if (g_ctx.rotationAngle == 0) {
        MessageBoxW(g_ctx.hWnd, L"No changes to save.", L"Save", MB_OK | MB_ICONINFORMATION);
        return;
    }

    GUID containerFormat{};
    {
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(g_ctx.wicFactory->CreateDecoderFromFilename(originalPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetContainerFormat(&containerFormat))) {
            MessageBoxW(g_ctx.hWnd, L"Could not determine original file format. Use 'Save As'.", L"Save Error", MB_ICONERROR);
            return;
        }
    }

    ComPtr<IWICBitmapSource> source = GetSaveSource(containerFormat);
    if (!source) {
        MessageBoxW(g_ctx.hWnd, L"Could not get image source to save.", L"Save Error", MB_ICONERROR);
        return;
    }

    std::wstring tempPath = originalPath + L".tmp_save";
    HRESULT hr = E_FAIL;
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;

        hr = g_ctx.wicFactory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(tempPath.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = g_ctx.wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(source, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
    }

    if (SUCCEEDED(hr)) {
        if (ReplaceFileW(originalPath.c_str(), tempPath.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            LoadImageFromFile(originalPath.c_str());
            g_ctx.rotationAngle = 0;
            InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        }
        else {
            DeleteFileW(tempPath.c_str());
            MessageBoxW(g_ctx.hWnd, L"Failed to replace the original file.", L"Save Error", MB_ICONERROR);
        }
    }
    else {
        DeleteFileW(tempPath.c_str());
        MessageBoxW(g_ctx.hWnd, L"Failed to save image to temporary file.", L"Save Error", MB_ICONERROR);
    }
}

void HandleDropFiles(HDROP hDrop) {
    wchar_t filePath[MAX_PATH] = { 0 };
    if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH) > 0) {
        LoadImageFromFile(filePath);
        GetImagesInDirectory(filePath);
    }
    DragFinish(hDrop);
}

void HandlePaste() {
    if (!IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_HDROP)) return;
    if (!OpenClipboard(g_ctx.hWnd)) return;

    HANDLE hClipData = GetClipboardData(CF_HDROP);
    if (hClipData) {
        HDROP hDrop = static_cast<HDROP>(hClipData);
        wchar_t filePath[MAX_PATH] = { 0 };
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH) > 0) {
            CloseClipboard();
            LoadImageFromFile(filePath);
            GetImagesInDirectory(filePath);
            return;
        }
    }

    hClipData = GetClipboardData(CF_DIB);
    if (hClipData) {
        LPBITMAPINFO lpbi = static_cast<LPBITMAPINFO>(GlobalLock(hClipData));
        if (lpbi) {
            if (g_ctx.hBitmap) DeleteObject(g_ctx.hBitmap);
            HDC screenDC = GetDC(nullptr);
            BYTE* pBits = reinterpret_cast<BYTE*>(lpbi) + lpbi->bmiHeader.biSize + lpbi->bmiHeader.biClrUsed * sizeof(RGBQUAD);
            g_ctx.hBitmap = CreateDIBitmap(screenDC, &(lpbi->bmiHeader), CBM_INIT, pBits, lpbi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, screenDC);
            GlobalUnlock(hClipData);

            if (g_ctx.hBitmap) {
                g_ctx.imageFiles.clear();
                g_ctx.currentImageIndex = -1;
                g_ctx.currentFilePathOverride = L"Pasted Image";
                CenterImage(true);
            }
        }
    }
    CloseClipboard();
}

void HandleCopy() {
    if (!g_ctx.hBitmap || !OpenClipboard(g_ctx.hWnd)) return;
    EmptyClipboard();

    BITMAP bm{};
    GetObject(g_ctx.hBitmap, sizeof(BITMAP), &bm);

    int outWidth = bm.bmWidth;
    int outHeight = bm.bmHeight;
    if (g_ctx.rotationAngle == 90 || g_ctx.rotationAngle == 270) {
        std::swap(outWidth, outHeight);
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = outWidth;
    bi.bmiHeader.biHeight = outHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(g_ctx.hWnd);
    HDC memDC = CreateCompatibleDC(screenDC);
    BYTE* pBits = nullptr;
    HBITMAP hClipBitmap = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pBits), NULL, 0);

    if (hClipBitmap) {
        HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(memDC, hClipBitmap));

        AppContext tempCtx = g_ctx;
        tempCtx.offsetX = 0;
        tempCtx.offsetY = 0;
        float rotatedW = (g_ctx.rotationAngle == 90 || g_ctx.rotationAngle == 270) ? static_cast<float>(bm.bmHeight) : static_cast<float>(bm.bmWidth);
        float rotatedH = (g_ctx.rotationAngle == 90 || g_ctx.rotationAngle == 270) ? static_cast<float>(bm.bmWidth) : static_cast<float>(bm.bmHeight);
        tempCtx.zoomFactor = std::min(static_cast<float>(outWidth) / rotatedW, static_cast<float>(outHeight) / rotatedH);

        RECT clientRect = { 0, 0, outWidth, outHeight };
        FillRect(memDC, &clientRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        DrawImage(memDC, clientRect, tempCtx);

        DWORD dwBmpSize = (static_cast<DWORD>(outWidth) * bi.bmiHeader.biBitCount + 31) / 32 * 4 * outHeight;
        DWORD dwSizeOfDIB = dwBmpSize + sizeof(BITMAPINFOHEADER);
        HGLOBAL hGlobal = GlobalAlloc(GHND, dwSizeOfDIB);
        if (hGlobal) {
            char* lpGlobal = static_cast<char*>(GlobalLock(hGlobal));
            if (lpGlobal) {
                memcpy(lpGlobal, &bi.bmiHeader, sizeof(BITMAPINFOHEADER));
                memcpy(lpGlobal + sizeof(BITMAPINFOHEADER), pBits, dwBmpSize);
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_DIB, hGlobal);
            }
        }
        SelectObject(memDC, hOldBitmap);
        DeleteObject(hClipBitmap);
    }

    DeleteDC(memDC);
    ReleaseDC(g_ctx.hWnd, screenDC);
    CloseClipboard();
}

void OpenFileLocationAction() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        return;
    }
    const std::wstring& filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];

    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(filePath.c_str(), nullptr, &pidl, 0, nullptr);
    if (SUCCEEDED(hr)) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }

    if (FAILED(hr)) {
        MessageBoxW(g_ctx.hWnd, L"Could not open file location.", L"Error", MB_OK | MB_ICONERROR);
    }
}