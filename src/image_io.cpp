#include "viewer.h"
#include <memory>
#include <algorithm>
#include <shlwapi.h> 
#include <filesystem>
#include <propkey.h>



static bool IsImageFile(const wchar_t* filePath) {
    return PathMatchSpecW(filePath, L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2;*.svg") == TRUE;
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
        std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);

        // Keep current image resources alive for flicker-free loading    
        m_ctx.undoStack.clear();
        m_ctx.stagedFrames.clear();
        m_ctx.stagedDelays.clear();
        m_ctx.stagedWidth = 0;
        m_ctx.stagedHeight = 0;
        m_ctx.stagedOrientation = 1;
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
        wil::unique_couninitialize_call cleanupCOM; // Automatically uninitializes COM on exit

        // Read entire file to avoid locking
        wil::unique_hfile hFile(CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
        if (!hFile) { PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId); return; }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(hFile.get(), &size) || size.HighPart != 0 || size.LowPart == 0 || size.LowPart > 1024 * 1024 * 1024) { // Cap at 1GB for safety
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId); return;
        }

        std::vector<BYTE> rawData(size.LowPart);
        DWORD bytesRead;
        if (!ReadFile(hFile.get(), rawData.data(), size.LowPart, &bytesRead, NULL) || bytesRead != size.LowPart) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId); return;
        }
        hFile.reset();
        // instant unlock

        const wchar_t* ext = PathFindExtensionW(filePath.c_str());
        if (ext && _wcsicmp(ext, L".svg") == 0) {
            std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
            m_ctx.stagedSvgData = std::move(rawData);
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
            return;
        }

        ComPtr<IWICFormatConverter> preloadedConverter;
        GUID containerFormat = {};
        UINT exifOrientation = 1;
        {
            std::lock_guard<std::recursive_mutex> lock(m_ctx.preloadMutex);
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
                    std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
                    m_ctx.stagedFrames.push_back(std::move(pixels));
                    m_ctx.stagedDelays.push_back(100);
                    m_ctx.stagedWidth = width;
                    m_ctx.stagedHeight = height;
                    m_ctx.originalContainerFormat = containerFormat;
                    m_ctx.stagedOrientation = exifOrientation;

                    PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
                    return;
                }
            }
        }

        // Fetch EXIF Orientation natively via IPropertyStore
        ComPtr<IPropertyStore> pStore;
        if (SUCCEEDED(SHGetPropertyStoreFromParsingName(filePath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&pStore)))) {
            wil::unique_prop_variant propValue;
            if (SUCCEEDED(pStore->GetValue(PKEY_Photo_Orientation, &propValue))) {
                if (propValue.vt == VT_UI2) {
                    exifOrientation = propValue.uiVal;
                }
            }
        }

        ComPtr<IWICImagingFactory> localFactory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&localFactory));
        if (FAILED(hr)) { PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId); return; }

        ComPtr<IWICStream> stream;
        localFactory->CreateStream(&stream);
        stream->InitializeFromMemory(rawData.data(), static_cast<DWORD>(rawData.size()));

        ComPtr<IWICBitmapDecoder> decoder;
        hr = localFactory->CreateDecoderFromStream(stream.Get(), NULL, WICDecodeMetadataCacheOnLoad, &decoder);

        if (!IsSequenceValid(mySeqId)) { return; }

        if (FAILED(hr)) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            return;
        }

        decoder->GetContainerFormat(&containerFormat);

        UINT frameCount = 0;
        decoder->GetFrameCount(&frameCount);
        if (frameCount == 0) frameCount = 1;

        // Fast static image loading
        if (frameCount == 1 && containerFormat != GUID_ContainerFormatGif) {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                UINT frameWidth = 0, frameHeight = 0;
                frame->GetSize(&frameWidth, &frameHeight);

                UINT maxDim = 3840; // 4K max base to save RAM
                ComPtr<IWICBitmapSource> sourceToCache = frame;
                bool downscaled = false;
                float ratio = 1.0f;

                if (frameWidth > maxDim || frameHeight > maxDim) {
                    downscaled = true;
                    ratio = std::min(static_cast<float>(maxDim) / frameWidth, static_cast<float>(maxDim) / frameHeight);
                    UINT newW = static_cast<UINT>(frameWidth * ratio);
                    UINT newH = static_cast<UINT>(frameHeight * ratio);
                    bool nativeScaled = false;
                    ComPtr<IWICBitmapSourceTransform> sourceTransform;

                    // Native codec rapid downscaling 
                    if (SUCCEEDED(frame.As(&sourceTransform))) {
                        UINT actualWidth = newW, actualHeight = newH;
                        if (SUCCEEDED(sourceTransform->GetClosestSize(&actualWidth, &actualHeight))) {
                            // Only proceed if the codec actually gave us an optimized size smaller than the original
                            if (actualWidth < frameWidth && actualHeight < frameHeight) {
                                WICPixelFormatGUID closestFormat = GUID_WICPixelFormat32bppPBGRA;
                                if (SUCCEEDED(sourceTransform->GetClosestPixelFormat(&closestFormat))) {
                                    ComPtr<IWICBitmap> fastBitmap;
                                    // Allocate only the memory needed for the 4K frame
                                    if (SUCCEEDED(localFactory->CreateBitmap(actualWidth, actualHeight, closestFormat, WICBitmapCacheOnLoad, &fastBitmap))) {
                                        WICRect rc = { 0, 0, (INT)actualWidth, (INT)actualHeight };
                                        ComPtr<IWICBitmapLock> lock;
                                        if (SUCCEEDED(fastBitmap->Lock(&rc, WICBitmapLockWrite, &lock))) {
                                            UINT cbStride = 0, cbBufferSize = 0;
                                            BYTE* pbBuffer = nullptr;
                                            lock->GetStride(&cbStride);
                                            lock->GetDataPointer(&cbBufferSize, &pbBuffer);

                                            // Only decode the pixels needed for the smaller size
                                            if (SUCCEEDED(sourceTransform->CopyPixels(nullptr, actualWidth, actualHeight, &closestFormat, WICBitmapTransformRotate0, cbStride, cbBufferSize, pbBuffer))) {
                                                sourceToCache = fastBitmap;
                                                nativeScaled = true;
                                                // Adjust ratio to match exact size 
                                                ratio = std::min(static_cast<float>(actualWidth) / frameWidth, static_cast<float>(actualHeight) / frameHeight);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Fallback to CPU scaler 
                    if (!nativeScaled) {
                        ComPtr<IWICBitmapScaler> scaler;
                        if (SUCCEEDED(localFactory->CreateBitmapScaler(&scaler))) {
                            // Scale directly from raw frame before conversion
                            if (SUCCEEDED(scaler->Initialize(frame.Get(), newW, newH, WICBitmapInterpolationModeFant))) {
                                sourceToCache = scaler;
                            }
                        }
                    }
                }

                if (ComPtr<IWICFormatConverter> finalConverter = ConvertToFormat(localFactory.Get(), sourceToCache.Get())) {
                    // D2D decodes straight to gpu vram
                    ComPtr<IWICFormatConverter> d2dConverter = finalConverter;
                    std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
                    m_ctx.stagedStaticConverter = d2dConverter;
                    m_ctx.stagedRawFileData = std::move(rawData); // Transfer memory ownership to context
                    m_ctx.stagedWicStream = stream;
                    // Keep stream alive
                    m_ctx.stagedWidth = frameWidth;
                    m_ctx.stagedHeight = frameHeight;
                    m_ctx.originalContainerFormat = containerFormat;
                    m_ctx.stagedOrientation = exifOrientation;
                    m_ctx.isDownscaled = downscaled;
                    m_ctx.downscaleRatio = ratio;

                    PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
                    return;
                }
            }
        }

        std::vector<std::vector<BYTE>> allFramesPixels;
        std::vector<UINT> allFramesDelays;
        UINT canvasWidth = 0, canvasHeight = 0;

        // Retrieve global logical screen descriptor 
        ComPtr<IWICMetadataQueryReader> decoderMetadata;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(&decoderMetadata))) {
            wil::unique_prop_variant propValue;
            if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Width", &propValue))) {
                if (propValue.vt == VT_UI2) canvasWidth = propValue.uiVal;
            }
            propValue.reset(); // clear before reuse
            if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Height", &propValue))) {
                if (propValue.vt == VT_UI2) canvasHeight = propValue.uiVal;
            }
        }

        std::vector<BYTE> compositeBuffer;
        std::vector<BYTE> previousCompositeBuffer;

        for (UINT i = 0; i < frameCount; ++i) {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(i, &frame))) continue;

            UINT frameWidth = 0, frameHeight = 0;
            frame->GetSize(&frameWidth, &frameHeight);
            // Fallback if dimensions arent found
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
                wil::unique_prop_variant propValue;
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Delay", &propValue))) {
                    if (propValue.vt == VT_UI2) delay = propValue.uiVal * 10;
                }
                propValue.reset(); // clear before reuse
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Disposal", &propValue))) {
                    if (propValue.vt == VT_UI1) disposal = propValue.bVal;
                }
                propValue.reset();
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/imgdesc/Left", &propValue))) {
                    if (propValue.vt == VT_UI2) left = propValue.uiVal;
                }
                propValue.reset();
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/imgdesc/Top", &propValue))) {
                    if (propValue.vt == VT_UI2) top = propValue.uiVal;
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

            if (ComPtr<IWICFormatConverter> converter = ConvertToFormat(localFactory.Get(), frame.Get())) {
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

            // Apply disposal method for next frame
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

            if (!IsSequenceValid(mySeqId)) { return; }
        }

        if (allFramesPixels.empty()) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            return;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
            m_ctx.stagedFrames = std::move(allFramesPixels);
            m_ctx.stagedDelays = std::move(allFramesDelays);

            // Stage using the global canvas dimensions
            m_ctx.stagedWidth = canvasWidth;
            m_ctx.stagedHeight = canvasHeight;
            m_ctx.originalContainerFormat = containerFormat;
            m_ctx.stagedOrientation = exifOrientation;
        }

        PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
        });
}

// directory scanner 
std::vector<std::wstring> ViewerApp::ScanDirectory(const std::wstring& directoryPath, int seqId) {
    namespace fs = std::filesystem;
    struct FileInfo {
        std::wstring path;
        fs::file_time_type writeTime{};
        uintmax_t fileSize = 0;
    };

    std::vector<FileInfo> foundFiles;
    foundFiles.reserve(100);

    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(directoryPath, fs::directory_options::skip_permission_denied, ec)) {
        if (!IsSequenceValid(seqId)) {
            return {};
        }
        if (m_ctx.cancelPreloading) {
            return {};
        }

        // Skip directories/special files
        if (entry.is_regular_file(ec)) {
            std::wstring fullPath = entry.path().wstring();

            if (IsImageFile(fullPath.c_str())) {
                FileInfo info;
                info.path = std::move(fullPath);
                info.writeTime = entry.last_write_time(ec);
                info.fileSize = entry.file_size(ec);
                foundFiles.push_back(info);
            }
        }
    }

    if (!IsSequenceValid(seqId)) return {};

    std::ranges::sort(foundFiles, [&](const FileInfo& a, const FileInfo& b) {
        std::strong_ordering cmp = std::strong_ordering::equal;

        switch (m_ctx.currentSortCriteria) {
        case SortCriteria::ByDateModified:
            cmp = a.writeTime <=> b.writeTime;
            break;
        case SortCriteria::ByFileSize:
            cmp = a.fileSize <=> b.fileSize;
            break;
        case SortCriteria::ByName:
        default:
            // StrCmpLogicalW returns an int
            cmp = StrCmpLogicalW(a.path.c_str(), b.path.c_str()) <=> 0;
            break;
        }

        return m_ctx.isSortAscending ? (cmp < 0) : (cmp > 0);
        });

    std::vector<std::wstring> result;
    result.reserve(foundFiles.size());
    for (auto& info : foundFiles) {
        result.push_back(std::move(info.path));
    }
    return result;
}

void ViewerApp::OnImageReady(bool success, int seqId) {
    if (m_ctx.loadSequenceId != seqId) return;

    if (success) {
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);

        if (!m_ctx.stagedStaticConverter && m_ctx.stagedFrames.empty() && m_ctx.stagedSvgData.empty()) {
            m_ctx.isLoading = false;
            return;
        }

        // Clear the old image state before displaying new
        KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
        m_ctx.d2dBitmap = nullptr;
        m_ctx.highResImageSource = nullptr;
        m_ctx.animationD2DBitmaps.clear();
        m_ctx.animationFrameConverters.clear();
        m_ctx.animationFrameDelays.clear();
        m_ctx.wicConverter = nullptr;
        m_ctx.wicConverterOriginal = nullptr;
        m_ctx.svgDocument = nullptr;
        m_ctx.svgData.clear();
        m_ctx.isSvg = false;
        m_ctx.isAnimated = false;
        m_ctx.rotationAngle = 0;
        m_ctx.isFlippedHorizontal = false;

        if (m_ctx.stagedStaticConverter) {
            m_ctx.wicConverter = m_ctx.stagedStaticConverter;
            m_ctx.wicConverterOriginal = m_ctx.stagedStaticConverter;
            m_ctx.rawFileData = std::move(m_ctx.stagedRawFileData);
            m_ctx.wicStream = m_ctx.stagedWicStream;
            m_ctx.originalWidth = m_ctx.stagedWidth;
            m_ctx.originalHeight = m_ctx.stagedHeight;
            m_ctx.stagedStaticConverter = nullptr;
            m_ctx.stagedWicStream = nullptr;
            m_ctx.isAnimated = false;
        }
        else if (!m_ctx.stagedSvgData.empty()) {
            m_ctx.svgData = std::move(m_ctx.stagedSvgData);
            m_ctx.isSvg = true;
            m_ctx.isAnimated = false;
            m_ctx.currentOrientation = 1;
            m_ctx.originalContainerFormat = GUID_NULL;
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
                    if (ComPtr<IWICFormatConverter> converter = ConvertToFormat(m_ctx.wicFactory.Get(), memoryBitmap.Get())) {
                        m_ctx.animationFrameConverters.push_back(converter);
                    }
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

        if (!m_ctx.isSvg) {
            m_ctx.currentOrientation = m_ctx.stagedOrientation;
            // Apply EXIF auto-rotation 
            switch (m_ctx.currentOrientation) {
            case 2: m_ctx.isFlippedHorizontal = true; break;
            case 3: m_ctx.rotationAngle = 180; break;
            case 4: m_ctx.rotationAngle = 180; m_ctx.isFlippedHorizontal = true; break;
            case 5: m_ctx.rotationAngle = 270; m_ctx.isFlippedHorizontal = true; break;
            case 6: m_ctx.rotationAngle = 90; break;
            case 7: m_ctx.rotationAngle = 90; m_ctx.isFlippedHorizontal = true; break;
            case 8: m_ctx.rotationAngle = 270; break;
            }
        }

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

        UpdateWindowTitle();

        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(m_ctx.loadingFilePath.c_str(), GetFileExInfoStandard, &fad)) {
            m_ctx.lastWriteTime = fad.ftLastWriteTime;
        }

        // Start background folder scan ONLY AFTER image is ready to display
        bool needsScan = false;
        {
           std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
            needsScan = m_ctx.imageFiles.empty();
        }

        if (needsScan) {
            std::wstring currentFilePath = m_ctx.loadingFilePath;
            int currentSeqId = seqId;
            m_ctx.RunBackgroundTask([this, currentFilePath, currentSeqId]() {
                wchar_t folder[MAX_PATH] = { 0 };
                wcscpy_s(folder, MAX_PATH, currentFilePath.c_str());
                PathRemoveFileSpecW(folder);

                std::vector<std::wstring> newFiles = ScanDirectory(folder, currentSeqId);

                if (!IsSequenceValid(currentSeqId)) return;

                int foundIndex = -1;
                auto it = std::ranges::find_if(newFiles,
                    [&](const std::wstring& s) { return _wcsicmp(s.c_str(), currentFilePath.c_str()) == 0; }
                );
                foundIndex = (it != newFiles.end()) ? static_cast<int>(std::distance(newFiles.begin(), it)) : -1;

                {
                   std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
                    m_ctx.stagedImageFiles = std::move(newFiles);
                    m_ctx.stagedFoundIndex = foundIndex;
                }

                PostMessage(m_ctx.hWnd, WM_APP_DIR_READY, 0, (LPARAM)currentSeqId);
                });
        }
        else {
            // Directory is already cached, jump straight to preloading next/prev
            StartPreloading();
        }
    }
    else {
        m_ctx.isLoading = false;
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
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
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
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
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
        m_ctx.d2dBitmap = nullptr;
        m_ctx.animationD2DBitmaps.clear();
        m_ctx.wicConverter = nullptr;
        m_ctx.wicConverterOriginal = nullptr;
        m_ctx.undoStack.clear();
        m_ctx.stagedFrames.clear();
    }

    if (success) {
        m_ctx.currentImageIndex = foundIndex;
        UpdateWindowTitle();
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
   std::lock_guard<std::recursive_mutex> lock(m_ctx.preloadMutex);
    m_ctx.preloadedNextConverter = nullptr;
    m_ctx.preloadedPrevConverter = nullptr;
    m_ctx.preloadedNextPath.clear();
    m_ctx.preloadedPrevPath.clear();
}

void ViewerApp::StartPreloading() {
    CleanupPreloadingThreads();
    m_ctx.cancelPreloading = false;
}
