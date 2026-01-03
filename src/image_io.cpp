#include "viewer.h"
#include <memory>

extern AppContext g_ctx;

static HRESULT CreateDecoderFromStream_FullFileRead(const wchar_t* filePath, IWICBitmapDecoder** ppDecoder) {
    if (!ppDecoder) return E_POINTER;
    *ppDecoder = nullptr;

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (fileSize.QuadPart == 0) {
        CloseHandle(hFile);
        return E_FAIL;
    }
    DWORD dwFileSize = static_cast<DWORD>(fileSize.QuadPart);

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, dwFileSize);
    if (!hMem) {
        CloseHandle(hFile);
        return E_OUTOFMEMORY;
    }

    LPVOID pMem = GlobalLock(hMem);
    if (!pMem) {
        CloseHandle(hFile);
        GlobalFree(hMem);
        return E_FAIL;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, pMem, dwFileSize, &bytesRead, NULL) || bytesRead != dwFileSize) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
        CloseHandle(hFile);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    GlobalUnlock(hMem);
    CloseHandle(hFile);

    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &stream);
    if (FAILED(hr)) {
        GlobalFree(hMem);
        return hr;
    }

    hr = g_ctx.wicFactory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, ppDecoder);

    return hr;
}

static bool IsImageFile(const wchar_t* filePath) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g_ctx.wicFactory->CreateDecoderFromFilename(
        filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder
    );
    return SUCCEEDED(hr);
}

static ComPtr<IWICFormatConverter> LoadImageInternal(const std::wstring& filePath, bool isPreload, GUID* pContainerFormat) {
    *pContainerFormat = GUID_NULL;
    if (isPreload && g_ctx.cancelPreloading) {
        return nullptr;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = CreateDecoderFromStream_FullFileRead(filePath.c_str(), &decoder);
    if (FAILED(hr)) return nullptr;

    decoder->GetContainerFormat(pContainerFormat);

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
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.wicConverter = nullptr;
        g_ctx.wicConverterOriginal = nullptr;
        g_ctx.d2dBitmap = nullptr;
        g_ctx.animationD2DBitmaps.clear();
        g_ctx.isAnimated = false;
        g_ctx.animationFrameConverters.clear();
        g_ctx.animationFrameDelays.clear();
        g_ctx.currentAnimationFrame = 0;
        g_ctx.originalContainerFormat = GUID_NULL;
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
            size_t size = g_ctx.imageFiles.size();
            nextIndex = (g_ctx.currentImageIndex + 1) % static_cast<int>(size);
            prevIndex = (g_ctx.currentImageIndex - 1 + static_cast<int>(size)) % static_cast<int>(size);
        }
        else {
            targetIndex = -1;
        }
    }

    ComPtr<IWICFormatConverter> preloadedConverter = nullptr;
    GUID preloadedFormat = {};
    {
        CriticalSectionLock lock(g_ctx.preloadMutex);
        if (targetIndex != -1 && targetIndex == nextIndex && g_ctx.preloadedNextConverter) {
            preloadedConverter = g_ctx.preloadedNextConverter;
            preloadedFormat = g_ctx.preloadedNextFormat;
            g_ctx.preloadedNextConverter = nullptr;
            g_ctx.preloadedNextFormat = {};
        }
        else if (targetIndex != -1 && targetIndex == prevIndex && g_ctx.preloadedPrevConverter) {
            preloadedConverter = g_ctx.preloadedPrevConverter;
            preloadedFormat = g_ctx.preloadedPrevFormat;
            g_ctx.preloadedPrevConverter = nullptr;
            g_ctx.preloadedPrevFormat = {};
        }
    }

    if (preloadedConverter) {
        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.stagedWicConverter = preloadedConverter;
            g_ctx.stagedWicConverterOriginal = preloadedConverter;
            g_ctx.isAnimated = false;
            g_ctx.animationFrameConverters.clear();
            g_ctx.animationFrameDelays.clear();
            g_ctx.originalContainerFormat = preloadedFormat;
        }
        PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOADED, (WPARAM)targetIndex, 0);
        return;
    }

    g_ctx.loadingThread = std::thread([filePath]() {
        if (FAILED(CoInitialize(nullptr))) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, 0);
            return;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = CreateDecoderFromStream_FullFileRead(filePath.c_str(), &decoder);

        if (FAILED(hr)) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, 0);
            CoUninitialize();
            return;
        }

        GUID containerFormat = {};
        decoder->GetContainerFormat(&containerFormat);

        UINT frameCount = 0;
        decoder->GetFrameCount(&frameCount);

        bool isAnimated = false;
        ComPtr<IWICFormatConverter> firstFrameConverter;
        std::vector<ComPtr<IWICFormatConverter>> frameConverters;
        std::vector<UINT> frameDelays;

        if (frameCount > 1) {
            isAnimated = true;
            for (UINT i = 0; i < frameCount; ++i) {
                ComPtr<IWICBitmapFrameDecode> frame;
                if (FAILED(decoder->GetFrame(i, &frame))) continue;

                ComPtr<IWICMetadataQueryReader> metadataReader;
                frame->GetMetadataQueryReader(&metadataReader);

                PROPVARIANT propValue;
                PropVariantInit(&propValue);
                UINT delay = 100;

                if (metadataReader && SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Delay", &propValue))) {
                    if (propValue.vt == VT_UI2) {
                        delay = propValue.uiVal * 10;
                    }
                    PropVariantClear(&propValue);
                }

                if (delay < 10) delay = 100;
                frameDelays.push_back(delay);

                ComPtr<IWICFormatConverter> converter;
                if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&converter))) {
                    if (SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                        frameConverters.push_back(converter);
                    }
                }
            }
            if (!frameConverters.empty()) {
                firstFrameConverter = frameConverters[0];
            }
        }
        else {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                ComPtr<IWICFormatConverter> converter;
                if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&converter))) {
                    if (SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                        firstFrameConverter = converter;
                    }
                }
            }
        }

        if (!firstFrameConverter) {
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
            GetImagesInDirectory(g_ctx.currentDirectory.c_str());
        }

        auto it = std::find_if(g_ctx.imageFiles.begin(), g_ctx.imageFiles.end(),
            [&](const std::wstring& s) { return _wcsicmp(s.c_str(), filePath.c_str()) == 0; }
        );
        foundIndex = (it != g_ctx.imageFiles.end()) ? static_cast<int>(std::distance(g_ctx.imageFiles.begin(), it)) : -1;

        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.stagedWicConverter = firstFrameConverter;
            g_ctx.stagedWicConverterOriginal = firstFrameConverter;
            g_ctx.isAnimated = isAnimated;
            g_ctx.animationFrameConverters = frameConverters;
            g_ctx.animationFrameDelays = frameDelays;
            g_ctx.currentAnimationFrame = 0;
            g_ctx.animationD2DBitmaps.clear();
            g_ctx.originalContainerFormat = containerFormat;
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

    KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.d2dBitmap = nullptr;
        g_ctx.animationD2DBitmaps.clear();
        if (success) {
            if (g_ctx.isAnimated) {
                g_ctx.wicConverter = nullptr;
                g_ctx.wicConverterOriginal = nullptr;
            }
            else {
                g_ctx.wicConverter = g_ctx.stagedWicConverter;
                g_ctx.wicConverterOriginal = g_ctx.stagedWicConverterOriginal;
                g_ctx.animationFrameConverters.clear();
                g_ctx.animationFrameDelays.clear();
            }
        }
        else {
            g_ctx.wicConverter = nullptr;
            g_ctx.wicConverterOriginal = nullptr;
            g_ctx.isAnimated = false;
            g_ctx.animationFrameConverters.clear();
            g_ctx.animationFrameDelays.clear();
            g_ctx.originalContainerFormat = GUID_NULL;
        }
        g_ctx.stagedWicConverter = nullptr;
        g_ctx.stagedWicConverterOriginal = nullptr;
        g_ctx.currentAnimationFrame = 0;
    }

    if (success) {
        g_ctx.currentImageIndex = foundIndex;
        SetWindowTextW(g_ctx.hWnd, g_ctx.loadingFilePath.c_str());
        StartPreloading();
        if (g_ctx.isAnimated && !g_ctx.animationFrameDelays.empty()) {
            SetTimer(g_ctx.hWnd, ANIMATION_TIMER_ID, g_ctx.animationFrameDelays[0], nullptr);
        }
    }
    else {
        g_ctx.currentImageIndex = -1;
        SetWindowTextW(g_ctx.hWnd, L"Minimal Image Viewer");
        CleanupPreloadingThreads();
        {
            CriticalSectionLock lock(g_ctx.preloadMutex);
            g_ctx.preloadedNextConverter = nullptr;
            g_ctx.preloadedPrevConverter = nullptr;
            g_ctx.preloadedNextFormat = {};
            g_ctx.preloadedPrevFormat = {};
        }
    }

    if (success && g_ctx.defaultZoomMode == DefaultZoomMode::Actual) {
        SetActualSize();
    }
    else {
        CenterImage(true);
    }
}

void StartPreloading() {
    CleanupPreloadingThreads();
    {
        CriticalSectionLock lock(g_ctx.preloadMutex);
        g_ctx.preloadedNextConverter = nullptr;
        g_ctx.preloadedPrevConverter = nullptr;
        g_ctx.preloadedNextFormat = {};
        g_ctx.preloadedPrevFormat = {};
    }

    if (g_ctx.imageFiles.empty() || g_ctx.currentImageIndex == -1) return;

    size_t size = g_ctx.imageFiles.size();
    int nextIndex = (g_ctx.currentImageIndex + 1) % static_cast<int>(size);
    int prevIndex = (g_ctx.currentImageIndex - 1 + static_cast<int>(size)) % static_cast<int>(size);

    if (nextIndex == prevIndex) {
        prevIndex = -1;
    }

    if (nextIndex != g_ctx.currentImageIndex) {
        std::wstring nextPath = g_ctx.imageFiles[nextIndex];
        g_ctx.preloadingNextThread = std::thread([nextPath]() {
            if (FAILED(CoInitialize(nullptr))) return;
            GUID containerFormat = {};
            ComPtr<IWICFormatConverter> converter = LoadImageInternal(nextPath, true, &containerFormat);
            if (converter && !g_ctx.cancelPreloading) {
                CriticalSectionLock lock(g_ctx.preloadMutex);
                g_ctx.preloadedNextConverter = converter;
                g_ctx.preloadedNextFormat = containerFormat;
            }
            CoUninitialize();
            });
    }

    if (prevIndex != -1 && prevIndex != g_ctx.currentImageIndex) {
        std::wstring prevPath = g_ctx.imageFiles[prevIndex];
        g_ctx.preloadingPrevThread = std::thread([prevPath]() {
            if (FAILED(CoInitialize(nullptr))) return;
            GUID containerFormat = {};
            ComPtr<IWICFormatConverter> converter = LoadImageInternal(prevPath, true, &containerFormat);
            if (converter && !g_ctx.cancelPreloading) {
                CriticalSectionLock lock(g_ctx.preloadMutex);
                g_ctx.preloadedPrevConverter = converter;
                g_ctx.preloadedPrevFormat = containerFormat;
            }
            CoUninitialize();
            });
    }
}


void GetImagesInDirectory(const wchar_t* directoryPath) {
    g_ctx.imageFiles.clear();

    struct FileInfo {
        std::wstring path;
        FILETIME writeTime = {};
        LARGE_INTEGER fileSize = {};
    };
    std::vector<FileInfo> foundFiles;

    WIN32_FIND_DATAW fd{};
    wchar_t searchPath[MAX_PATH] = { 0 };
    PathCombineW(searchPath, directoryPath, L"*.*");

    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                wchar_t fullPath[MAX_PATH] = { 0 };
                PathCombineW(fullPath, directoryPath, fd.cFileName);
                if (IsImageFile(fullPath)) {
                    FileInfo info;
                    info.path = fullPath;
                    info.writeTime = fd.ftLastWriteTime;
                    info.fileSize.LowPart = fd.nFileSizeLow;
                    info.fileSize.HighPart = fd.nFileSizeHigh;
                    foundFiles.push_back(info);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    std::sort(foundFiles.begin(), foundFiles.end(), [&](const FileInfo& a, const FileInfo& b) {
        int compareResult = 0;
        switch (g_ctx.currentSortCriteria) {
        case SortCriteria::ByDateModified:
            compareResult = CompareFileTime(&a.writeTime, &b.writeTime);
            break;
        case SortCriteria::ByFileSize:
            if (a.fileSize.QuadPart < b.fileSize.QuadPart) compareResult = -1;
            else if (a.fileSize.QuadPart > b.fileSize.QuadPart) compareResult = 1;
            else compareResult = 0;
            break;
        case SortCriteria::ByName:
        default:
            compareResult = StrCmpLogicalW(a.path.c_str(), b.path.c_str());
            break;
        }

        if (g_ctx.isSortAscending) {
            return compareResult < 0;
        }
        else {
            return compareResult > 0;
        }
        });

    for (const auto& info : foundFiles) {
        g_ctx.imageFiles.push_back(info.path);
    }
}

void DeleteCurrentImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) return;

    const std::wstring filePathToDelete = g_ctx.imageFiles[g_ctx.currentImageIndex];
    int indexToDelete = g_ctx.currentImageIndex;

    std::wstring msg = L"Are you sure you want to move this file to the Recycle Bin?\n\n" + filePathToDelete;

    if (MessageBoxW(g_ctx.hWnd, msg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {

        KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
        g_ctx.cancelPreloading = true;
        CleanupPreloadingThreads();
        {
            CriticalSectionLock lock(g_ctx.preloadMutex);
            g_ctx.preloadedNextConverter = nullptr;
            g_ctx.preloadedPrevConverter = nullptr;
            g_ctx.preloadedNextFormat = {};
            g_ctx.preloadedPrevFormat = {};
        }
        g_ctx.cancelPreloading = false;

        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.wicConverter = nullptr;
            g_ctx.wicConverterOriginal = nullptr;
            g_ctx.d2dBitmap = nullptr;
            g_ctx.isAnimated = false;
            g_ctx.animationFrameConverters.clear();
            g_ctx.animationD2DBitmaps.clear();
            g_ctx.animationFrameDelays.clear();
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
            KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
            {
                CriticalSectionLock lock(g_ctx.wicMutex);
                g_ctx.wicConverter = nullptr;
                g_ctx.wicConverterOriginal = nullptr;
                g_ctx.d2dBitmap = nullptr;
                g_ctx.isAnimated = false;
                g_ctx.animationFrameConverters.clear();
                g_ctx.animationD2DBitmaps.clear();
                g_ctx.animationFrameDelays.clear();
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
                    CriticalSectionLock lock(g_ctx.preloadMutex);
                    g_ctx.preloadedNextConverter = nullptr;
                    g_ctx.preloadedPrevConverter = nullptr;
                    g_ctx.preloadedNextFormat = {};
                    g_ctx.preloadedPrevFormat = {};
                }
                g_ctx.cancelPreloading = false;

                CriticalSectionLock lock(g_ctx.wicMutex);
                g_ctx.wicConverter = localConverter;
                g_ctx.wicConverterOriginal = localConverter;
                g_ctx.imageFiles.clear();
                g_ctx.currentImageIndex = -1;
                g_ctx.currentDirectory.clear();
                g_ctx.loadingFilePath.clear();
                g_ctx.currentFilePathOverride = L"Pasted Image";
                g_ctx.originalContainerFormat = GUID_ContainerFormatBmp;
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
        CriticalSectionLock lock(g_ctx.wicMutex);
        if (g_ctx.isAnimated && g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size()) {
            converterCopy = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
        }
        else {
            converterCopy = g_ctx.wicConverter;
        }
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