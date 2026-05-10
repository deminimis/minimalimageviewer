#include "viewer.h"
#include <memory>
#include <algorithm>
#include <shlwapi.h> 



static bool IsImageFile(const wchar_t* filePath) {
    const wchar_t* ext = PathFindExtensionW(filePath);
    if (!ext) return false;
    static const wchar_t* validExts[] = {
        L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tiff", L".tif",
        L".ico", L".webp", L".heic", L".heif", L".avif", L".cr2", L".cr3",
        L".nef", L".dng", L".arw", L".orf", L".rw2", L".svg"
    };
    for (const auto& validExt : validExts) {
        if (_wcsicmp(ext, validExt) == 0) return true;
    }
    return false;
}

bool ViewerApp::IsSequenceValid(int seqId) {
    return m_ctx.loadSequenceId == seqId;
}

HRESULT ViewerApp::CreateDecoderFromStream_FullFileRead(
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

HRESULT ViewerApp::CreateDecoderFromFile(const wchar_t* filePath, IWICBitmapDecoder** ppDecoder) {
    return CreateDecoderFromStream_FullFileRead(m_ctx.wicFactory.Get(), filePath, ppDecoder, -1);
}


void ViewerApp::LoadImageFromFile(const std::wstring& filePath, bool startAtEnd) {
    CleanupPreloadingThreads();
    m_ctx.cancelPreloading = false;
    int mySeqId = ++m_ctx.loadSequenceId;

    m_ctx.isLoading = true;
    m_ctx.loadStartTime = GetTickCount64();
    SetTimer(m_ctx.hWnd, LOADING_TIMER_ID, 700, nullptr);
    m_ctx.loadingFilePath = filePath;
    m_ctx.startAtEnd = startAtEnd;
    {
        CriticalSectionLock lock(m_ctx.wicMutex);
        m_ctx.wicConverter = nullptr;
        m_ctx.wicConverterOriginal = nullptr;
        m_ctx.undoStack.clear();
        m_ctx.d2dBitmap = nullptr;
        m_ctx.d2dBitmapHq = nullptr;
        m_ctx.isHqPending = false;
        m_ctx.animationD2DBitmaps.clear();
        m_ctx.isAnimated = false;
        m_ctx.animationFrameConverters.clear();
        m_ctx.animationFrameDelays.clear();
        m_ctx.currentAnimationFrame = 0;
        m_ctx.originalContainerFormat = GUID_NULL;

        // Reset view transforms for new image
        m_ctx.rotationAngle = 0;
        m_ctx.isFlippedHorizontal = false;

        m_ctx.stagedFrames.clear();
        m_ctx.stagedDelays.clear();
        m_ctx.stagedWidth = 0;
        m_ctx.stagedHeight = 0;
        m_ctx.stagedOrientation = 1;

        m_ctx.isSvg = false;
        m_ctx.svgDocument = nullptr;
        m_ctx.svgData.clear();
        m_ctx.stagedSvgData.clear();
    }
    m_ctx.currentFilePathOverride.clear();

    // Check if directory changed
    wchar_t folder[MAX_PATH] = { 0 };
    wcscpy_s(folder, MAX_PATH, filePath.c_str());
    PathRemoveFileSpecW(folder);
    if (m_ctx.currentDirectory != folder) {
        m_ctx.imageFiles.clear();
        m_ctx.currentImageIndex = -1;
        m_ctx.currentDirectory = folder;
    }

    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
    m_ctx.RunBackgroundTask([this, filePath, mySeqId]() {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            if (IsSequenceValid(mySeqId)) {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            }
            return;
        }

        const wchar_t* ext = PathFindExtensionW(filePath.c_str());
        if (ext && _wcsicmp(ext, L".svg") == 0) {
            HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER size;
                // Cap the max SVG size to 256 MB to prevent memory exhaustion.
                if (GetFileSizeEx(hFile, &size) && size.HighPart == 0 && size.LowPart > 0 && size.LowPart <= 256 * 1024 * 1024) {
                    std::vector<BYTE> data(size.LowPart);
                    DWORD bytesRead;
                    // Verify ReadFile 
                    if (ReadFile(hFile, data.data(), size.LowPart, &bytesRead, NULL) && bytesRead == size.LowPart) {
                        CriticalSectionLock lock(m_ctx.wicMutex);
                        m_ctx.stagedSvgData = std::move(data);
                        PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
                    }
                    else {
                        PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                    }
                }
                else {
                    PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                }
                CloseHandle(hFile);
            }
            else {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            }
            CoUninitialize();
            return;
        }

        ComPtr<IWICFormatConverter> preloadedConverter;
        GUID containerFormat = {};
        UINT exifOrientation = 1;

        {
            CriticalSectionLock lock(m_ctx.preloadMutex);
            if (filePath == m_ctx.preloadedNextPath && m_ctx.preloadedNextConverter) {
                preloadedConverter = m_ctx.preloadedNextConverter;
                containerFormat = m_ctx.preloadedNextFormat;
                exifOrientation = m_ctx.preloadedNextOrientation;
            }
            else if (filePath == m_ctx.preloadedPrevPath && m_ctx.preloadedPrevConverter) {
                preloadedConverter = m_ctx.preloadedPrevConverter;
                containerFormat = m_ctx.preloadedPrevFormat;
                exifOrientation = m_ctx.preloadedPrevOrientation;
            }
        }

        if (preloadedConverter) {
            UINT width = 0, height = 0;
            if (SUCCEEDED(preloadedConverter->GetSize(&width, &height))) {
                std::vector<BYTE> pixels(width * height * 4);
                if (SUCCEEDED(preloadedConverter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
                    CriticalSectionLock lock(m_ctx.wicMutex);
                    m_ctx.stagedFrames.push_back(std::move(pixels));
                    m_ctx.stagedDelays.push_back(100);
                    m_ctx.stagedWidth = width;
                    m_ctx.stagedHeight = height;
                    m_ctx.originalContainerFormat = containerFormat;
                    m_ctx.stagedOrientation = exifOrientation;

                    PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
                    CoUninitialize();
                    return;
                }
            }
        }

        ComPtr<IWICImagingFactory> localFactory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&localFactory));
        if (FAILED(hr)) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            CoUninitialize();
            return;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        hr = CreateDecoderFromStream_FullFileRead(localFactory.Get(), filePath.c_str(), &decoder, mySeqId);

        if (!IsSequenceValid(mySeqId)) { CoUninitialize(); return; }

        if (FAILED(hr)) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            CoUninitialize();
            return;
        }

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
            if (delay < 20) delay = 100;

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
                if (SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {

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
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            CoUninitialize();
            return;
        }

        {
            CriticalSectionLock lock(m_ctx.wicMutex);
            m_ctx.stagedFrames = std::move(allFramesPixels);
            m_ctx.stagedDelays = std::move(allFramesDelays);

            // Stage using the global canvas dimensions
            m_ctx.stagedWidth = canvasWidth;
            m_ctx.stagedHeight = canvasHeight;
            m_ctx.originalContainerFormat = containerFormat;
            m_ctx.stagedOrientation = exifOrientation;
        }

        PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
        CoUninitialize();
    });
}

// directory scanner that checks sequence validity
std::vector<std::wstring> ViewerApp::ScanDirectory(const std::wstring& directoryPath, int seqId) {
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
            if (m_ctx.cancelPreloading) {
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
        switch (m_ctx.currentSortCriteria) {
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

        if (m_ctx.isSortAscending) {
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

void ViewerApp::OnImageReady(bool success, int seqId) {
    if (m_ctx.loadSequenceId != seqId) return;
    if (success) {
        CriticalSectionLock lock(m_ctx.wicMutex);
        if (m_ctx.stagedFrames.empty() && m_ctx.stagedSvgData.empty()) {
            m_ctx.isLoading = false;
            return;
        }

        if (!m_ctx.stagedSvgData.empty()) {
            m_ctx.svgData = std::move(m_ctx.stagedSvgData);
            m_ctx.isSvg = true;
            m_ctx.isAnimated = false;
            m_ctx.currentOrientation = 1;
            m_ctx.originalContainerFormat = GUID_NULL;

            // This initializes m_ctx.svgDocument securely using the UI Thread's RT
            CreateDeviceResources();
        }
        else {
            bool animated = m_ctx.stagedFrames.size() > 1;
            // Disable automatic animation for TIFFs 
            if (m_ctx.originalContainerFormat == GUID_ContainerFormatTiff) {
                animated = false;
            }

            m_ctx.isAnimated = animated;
            m_ctx.animationFrameConverters.clear();
            m_ctx.animationFrameDelays.clear();

            // Determine start frame
            m_ctx.currentAnimationFrame = 0;
            if (m_ctx.startAtEnd && !animated && !m_ctx.stagedFrames.empty()) {
                m_ctx.currentAnimationFrame = static_cast<UINT>(m_ctx.stagedFrames.size() - 1);
            }

            UINT stride = m_ctx.stagedWidth * 4;

            // Reconstruct WIC 
            for (const auto& pixels : m_ctx.stagedFrames) {
                ComPtr<IWICBitmap> memoryBitmap;
                HRESULT hr = m_ctx.wicFactory->CreateBitmapFromMemory(
                    m_ctx.stagedWidth,
                    m_ctx.stagedHeight,
                    GUID_WICPixelFormat32bppPBGRA,
                    stride,
                    static_cast<UINT>(pixels.size()),
                    const_cast<BYTE*>(pixels.data()),
                    &memoryBitmap
                );
                if (SUCCEEDED(hr)) {
                    ComPtr<IWICFormatConverter> converter;
                    m_ctx.wicFactory->CreateFormatConverter(&converter);
                    converter->Initialize(memoryBitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
                    m_ctx.animationFrameConverters.push_back(converter);
                }
            }

            if (!m_ctx.animationFrameConverters.empty()) {
                // Set the primary converter based on calculated start frame
                if (m_ctx.currentAnimationFrame >= m_ctx.animationFrameConverters.size()) {
                    m_ctx.currentAnimationFrame = 0;
                }
                m_ctx.wicConverter = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
                m_ctx.wicConverterOriginal = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
                if (animated) {
                    m_ctx.animationFrameDelays = m_ctx.stagedDelays;
                    // Start animation timer
                    SetTimer(m_ctx.hWnd, ANIMATION_TIMER_ID, m_ctx.animationFrameDelays[0], nullptr);
                }
            }
        }

        // Reset flag
        m_ctx.startAtEnd = false;
        m_ctx.stagedFrames.clear();
        m_ctx.stagedDelays.clear();
        m_ctx.stagedSvgData.clear();
        m_ctx.isLoading = false;
        if (!m_ctx.isSvg) m_ctx.currentOrientation = m_ctx.stagedOrientation;

        if (m_ctx.enableFadeAnimation) {
            m_ctx.isFading = true;
            m_ctx.fadeStartTime = GetTickCount64();
        }
        else {
            m_ctx.isFading = false;
        }

        if (m_ctx.preserveView) {
            m_ctx.preserveView = false;
        }
        else if (m_ctx.defaultZoomMode == DefaultZoomMode::Actual) {
            SetActualSize();
        }
        else {
            FitImageToWindow();
        }

        SetWindowTextW(m_ctx.hWnd, m_ctx.loadingFilePath.c_str());

        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(m_ctx.loadingFilePath.c_str(), GetFileExInfoStandard, &fad)) {
            m_ctx.lastWriteTime = fad.ftLastWriteTime;
        }

        // Start background folder scan ONLY AFTER image is ready to display
        std::wstring currentFilePath = m_ctx.loadingFilePath;
        int currentSeqId = seqId;
        m_ctx.RunBackgroundTask([this, currentFilePath, currentSeqId]() {
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
                CriticalSectionLock lock(m_ctx.wicMutex);
                m_ctx.stagedImageFiles = std::move(newFiles);
                m_ctx.stagedFoundIndex = foundIndex;
            }

            PostMessage(m_ctx.hWnd, WM_APP_DIR_READY, 0, (LPARAM)currentSeqId);
            });
    }
    else {
        m_ctx.isLoading = false;
        CriticalSectionLock lock(m_ctx.wicMutex);
        m_ctx.wicConverter = nullptr;
        m_ctx.wicConverterOriginal = nullptr;
        m_ctx.undoStack.clear();
        SetWindowTextW(m_ctx.hWnd, L"Load Failed");
    }

    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::OnDirReady(int seqId) {
    if (m_ctx.loadSequenceId != seqId) return;
    {
        CriticalSectionLock lock(m_ctx.wicMutex);
        m_ctx.imageFiles = std::move(m_ctx.stagedImageFiles);
        m_ctx.currentImageIndex = m_ctx.stagedFoundIndex;
    }
    StartPreloading();
}

// Fallback  handler
void ViewerApp::FinalizeImageLoad(bool success, int foundIndex) {
    m_ctx.isLoading = false;
    KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);

    {
        CriticalSectionLock lock(m_ctx.wicMutex);
        m_ctx.d2dBitmap = nullptr;
        m_ctx.d2dBitmapHq = nullptr;
        m_ctx.isHqPending = false;
        m_ctx.animationD2DBitmaps.clear();
        m_ctx.wicConverter = nullptr;
        m_ctx.wicConverterOriginal = nullptr;
        m_ctx.undoStack.clear();
        m_ctx.stagedFrames.clear();
    }

    if (success) {
        m_ctx.currentImageIndex = foundIndex;
        SetWindowTextW(m_ctx.hWnd, m_ctx.loadingFilePath.c_str());
        CenterImage(true);
    }
    else {
        m_ctx.currentImageIndex = -1;
        SetWindowTextW(m_ctx.hWnd, L"Minimal Image Viewer");
        CenterImage(true);
    }
}

void ViewerApp::CleanupLoadingThread() {
    m_ctx.cancelPreloading = true;
    CleanupPreloadingThreads();
    KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
}

void ViewerApp::CleanupPreloadingThreads() {
    m_ctx.cancelPreloading = true;
    CriticalSectionLock lock(m_ctx.preloadMutex);
    m_ctx.preloadedNextConverter = nullptr;
    m_ctx.preloadedPrevConverter = nullptr;
    m_ctx.preloadedNextPath.clear();
    m_ctx.preloadedPrevPath.clear();
}

void ViewerApp::StartPreloading() {
    CleanupPreloadingThreads();
    m_ctx.cancelPreloading = false;

    if (m_ctx.imageFiles.size() < 2 || m_ctx.currentImageIndex < 0) return;

    int nextIdx = (m_ctx.currentImageIndex + 1) % static_cast<int>(m_ctx.imageFiles.size());
    int prevIdx = (m_ctx.currentImageIndex - 1 + static_cast<int>(m_ctx.imageFiles.size())) % static_cast<int>(m_ctx.imageFiles.size());

    std::wstring nextPath = m_ctx.imageFiles[nextIdx];
    std::wstring prevPath = m_ctx.imageFiles[prevIdx];

    auto preloadTask = [this](std::wstring path, ComPtr<IWICFormatConverter>* pConverter, GUID* pFormat, UINT* pOrientation, std::wstring* pPath) {
        // Skip GIFs and TIFFs to preserve animation frame extraction during a normal load
        const wchar_t* ext = PathFindExtensionW(path.c_str());
        if (ext && (_wcsicmp(ext, L".gif") == 0 || _wcsicmp(ext, L".tiff") == 0 || _wcsicmp(ext, L".tif") == 0)) return;

        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
        ComPtr<IWICImagingFactory> factory;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            ComPtr<IWICBitmapDecoder> decoder;
            if (SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) {
                ComPtr<IWICBitmapFrameDecode> frame;
                if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                    ComPtr<IWICFormatConverter> converter;
                    if (SUCCEEDED(factory->CreateFormatConverter(&converter))) {
                        if (SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                            // Force immediate decode into RAM
                            ComPtr<IWICBitmap> memBmp;
                            if (SUCCEEDED(factory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &memBmp))) {
                                ComPtr<IWICFormatConverter> finalConverter;
                                if (SUCCEEDED(factory->CreateFormatConverter(&finalConverter))) {
                                    if (SUCCEEDED(finalConverter->Initialize(memBmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                                        UINT orientation = 1;
                                        ComPtr<IWICMetadataQueryReader> metadataReader;
                                        if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                                            PROPVARIANT propValue;
                                            PropVariantInit(&propValue);
                                            if (SUCCEEDED(metadataReader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &propValue)) && propValue.vt == VT_UI2) {
                                                orientation = propValue.uiVal;
                                            }
                                            PropVariantClear(&propValue);
                                        }

                                        CriticalSectionLock lock(m_ctx.preloadMutex);
                                        if (!m_ctx.cancelPreloading) {
                                            *pConverter = finalConverter;
                                            if (pFormat) decoder->GetContainerFormat(pFormat);
                                            if (pOrientation) *pOrientation = orientation;
                                            if (pPath) *pPath = path;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        CoUninitialize();
        };

    m_ctx.RunBackgroundTask([this, preloadTask, nextPath]() {
        preloadTask(nextPath, std::addressof(m_ctx.preloadedNextConverter), &m_ctx.preloadedNextFormat, &m_ctx.preloadedNextOrientation, &m_ctx.preloadedNextPath);
        });

    if (nextIdx != prevIdx) {
        m_ctx.RunBackgroundTask([this, preloadTask, prevPath]() {
            preloadTask(prevPath, std::addressof(m_ctx.preloadedPrevConverter), &m_ctx.preloadedPrevFormat, &m_ctx.preloadedPrevOrientation, &m_ctx.preloadedPrevPath);
            });
    }
}