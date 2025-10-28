#include "viewer.h"

extern AppContext g_ctx;

static bool IsImageFile(const wchar_t* filePath) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_ctx.wicFactory->CreateDecoderFromFilename(
        filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder
    );
    return SUCCEEDED(hr);
}

static ComPtr<IWICFormatConverter> LoadImageInternal(const std::wstring& filePath, bool isPreload) {
    if (isPreload && g_ctx.cancelPreloading) {
        return nullptr;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_ctx.wicFactory->CreateDecoderFromFilename(
        filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder
    );
    if (FAILED(hr)) return nullptr;

    if (isPreload && g_ctx.cancelPreloading) return nullptr;

    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = g_ctx.wicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.f, WICBitmapPaletteTypeCustom
        );
    }

    if (SUCCEEDED(hr)) {
        return converter;
    }
    return nullptr;
}

void LoadImageFromFile(const std::wstring& filePath) {
    CleanupLoadingThread();

    g_ctx.isLoading = true;
    g_ctx.loadingFilePath = filePath;

    {
        std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
        g_ctx.wicConverter = nullptr;
        g_ctx.d2dBitmap = nullptr;
    }
    g_ctx.currentFilePathOverride.clear();
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);

    int nextIndex = -1, prevIndex = -1, targetIndex = -1;
    auto it = std::find_if(g_ctx.imageFiles.begin(), g_ctx.imageFiles.end(),
        [&](const std::wstring& s) { return _wcsicmp(s.c_str(), filePath.c_str()) == 0; }
    );
    targetIndex = (it != g_ctx.imageFiles.end()) ? static_cast<int>(std::distance(g_ctx.imageFiles.begin(), it)) : -1;

    if (!g_ctx.imageFiles.empty() && g_ctx.currentImageIndex != -1) {
        wchar_t folder[MAX_PATH] = { 0 };
        wcscpy_s(folder, MAX_PATH, filePath.c_str());
        PathRemoveFileSpecW(folder);
        if (g_ctx.currentDirectory == folder && targetIndex != -1) {
            nextIndex = (g_ctx.currentImageIndex + 1) % g_ctx.imageFiles.size();
            prevIndex = (g_ctx.currentImageIndex - 1 + g_ctx.imageFiles.size()) % g_ctx.imageFiles.size();
        }
        else {
            targetIndex = -1;
        }
    }

    ComPtr<IWICFormatConverter> preloadedConverter = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_ctx.preloadMutex);
        if (targetIndex != -1 && targetIndex == nextIndex && g_ctx.preloadedNextConverter) {
            preloadedConverter = g_ctx.preloadedNextConverter;
            g_ctx.preloadedNextConverter = nullptr;
        }
        else if (targetIndex != -1 && targetIndex == prevIndex && g_ctx.preloadedPrevConverter) {
            preloadedConverter = g_ctx.preloadedPrevConverter;
            g_ctx.preloadedPrevConverter = nullptr;
        }
    }

    if (preloadedConverter) {
        {
            std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
            g_ctx.stagedWicConverter = preloadedConverter;
        }
        PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOADED, (WPARAM)targetIndex, 0);
        return;
    }

    g_ctx.loadingThread = std::thread([filePath]() {
        if (FAILED(CoInitialize(nullptr))) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, 0);
            return;
        }

        ComPtr<IWICFormatConverter> converter = LoadImageInternal(filePath, false);

        if (!converter) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, 0);
            CoUninitialize();
            return;
        }

        wchar_t folder[MAX_PATH] = { 0 };
        wcscpy_s(folder, MAX_PATH, filePath.c_str());
        PathRemoveFileSpecW(folder);

        int foundIndex = -1;
        if (g_ctx.currentDirectory != folder) {
            g_ctx.currentDirectory = folder;
            GetImagesInDirectory(filePath.c_str());
        }

        auto it = std::find_if(g_ctx.imageFiles.begin(), g_ctx.imageFiles.end(),
            [&](const std::wstring& s) { return _wcsicmp(s.c_str(), filePath.c_str()) == 0; }
        );
        foundIndex = (it != g_ctx.imageFiles.end()) ? static_cast<int>(std::distance(g_ctx.imageFiles.begin(), it)) : -1;

        {
            std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
            g_ctx.stagedWicConverter = converter;
        }

        PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOADED, (WPARAM)foundIndex, 0);
        CoUninitialize();
        });
}

void FinalizeImageLoad(bool success, int foundIndex) {
    g_ctx.isLoading = false;
    if (g_ctx.loadingThread.joinable()) {
        g_ctx.loadingThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
        g_ctx.d2dBitmap = nullptr;
        if (success) {
            g_ctx.wicConverter = g_ctx.stagedWicConverter;
        }
        else {
            g_ctx.wicConverter = nullptr;
        }
        g_ctx.stagedWicConverter = nullptr;
    }

    if (success) {
        g_ctx.currentImageIndex = foundIndex;
        SetWindowTextW(g_ctx.hWnd, g_ctx.loadingFilePath.c_str());
        StartPreloading();
    }
    else {
        g_ctx.currentImageIndex = -1;
        SetWindowTextW(g_ctx.hWnd, L"Minimal Image Viewer");
        CleanupPreloadingThreads();
        {
            std::lock_guard<std::mutex> lock(g_ctx.preloadMutex);
            g_ctx.preloadedNextConverter = nullptr;
            g_ctx.preloadedPrevConverter = nullptr;
        }
    }

    CenterImage(true);
}

void StartPreloading() {
    CleanupPreloadingThreads();
    {
        std::lock_guard<std::mutex> lock(g_ctx.preloadMutex);
        g_ctx.preloadedNextConverter = nullptr;
        g_ctx.preloadedPrevConverter = nullptr;
    }

    if (g_ctx.imageFiles.empty() || g_ctx.currentImageIndex == -1) return;

    int nextIndex = (g_ctx.currentImageIndex + 1) % g_ctx.imageFiles.size();
    int prevIndex = (g_ctx.currentImageIndex - 1 + g_ctx.imageFiles.size()) % g_ctx.imageFiles.size();

    if (nextIndex == prevIndex) {
        prevIndex = -1;
    }

    if (nextIndex != g_ctx.currentImageIndex) {
        std::wstring nextPath = g_ctx.imageFiles[nextIndex];
        g_ctx.preloadingNextThread = std::thread([nextPath]() {
            if (FAILED(CoInitialize(nullptr))) return;
            ComPtr<IWICFormatConverter> converter = LoadImageInternal(nextPath, true);
            if (converter && !g_ctx.cancelPreloading) {
                std::lock_guard<std::mutex> lock(g_ctx.preloadMutex);
                g_ctx.preloadedNextConverter = converter;
            }
            CoUninitialize();
            });
    }

    if (prevIndex != -1 && prevIndex != g_ctx.currentImageIndex) {
        std::wstring prevPath = g_ctx.imageFiles[prevIndex];
        g_ctx.preloadingPrevThread = std::thread([prevPath]() {
            if (FAILED(CoInitialize(nullptr))) return;
            ComPtr<IWICFormatConverter> converter = LoadImageInternal(prevPath, true);
            if (converter && !g_ctx.cancelPreloading) {
                std::lock_guard<std::mutex> lock(g_ctx.preloadMutex);
                g_ctx.preloadedPrevConverter = converter;
            }
            CoUninitialize();
            });
    }
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
}

void DeleteCurrentImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) return;

    const std::wstring filePathToDelete = g_ctx.imageFiles[g_ctx.currentImageIndex];
    int indexToDelete = g_ctx.currentImageIndex;

    std::wstring msg = L"Are you sure you want to move this file to the Recycle Bin?\n\n" + filePathToDelete;

    if (MessageBoxW(g_ctx.hWnd, msg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {

        g_ctx.cancelPreloading = true;
        CleanupPreloadingThreads();
        {
            std::lock_guard<std::mutex> lock(g_ctx.preloadMutex);
            g_ctx.preloadedNextConverter = nullptr;
            g_ctx.preloadedPrevConverter = nullptr;
        }
        g_ctx.cancelPreloading = false;

        {
            std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
            g_ctx.wicConverter = nullptr;
            g_ctx.d2dBitmap = nullptr;
        }
        InvalidateRect(g_ctx.hWnd, nullptr, TRUE);
        UpdateWindow(g_ctx.hWnd);

        std::vector<wchar_t> pFromBuffer(filePathToDelete.length() + 2, 0);
        wcscpy_s(pFromBuffer.data(), pFromBuffer.size(), filePathToDelete.c_str());

        SHFILEOPSTRUCTW sfos = { 0 };
        sfos.hwnd = g_ctx.hWnd;
        sfos.wFunc = FO_DELETE;
        sfos.pFrom = pFromBuffer.data();
        sfos.fFlags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION;

        if (SHFileOperationW(&sfos) == 0 && !sfos.fAnyOperationsAborted) {
            g_ctx.imageFiles.erase(g_ctx.imageFiles.begin() + indexToDelete);

            if (g_ctx.imageFiles.empty()) {
                g_ctx.currentImageIndex = -1;
                g_ctx.currentDirectory.clear();
                InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
                SetWindowTextW(g_ctx.hWnd, L"Minimal Image Viewer");
            }
            else {
                g_ctx.currentImageIndex = indexToDelete;
                if (g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
                    g_ctx.currentImageIndex = 0;
                }
                LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex].c_str());
            }
        }
        else {
            MessageBoxW(g_ctx.hWnd, L"Failed to delete the file.", L"Error", MB_OK | MB_ICONERROR);
            LoadImageFromFile(filePathToDelete.c_str());
        }
    }
}

static ComPtr<IWICBitmapSource> GetSaveSource(const GUID& targetFormat) {
    std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
    if (!g_ctx.wicConverter) return nullptr;

    ComPtr<IWICBitmapSource> source;
    source = g_ctx.wicConverter;

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
    {
        std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
        if (!g_ctx.wicConverter) return;
    }

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
    }
    else {
        MessageBoxW(g_ctx.hWnd, L"Failed to save image.", L"Save As Error", MB_ICONERROR);
    }
}

void SaveImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
        if (g_ctx.wicConverter) {
            SaveImageAs();
        }
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
            return;
        }
    }

    hClipData = GetClipboardData(CF_DIB);
    if (hClipData) {
        LPBITMAPINFO lpbi = static_cast<LPBITMAPINFO>(GlobalLock(hClipData));
        if (lpbi) {
            {
                std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
                g_ctx.wicConverter = nullptr;
                g_ctx.d2dBitmap = nullptr;
            }
            HDC screenDC = GetDC(nullptr);
            BYTE* pBits = reinterpret_cast<BYTE*>(lpbi) + lpbi->bmiHeader.biSize + lpbi->bmiHeader.biClrUsed * sizeof(RGBQUAD);
            HBITMAP hTempBitmap = CreateDIBitmap(screenDC, &(lpbi->bmiHeader), CBM_INIT, pBits, lpbi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, screenDC);
            GlobalUnlock(hClipData);

            ComPtr<IWICFormatConverter> localConverter;
            if (hTempBitmap) {
                ComPtr<IWICBitmap> wicBitmap;
                if (SUCCEEDED(g_ctx.wicFactory->CreateBitmapFromHBITMAP(hTempBitmap, nullptr, WICBitmapUseAlpha, &wicBitmap))) {
                    ComPtr<IWICFormatConverter> converter;
                    if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&converter))) {
                        if (SUCCEEDED(converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                            localConverter = converter;
                        }
                    }
                }
                DeleteObject(hTempBitmap);
            }

            if (localConverter) {
                g_ctx.cancelPreloading = true;
                CleanupPreloadingThreads();
                {
                    std::lock_guard<std::mutex> lock(g_ctx.preloadMutex);
                    g_ctx.preloadedNextConverter = nullptr;
                    g_ctx.preloadedPrevConverter = nullptr;
                }
                g_ctx.cancelPreloading = false;

                std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
                g_ctx.wicConverter = localConverter;
                g_ctx.imageFiles.clear();
                g_ctx.currentImageIndex = -1;
                g_ctx.currentDirectory.clear();
                g_ctx.currentFilePathOverride = L"Pasted Image";
                CenterImage(true);
                SetWindowTextW(g_ctx.hWnd, L"Pasted Image");
            }
        }
    }
    CloseClipboard();
}

void HandleCopy() {
    ComPtr<IWICFormatConverter> converterCopy;
    int indexCopy = g_ctx.currentImageIndex;
    {
        std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
        converterCopy = g_ctx.wicConverter;
    }

    if (indexCopy == -1 && !converterCopy) return;
    if (!OpenClipboard(g_ctx.hWnd)) return;
    EmptyClipboard();

    if (indexCopy != -1) {
        const std::wstring& filePath = g_ctx.imageFiles[indexCopy];

        SIZE_T cbTotalSize = sizeof(DROPFILES) + (filePath.length() + 2) * sizeof(wchar_t);
        HGLOBAL hGlobal = GlobalAlloc(GHND, cbTotalSize);
        if (hGlobal) {
            LPDROPFILES pDropFiles = static_cast<LPDROPFILES>(GlobalLock(hGlobal));
            if (pDropFiles) {
                pDropFiles->pFiles = sizeof(DROPFILES);
                pDropFiles->pt.x = 0;
                pDropFiles->pt.y = 0;
                pDropFiles->fNC = FALSE;
                pDropFiles->fWide = TRUE;

                wchar_t* pszFile = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(pDropFiles) + sizeof(DROPFILES));
                wcscpy_s(pszFile, filePath.length() + 1, filePath.c_str());

                GlobalUnlock(hGlobal);
                SetClipboardData(CF_HDROP, hGlobal);
            }
        }
    }
    else if (converterCopy) {
        UINT width = 0, height = 0;
        converterCopy->GetSize(&width, &height);

        BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER) };
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC screenDC = GetDC(nullptr);
        HBITMAP hTempBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screenDC);

        if (hTempBitmap && bits) {
            converterCopy->CopyPixels(nullptr, static_cast<UINT>(width) * 4, static_cast<UINT>(width) * height * 4, static_cast<BYTE*>(bits));

            BITMAP bm{};
            GetObject(hTempBitmap, sizeof(BITMAP), &bm);
            DWORD dwBmpSize = ((bm.bmWidth * bmi.bmiHeader.biBitCount + 31) / 32) * 4 * bm.bmHeight;
            DWORD dwSizeOfDIB = dwBmpSize + sizeof(BITMAPINFOHEADER);
            HGLOBAL hGlobal = GlobalAlloc(GHND, dwSizeOfDIB);
            if (hGlobal) {
                char* lpGlobal = static_cast<char*>(GlobalLock(hGlobal));
                if (lpGlobal) {
                    memcpy(lpGlobal, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));

                    HDC hdc = GetDC(NULL);
                    GetDIBits(hdc, hTempBitmap, 0, (UINT)bm.bmHeight, lpGlobal + sizeof(BITMAPINFOHEADER), &bmi, DIB_RGB_COLORS);
                    ReleaseDC(NULL, hdc);

                    GlobalUnlock(hGlobal);
                    SetClipboardData(CF_DIB, hGlobal);
                }
            }
        }
        if (hTempBitmap) DeleteObject(hTempBitmap);
    }
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
