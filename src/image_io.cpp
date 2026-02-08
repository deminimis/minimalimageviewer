#include "viewer.h"
#include <memory>
#include <algorithm>
#include <shlwapi.h> 

extern AppContext g_ctx;

static bool IsSequenceValid(int seqId) {
    return g_ctx.loadSequenceId == seqId;
}

static HRESULT CreateDecoderFromStream_FullFileRead(
    IWICImagingFactory* pFactory,
    const wchar_t* filePath,
    IWICBitmapDecoder** ppDecoder,
    int seqId)
{
    if (!ppDecoder) return E_POINTER;
    *ppDecoder = nullptr;

    if (seqId != -1 && !IsSequenceValid(seqId)) return E_ABORT;

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

    if (seqId != -1 && !IsSequenceValid(seqId)) {
        CloseHandle(hFile);
        return E_ABORT;
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

    if (seqId != -1 && !IsSequenceValid(seqId)) {
        GlobalFree(hMem);
        return E_ABORT;
    }

    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &stream);
    if (FAILED(hr)) {
        GlobalFree(hMem);
        return hr;
    }

    hr = pFactory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, ppDecoder);

    return hr;
}

HRESULT CreateDecoderFromFile(const wchar_t* filePath, IWICBitmapDecoder** ppDecoder) {
    return CreateDecoderFromStream_FullFileRead(g_ctx.wicFactory, filePath, ppDecoder, -1);
}


static bool IsImageFile(IWICImagingFactory* pFactory, const wchar_t* filePath) {
    ComPtr<IWICBitmapDecoder> decoder;
    // Open lightly (in case editor not done saving)
    HRESULT hr = pFactory->CreateDecoderFromFilename(
        filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder
    );
    return SUCCEEDED(hr);
}

// directory scanner that checks sequence validity
std::vector<std::wstring> ScanDirectory(IWICImagingFactory* pFactory, const std::wstring& directoryPath, int seqId) {
    struct FileInfo {
        std::wstring path;
        FILETIME writeTime = {};
        LARGE_INTEGER fileSize = {};
    };
    std::vector<FileInfo> foundFiles;
    foundFiles.reserve(100);

    WIN32_FIND_DATAW fd{};
    wchar_t searchPath[MAX_PATH] = { 0 };
    PathCombineW(searchPath, directoryPath.c_str(), L"*.*");

    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!IsSequenceValid(seqId)) {
                FindClose(hFind);
                return {};
            }
            if (g_ctx.cancelPreloading) {
                FindClose(hFind);
                return {};
            }

            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                wchar_t fullPath[MAX_PATH] = { 0 };
                PathCombineW(fullPath, directoryPath.c_str(), fd.cFileName);

                if (IsImageFile(pFactory, fullPath)) {
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

    if (!IsSequenceValid(seqId)) return {};

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

    std::vector<std::wstring> result;
    result.reserve(foundFiles.size());
    for (const auto& info : foundFiles) {
        result.push_back(info.path);
    }
    return result;
}

void LoadImageFromFile(const std::wstring& filePath, bool startAtEnd) {
    CleanupPreloadingThreads();
    g_ctx.cancelPreloading = false;

    int mySeqId = ++g_ctx.loadSequenceId;

    g_ctx.isLoading = true;
    g_ctx.loadingFilePath = filePath;
    g_ctx.startAtEnd = startAtEnd;

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

        g_ctx.stagedFrames.clear();
        g_ctx.stagedDelays.clear();
        g_ctx.stagedWidth = 0;
        g_ctx.stagedHeight = 0;
    }
    g_ctx.currentFilePathOverride.clear();

    // Check if directory changed
    wchar_t folder[MAX_PATH] = { 0 };
    wcscpy_s(folder, MAX_PATH, filePath.c_str());
    PathRemoveFileSpecW(folder);

    if (g_ctx.currentDirectory != folder) {
        g_ctx.imageFiles.clear();
        g_ctx.currentImageIndex = -1;
        g_ctx.currentDirectory = folder;
    }

    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);

    std::thread([filePath, mySeqId]() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            if (IsSequenceValid(mySeqId)) {
                PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            }
            return;
        }

        ComPtr<IWICImagingFactory> localFactory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&localFactory));

        if (FAILED(hr)) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            CoUninitialize();
            return;
        }

        // Load Image to Memory Buffer (Handles Animation)
        ComPtr<IWICBitmapDecoder> decoder;
        hr = CreateDecoderFromStream_FullFileRead(localFactory, filePath.c_str(), &decoder, mySeqId);

        if (!IsSequenceValid(mySeqId)) { CoUninitialize(); return; }

        if (FAILED(hr)) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            CoUninitialize();
            return;
        }

        GUID containerFormat = {};
        decoder->GetContainerFormat(&containerFormat);

        UINT frameCount = 0;
        decoder->GetFrameCount(&frameCount);
        if (frameCount == 0) frameCount = 1;

        std::vector<std::vector<BYTE>> allFramesPixels;
        std::vector<UINT> allFramesDelays;
        UINT width = 0, height = 0;

        for (UINT i = 0; i < frameCount; ++i) {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(i, &frame))) continue;

            // delay metadata
            UINT delay = 100;
            ComPtr<IWICMetadataQueryReader> metadataReader;
            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                PROPVARIANT propValue;
                PropVariantInit(&propValue);
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Delay", &propValue))) {
                    if (propValue.vt == VT_UI2) {
                        delay = propValue.uiVal * 10;
                    }
                    PropVariantClear(&propValue);
                }
            }
            if (delay < 10) delay = 100;
            allFramesDelays.push_back(delay);

            // Get Pixels
            ComPtr<IWICFormatConverter> converter;
            if (SUCCEEDED(localFactory->CreateFormatConverter(&converter))) {
                if (SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                    converter->GetSize(&width, &height);

                    UINT stride = width * 4;
                    UINT size = stride * height;
                    std::vector<BYTE> rawPixels(size);
                    if (SUCCEEDED(converter->CopyPixels(nullptr, stride, size, rawPixels.data()))) {
                        allFramesPixels.push_back(std::move(rawPixels));
                    }
                }
            }

            if (!IsSequenceValid(mySeqId)) { CoUninitialize(); return; }
        }

        if (allFramesPixels.empty()) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            CoUninitialize();
            return;
        }

        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.stagedFrames = std::move(allFramesPixels);
            g_ctx.stagedDelays = std::move(allFramesDelays);
            g_ctx.stagedWidth = width;
            g_ctx.stagedHeight = height;
            g_ctx.originalContainerFormat = containerFormat;
        }

        PostMessage(g_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);

        // Background Scan
        wchar_t folder[MAX_PATH] = { 0 };
        wcscpy_s(folder, MAX_PATH, filePath.c_str());
        PathRemoveFileSpecW(folder);

        std::vector<std::wstring> newFiles = ScanDirectory(localFactory, folder, mySeqId);

        if (!IsSequenceValid(mySeqId)) { CoUninitialize(); return; }

        int foundIndex = -1;
        auto it = std::find_if(newFiles.begin(), newFiles.end(),
            [&](const std::wstring& s) { return _wcsicmp(s.c_str(), filePath.c_str()) == 0; }
        );
        foundIndex = (it != newFiles.end()) ? static_cast<int>(std::distance(newFiles.begin(), it)) : -1;

        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            g_ctx.stagedImageFiles = std::move(newFiles);
            g_ctx.stagedFoundIndex = foundIndex;
        }

        PostMessage(g_ctx.hWnd, WM_APP_DIR_READY, 0, (LPARAM)mySeqId);

        CoUninitialize();
        }).detach();
}

void OnImageReady(bool success, int seqId) {
    if (g_ctx.loadSequenceId != seqId) return;

    if (success) {
        CriticalSectionLock lock(g_ctx.wicMutex);

        if (g_ctx.stagedFrames.empty()) {
            g_ctx.isLoading = false;
            return;
        }

        bool animated = g_ctx.stagedFrames.size() > 1;

        // Disable automatic animation for TIFFs 
        if (g_ctx.originalContainerFormat == GUID_ContainerFormatTiff) {
            animated = false;
        }

        g_ctx.isAnimated = animated;
        g_ctx.animationFrameConverters.clear();
        g_ctx.animationFrameDelays.clear();

        // Determine start frame
        g_ctx.currentAnimationFrame = 0;
        if (g_ctx.startAtEnd && !animated && !g_ctx.stagedFrames.empty()) {
            g_ctx.currentAnimationFrame = static_cast<UINT>(g_ctx.stagedFrames.size() - 1);
        }
        // Reset flag
        g_ctx.startAtEnd = false;

        UINT stride = g_ctx.stagedWidth * 4;

        // Reconstruct WIC 
        for (const auto& pixels : g_ctx.stagedFrames) {
            ComPtr<IWICBitmap> memoryBitmap;
            HRESULT hr = g_ctx.wicFactory->CreateBitmapFromMemory(
                g_ctx.stagedWidth,
                g_ctx.stagedHeight,
                GUID_WICPixelFormat32bppPBGRA,
                stride,
                static_cast<UINT>(pixels.size()),
                const_cast<BYTE*>(pixels.data()),
                &memoryBitmap
            );

            if (SUCCEEDED(hr)) {
                ComPtr<IWICFormatConverter> converter;
                g_ctx.wicFactory->CreateFormatConverter(&converter);
                converter->Initialize(memoryBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
                g_ctx.animationFrameConverters.push_back(converter);
            }
        }

        if (!g_ctx.animationFrameConverters.empty()) {
            // Set the primary converter based on calculated start frame
            if (g_ctx.currentAnimationFrame >= g_ctx.animationFrameConverters.size()) {
                g_ctx.currentAnimationFrame = 0;
            }
            g_ctx.wicConverter = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
            g_ctx.wicConverterOriginal = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];

            if (animated) {
                g_ctx.animationFrameDelays = g_ctx.stagedDelays;
                // Start animation timer
                SetTimer(g_ctx.hWnd, ANIMATION_TIMER_ID, g_ctx.animationFrameDelays[0], nullptr);
            }
        }


        g_ctx.stagedFrames.clear();
        g_ctx.stagedDelays.clear();

        g_ctx.isLoading = false;

        if (g_ctx.preserveView) {
            g_ctx.preserveView = false;
        }
        else if (g_ctx.defaultZoomMode == DefaultZoomMode::Actual) {
            SetActualSize();
        }
        else {
            CenterImage(true);
        }

        SetWindowTextW(g_ctx.hWnd, g_ctx.loadingFilePath.c_str());

        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(g_ctx.loadingFilePath.c_str(), GetFileExInfoStandard, &fad)) {
            g_ctx.lastWriteTime = fad.ftLastWriteTime;
        }
    }
    else {
        g_ctx.isLoading = false;
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.wicConverter = nullptr;
        g_ctx.wicConverterOriginal = nullptr;
        SetWindowTextW(g_ctx.hWnd, L"Load Failed");
    }

    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void OnDirReady(int seqId) {
    if (g_ctx.loadSequenceId != seqId) return;

    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.imageFiles = std::move(g_ctx.stagedImageFiles);
        g_ctx.currentImageIndex = g_ctx.stagedFoundIndex;
    }

}

// Fallback  handler
void FinalizeImageLoad(bool success, int foundIndex) {
    g_ctx.isLoading = false;
    KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);

    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        g_ctx.d2dBitmap = nullptr;
        g_ctx.animationD2DBitmaps.clear();
        g_ctx.wicConverter = nullptr;
        g_ctx.wicConverterOriginal = nullptr;
        g_ctx.stagedFrames.clear();
    }

    if (success) {
        g_ctx.currentImageIndex = foundIndex;
        SetWindowTextW(g_ctx.hWnd, g_ctx.loadingFilePath.c_str());
        CenterImage(true);
    }
    else {
        g_ctx.currentImageIndex = -1;
        SetWindowTextW(g_ctx.hWnd, L"Minimal Image Viewer");
        CenterImage(true);
    }
}

void CleanupLoadingThread() {
    g_ctx.cancelPreloading = true;
    CleanupPreloadingThreads();
    KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
}

void StartPreloading() {
}

void CleanupPreloadingThreads() {
}