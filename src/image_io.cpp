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

    // Use WIC's native memory-mapped file decoding.
    return pFactory->CreateDecoderFromFilename(
        filePath,
        NULL,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        ppDecoder
    );
}

HRESULT CreateDecoderFromFile(const wchar_t* filePath, IWICBitmapDecoder** ppDecoder) {
    return CreateDecoderFromStream_FullFileRead(g_ctx.wicFactory, filePath, ppDecoder, -1);
}


static bool IsImageFile(const wchar_t* filePath) {
    const wchar_t* ext = PathFindExtensionW(filePath);
    if (!ext) return false;

    static const wchar_t* validExts[] = {
        L".jpg", L".jpeg", L".png", L".bmp", L".gif",
        L".tiff", L".tif", L".ico", L".webp", L".heic",
        L".heif", L".avif", L".cr2", L".cr3", L".nef",
        L".dng", L".arw", L".orf", L".rw2"
    };

    for (const auto& validExt : validExts) {
        if (_wcsicmp(ext, validExt) == 0) return true;
    }
    return false;
}

// directory scanner that checks sequence validity
std::vector<std::wstring> ScanDirectory(const std::wstring& directoryPath, int seqId) {
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

    HANDLE hFind = FindFirstFileExW(searchPath, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
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
                if (IsImageFile(fd.cFileName)) {
                    wchar_t fullPath[MAX_PATH] = { 0 };
                    PathCombineW(fullPath, directoryPath.c_str(), fd.cFileName);

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
    g_ctx.loadStartTime = GetTickCount64();
    SetTimer(g_ctx.hWnd, LOADING_TIMER_ID, 700, nullptr);
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
        g_ctx.stagedOrientation = 1;
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
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&localFactory));

        if (FAILED(hr)) {
            PostMessage(g_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            CoUninitialize();
            return;
        }

      
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
        UINT canvasWidth = 0, canvasHeight = 0;

        // Retrieve global logical screen descriptor 
        ComPtr<IWICMetadataQueryReader> decoderMetadata;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(&decoderMetadata))) {
            PROPVARIANT propValue;
            PropVariantInit(&propValue);
            if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Width", &propValue))) {
                if (propValue.vt == VT_UI2) canvasWidth = propValue.uiVal;
                PropVariantClear(&propValue);
            }
            if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Height", &propValue))) {
                if (propValue.vt == VT_UI2) canvasHeight = propValue.uiVal;
                PropVariantClear(&propValue);
            }
        }

        std::vector<BYTE> compositeBuffer;
        std::vector<BYTE> previousCompositeBuffer;
        UINT exifOrientation = 1;

        for (UINT i = 0; i < frameCount; ++i) {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(i, &frame))) continue;

            UINT frameWidth = 0, frameHeight = 0;
            frame->GetSize(&frameWidth, &frameHeight);

            // Fallback if global dimensions aren't found in metadata
            if (canvasWidth == 0 || canvasHeight == 0) {
                canvasWidth = frameWidth;
                canvasHeight = frameHeight;
            }

            UINT delay = 100;
            UINT disposal = 0;
            UINT left = 0;
            UINT top = 0;

            ComPtr<IWICMetadataQueryReader> metadataReader;
            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                PROPVARIANT propValue;
                PropVariantInit(&propValue);
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Delay", &propValue))) {
                    if (propValue.vt == VT_UI2) delay = propValue.uiVal * 10;
                    PropVariantClear(&propValue);
                }
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Disposal", &propValue))) {
                    if (propValue.vt == VT_UI1) disposal = propValue.bVal;
                    PropVariantClear(&propValue);
                }
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/imgdesc/Left", &propValue))) {
                    if (propValue.vt == VT_UI2) left = propValue.uiVal;
                    PropVariantClear(&propValue);
                }
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/imgdesc/Top", &propValue))) {
                    if (propValue.vt == VT_UI2) top = propValue.uiVal;
                    PropVariantClear(&propValue);
                }
                if (i == 0) {
                    if (SUCCEEDED(metadataReader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &propValue))) {
                        if (propValue.vt == VT_UI2) exifOrientation = propValue.uiVal;
                        PropVariantClear(&propValue);
                    }
                }
            }
            if (delay < 10) delay = 100;
            allFramesDelays.push_back(delay);

            UINT canvasStride = canvasWidth * 4;
            UINT canvasSize = canvasStride * canvasHeight;

            if (compositeBuffer.size() != canvasSize) {
                compositeBuffer.assign(canvasSize, 0);
                previousCompositeBuffer.assign(canvasSize, 0);
            }

            if (disposal == 3) {
                previousCompositeBuffer = compositeBuffer;
            }

            ComPtr<IWICFormatConverter> converter;
            if (SUCCEEDED(localFactory->CreateFormatConverter(&converter))) {
                if (SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {

                    UINT frameStride = frameWidth * 4;
                    UINT frameSize = frameStride * frameHeight;
                    std::vector<BYTE> framePixels(frameSize);

                    if (SUCCEEDED(converter->CopyPixels(nullptr, frameStride, frameSize, framePixels.data()))) {

                        
                        for (UINT y = 0; y < frameHeight; ++y) {
                            if (top + y >= canvasHeight) break; 

                            BYTE* destRow = compositeBuffer.data() + (top + y) * canvasStride + (left * 4);
                            BYTE* srcRow = framePixels.data() + y * frameStride;

                            for (UINT x = 0; x < frameWidth; ++x) {
                                if (left + x >= canvasWidth) break; 

                                UINT dp = x * 4;
                                UINT sp = x * 4;

                                BYTE alpha = srcRow[sp + 3];
                                if (alpha == 255) {
                                    destRow[dp] = srcRow[sp];
                                    destRow[dp + 1] = srcRow[sp + 1];
                                    destRow[dp + 2] = srcRow[sp + 2];
                                    destRow[dp + 3] = 255;
                                }
                                else if (alpha > 0) {
                                    BYTE invAlpha = 255 - alpha;
                                    destRow[dp] = srcRow[sp] + (destRow[dp] * invAlpha) / 255;
                                    destRow[dp + 1] = srcRow[sp + 1] + (destRow[dp + 1] * invAlpha) / 255;
                                    destRow[dp + 2] = srcRow[sp + 2] + (destRow[dp + 2] * invAlpha) / 255;
                                    destRow[dp + 3] = alpha + (destRow[dp + 3] * invAlpha) / 255;
                                }
                            }
                        }

                        allFramesPixels.push_back(compositeBuffer);
                    }
                }
            }

            // Apply disposal method for the NEXT frame
            if (disposal == 2) {
               
                for (UINT y = 0; y < frameHeight; ++y) {
                    if (top + y >= canvasHeight) break;
                    BYTE* destRow = compositeBuffer.data() + (top + y) * canvasStride + (left * 4);
                    for (UINT x = 0; x < frameWidth; ++x) {
                        if (left + x >= canvasWidth) break;
                        UINT dp = x * 4;
                        destRow[dp] = 0;
                        destRow[dp + 1] = 0;
                        destRow[dp + 2] = 0;
                        destRow[dp + 3] = 0;
                    }
                }
            }
            else if (disposal == 3) {
                compositeBuffer = previousCompositeBuffer; 
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

            // Stage using the global canvas dimensions
            g_ctx.stagedWidth = canvasWidth;
            g_ctx.stagedHeight = canvasHeight;
            g_ctx.originalContainerFormat = containerFormat;
            g_ctx.stagedOrientation = exifOrientation;
        }

        PostMessage(g_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
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
        g_ctx.currentOrientation = g_ctx.stagedOrientation;

        g_ctx.isFading = true;
        g_ctx.fadeStartTime = GetTickCount64();

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

        // Start background folder scan ONLY AFTER image is ready to display
        std::wstring currentFilePath = g_ctx.loadingFilePath;
        int currentSeqId = seqId;
        std::thread([currentFilePath, currentSeqId]() {
            wchar_t folder[MAX_PATH] = { 0 };
            wcscpy_s(folder, MAX_PATH, currentFilePath.c_str());
            PathRemoveFileSpecW(folder);

            std::vector<std::wstring> newFiles = ScanDirectory(folder, currentSeqId);

            if (!IsSequenceValid(currentSeqId)) return;

            int foundIndex = -1;
            auto it = std::find_if(newFiles.begin(), newFiles.end(),
                [&](const std::wstring& s) { return _wcsicmp(s.c_str(), currentFilePath.c_str()) == 0; }
            );
            foundIndex = (it != newFiles.end()) ? static_cast<int>(std::distance(newFiles.begin(), it)) : -1;

            {
                CriticalSectionLock lock(g_ctx.wicMutex);
                g_ctx.stagedImageFiles = std::move(newFiles);
                g_ctx.stagedFoundIndex = foundIndex;
            }

            PostMessage(g_ctx.hWnd, WM_APP_DIR_READY, 0, (LPARAM)currentSeqId);
            }).detach();

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