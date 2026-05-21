#include "viewer.h"
#include <memory>
#include <algorithm>
#include <shlwapi.h> 
#include <filesystem>
#include <propkey.h>

#pragma warning(push)
#pragma warning(disable : 4996) // Suppress 'fopen' unsafe error
#pragma warning(disable : 4267) // Suppress size_t to int conversion warning

#define QOI_IMPLEMENTATION
#include "qoi.h"

// stb_image is used only for Radiance HDR (.hdr) float decoding

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#pragma warning(pop)



static bool IsImageFile(const wchar_t* filePath) {
    return PathMatchSpecW(filePath,
        L"*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;"
        L"*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2;"
        L"*.svg;*.qoi;*.hdr;"
        L"*.tga;*.psd;*.ppm;*.pgm;*.pbm;*.pnm;*.pic") == TRUE;
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
    m_ctx.isOsdCacheValid = false;
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
        // Abort obsolete tasks on thread wake up
        if (!IsSequenceValid(mySeqId)) return;

        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            if (IsSequenceValid(mySeqId)) {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            }
            return;
        }

        wil::unique_couninitialize_call cleanupCOM; // Automatically uninitializes COM on exit

        // Check before touching the disk
        if (!IsSequenceValid(mySeqId)) return;

        // Read entire file to avoid locking
        wil::unique_hfile hFile(CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
        if (!hFile) { PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId); return; }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(hFile.get(), &size) || size.HighPart != 0 || size.LowPart == 0 || size.LowPart > 1024 * 1024 * 1024) { // Cap at 1GB for safety
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId); return;
        }

        // Check before large memory allocation and blocking read
        if (!IsSequenceValid(mySeqId)) return;

        FastByteBuffer rawData;
        rawData.allocate(size.LowPart);

        DWORD bytesRead;
        if (!ReadFile(hFile.get(), rawData.data(), size.LowPart, &bytesRead, NULL) || bytesRead != size.LowPart) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            return;
        }
        hFile.reset();
        // instant unlock

        // Final check 
        if (!IsSequenceValid(mySeqId)) return;

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
        if (FAILED(hr)) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId); return;
        }

        if (ext && _wcsicmp(ext, L".qoi") == 0) {
            qoi_desc desc;
            void* pixels = qoi_decode(rawData.data(), static_cast<int>(rawData.size()), &desc, 4);
            if (pixels) {
                ComPtr<IWICBitmap> qoiBmp;
                if (SUCCEEDED(localFactory->CreateBitmap(desc.width, desc.height, GUID_WICPixelFormat32bppRGBA, WICBitmapCacheOnLoad, &qoiBmp))) {
                    WICRect rc = { 0, 0, (INT)desc.width, (INT)desc.height };
                    ComPtr<IWICBitmapLock> lock;
                    if (SUCCEEDED(qoiBmp->Lock(&rc, WICBitmapLockWrite, &lock))) {
                        UINT cbStride = 0, cbBufferSize = 0;
                        BYTE* pbBuffer = nullptr;
                        lock->GetStride(&cbStride);
                        lock->GetDataPointer(&cbBufferSize, &pbBuffer);
                        if (pbBuffer) {
                            memcpy(pbBuffer, pixels, desc.width * desc.height * 4);
                        }
                    }

                    ComPtr<IWICBitmapSource> sourceToCache = qoiBmp;
                    bool downscaled = false;
                    float ratio = 1.0f;
                    UINT maxDim = 3840; // Hooking into scaling constant

                    // Route directly into existing CPU scaler 
                    if (desc.width > maxDim || desc.height > maxDim) {
                        downscaled = true;
                        ratio = std::min(static_cast<float>(maxDim) / desc.width, static_cast<float>(maxDim) / desc.height);
                        UINT newW = static_cast<UINT>(desc.width * ratio);
                        UINT newH = static_cast<UINT>(desc.height * ratio);

                        ComPtr<IWICBitmapScaler> scaler;
                        if (SUCCEEDED(localFactory->CreateBitmapScaler(&scaler))) {
                            if (SUCCEEDED(scaler->Initialize(sourceToCache.Get(), newW, newH, WICBitmapInterpolationModeFant))) {
                                sourceToCache = scaler;
                            }
                        }
                    }

                    if (ComPtr<IWICFormatConverter> finalConverter = ConvertToFormat(localFactory.Get(), sourceToCache.Get())) {
                        std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
                        m_ctx.stagedStaticConverter = finalConverter;
                        m_ctx.stagedRawFileData = std::move(rawData);
                        m_ctx.stagedWidth = desc.width;
                        m_ctx.stagedHeight = desc.height;
                        m_ctx.originalContainerFormat = GUID_NULL;
                        m_ctx.stagedOrientation = 1;
                        m_ctx.isDownscaled = downscaled;
                        m_ctx.downscaleRatio = ratio;

                        free(pixels); // Clean up QOI memory
                        PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
                        return;
                    }
                }
                free(pixels);
            }
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            return;
        }

        if (ext && _wcsicmp(ext, L".hdr") == 0) {
            int hdrW = 0;
            int hdrH = 0;
            int hdrComp = 0;

            if (!stbi_info_from_memory(
                rawData.data(),
                static_cast<int>(rawData.size()),
                &hdrW,
                &hdrH,
                &hdrComp))
            {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            if (hdrW <= 0 || hdrH <= 0) {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            // HDR via stb_image requires a full float decode in memory.
            // Keep this cap isolated to HDR so it does not disturb the existing WIC massive-image path.
            constexpr uint64_t HDR_MAX_PIXELS = 40ull * 1000ull * 1000ull;
            uint64_t hdrPixelsCount = static_cast<uint64_t>(hdrW) * static_cast<uint64_t>(hdrH);

            if (hdrPixelsCount > HDR_MAX_PIXELS) {
                MessageBoxW(
                    m_ctx.hWnd,
                    L"This HDR image is too large for the stb_image HDR loader safety limit.",
                    L"HDR Image Too Large",
                    MB_ICONWARNING
                );
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            float* hdrPixels = stbi_loadf_from_memory(
                rawData.data(),
                static_cast<int>(rawData.size()),
                &hdrW,
                &hdrH,
                &hdrComp,
                3
            );

            if (!hdrPixels) {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            ComPtr<IWICBitmap> hdrBmp;
            HRESULT hdrHr = localFactory->CreateBitmap(
                static_cast<UINT>(hdrW),
                static_cast<UINT>(hdrH),
                GUID_WICPixelFormat32bppPBGRA,
                WICBitmapCacheOnLoad,
                &hdrBmp
            );

            if (FAILED(hdrHr)) {
                stbi_image_free(hdrPixels);
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            WICRect hdrRc = { 0, 0, hdrW, hdrH };
            ComPtr<IWICBitmapLock> hdrLock;

            hdrHr = hdrBmp->Lock(&hdrRc, WICBitmapLockWrite, &hdrLock);
            if (FAILED(hdrHr)) {
                stbi_image_free(hdrPixels);
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            UINT cbStride = 0;
            UINT cbBufferSize = 0;
            BYTE* pbBuffer = nullptr;

            hdrLock->GetStride(&cbStride);
            hdrLock->GetDataPointer(&cbBufferSize, &pbBuffer);

            if (!pbBuffer) {
                stbi_image_free(hdrPixels);
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            auto FloatHdrToByte = [](float v) -> BYTE {
                if (!std::isfinite(v) || v <= 0.0f) {
                    return 0;
                }

            // Simple Reinhard tone map, then display gamma.
            float mapped = v / (1.0f + v);
            float gammaCorrected = powf(mapped, 1.0f / 2.2f);

            int out = static_cast<int>(gammaCorrected * 255.0f + 0.5f);
            if (out < 0) return 0;
            if (out > 255) return 255;
            return static_cast<BYTE>(out);
            };

            for (int y = 0; y < hdrH; ++y) {
                const float* srcRow = hdrPixels + (static_cast<size_t>(y) * static_cast<size_t>(hdrW) * 3u);
                BYTE* dstRow = pbBuffer + (static_cast<size_t>(y) * cbStride);

                for (int x = 0; x < hdrW; ++x) {
                    const float r = srcRow[x * 3 + 0];
                    const float g = srcRow[x * 3 + 1];
                    const float b = srcRow[x * 3 + 2];

                    BYTE* dst = dstRow + (static_cast<size_t>(x) * 4u);

                    // Alpha is opaque, so premultiplied RGB == normal RGB.
                    dst[0] = FloatHdrToByte(b);
                    dst[1] = FloatHdrToByte(g);
                    dst[2] = FloatHdrToByte(r);
                    dst[3] = 255;
                }
            }

            stbi_image_free(hdrPixels);

            ComPtr<IWICBitmapSource> sourceToCache = hdrBmp;
            bool downscaled = false;
            float ratio = 1.0f;
            UINT maxDim = 3840; 

            if (static_cast<UINT>(hdrW) > maxDim || static_cast<UINT>(hdrH) > maxDim) {
                downscaled = true;
                ratio = std::min(
                    static_cast<float>(maxDim) / static_cast<float>(hdrW),
                    static_cast<float>(maxDim) / static_cast<float>(hdrH)
                );

                UINT newW = static_cast<UINT>(hdrW * ratio);
                UINT newH = static_cast<UINT>(hdrH * ratio);

                ComPtr<IWICBitmapScaler> scaler;
                if (SUCCEEDED(localFactory->CreateBitmapScaler(&scaler))) {
                    if (SUCCEEDED(scaler->Initialize(sourceToCache.Get(), newW, newH, WICBitmapInterpolationModeFant))) {
                        sourceToCache = scaler;
                    }
                }
            }

            if (ComPtr<IWICFormatConverter> finalConverter = ConvertToFormat(localFactory.Get(), sourceToCache.Get())) {
                std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);

                m_ctx.stagedStaticConverter = finalConverter;

                // Don't add stb to high-res path
                m_ctx.stagedRawFileData.clear();
                m_ctx.stagedWicStream = nullptr;

                m_ctx.stagedWidth = static_cast<UINT>(hdrW);
                m_ctx.stagedHeight = static_cast<UINT>(hdrH);
                m_ctx.originalContainerFormat = GUID_NULL;
                m_ctx.stagedOrientation = 1;
                m_ctx.isDownscaled = downscaled;
                m_ctx.downscaleRatio = ratio;

                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
                return;
            }

            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            return;
        }

        // STB fallback for non-WIC formats 
        if (ext &&
            (_wcsicmp(ext, L".tga") == 0 ||
                _wcsicmp(ext, L".psd") == 0 ||
                _wcsicmp(ext, L".ppm") == 0 ||
                _wcsicmp(ext, L".pgm") == 0 ||
                _wcsicmp(ext, L".pbm") == 0 ||
                _wcsicmp(ext, L".pnm") == 0 ||
                _wcsicmp(ext, L".pic") == 0))
        {
            int w = 0, h = 0, comp = 0;

            if (!stbi_info_from_memory(
                rawData.data(),
                static_cast<int>(rawData.size()),
                &w, &h, &comp))
            {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            if (w <= 0 || h <= 0)
            {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            unsigned char* pixels = stbi_load_from_memory(
                rawData.data(),
                static_cast<int>(rawData.size()),
                &w, &h, &comp,
                4
            );

            if (!pixels)
            {
                PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
                return;
            }

            ComPtr<IWICBitmap> bmp;

            if (SUCCEEDED(localFactory->CreateBitmap(
                (UINT)w, (UINT)h,
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapCacheOnLoad,
                &bmp)))
            {
                WICRect rc = { 0, 0, w, h };

                ComPtr<IWICBitmapLock> lock;
                if (SUCCEEDED(bmp->Lock(&rc, WICBitmapLockWrite, &lock)))
                {
                    UINT stride = 0, size = 0;
                    BYTE* dest = nullptr;

                    lock->GetStride(&stride);
                    lock->GetDataPointer(&size, &dest);

                    if (dest)
                    {
                        memcpy(dest, pixels, (size_t)w * h * 4);
                    }
                }

                ComPtr<IWICBitmapSource> sourceToCache = bmp;

                bool downscaled = false;
                float ratio = 1.0f;
                UINT maxDim = 3840;

                if ((UINT)w > maxDim || (UINT)h > maxDim)
                {
                    downscaled = true;
                    ratio = std::min((float)maxDim / w, (float)maxDim / h);

                    UINT newW = (UINT)(w * ratio);
                    UINT newH = (UINT)(h * ratio);

                    ComPtr<IWICBitmapScaler> scaler;
                    if (SUCCEEDED(localFactory->CreateBitmapScaler(&scaler)) &&
                        SUCCEEDED(scaler->Initialize(sourceToCache.Get(), newW, newH, WICBitmapInterpolationModeFant)))
                    {
                        sourceToCache = scaler;
                    }
                }

                if (ComPtr<IWICFormatConverter> finalConverter =
                    ConvertToFormat(localFactory.Get(), sourceToCache.Get()))
                {
                    std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);

                    m_ctx.stagedStaticConverter = finalConverter;
                    m_ctx.stagedRawFileData = std::move(rawData);
                    m_ctx.stagedWidth = (UINT)w;
                    m_ctx.stagedHeight = (UINT)h;
                    m_ctx.originalContainerFormat = GUID_NULL;
                    m_ctx.stagedOrientation = 1;
                    m_ctx.isDownscaled = downscaled;
                    m_ctx.downscaleRatio = ratio;

                    stbi_image_free(pixels);

                    PostMessage(m_ctx.hWnd, WM_APP_IMAGE_READY, 1, (LPARAM)mySeqId);
                    return;
                }
            }

            stbi_image_free(pixels);
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            return;
        }


        ComPtr<IWICStream> stream;
        localFactory->CreateStream(&stream);
        stream->InitializeFromMemory(rawData.data(), static_cast<DWORD>(rawData.size()));

        ComPtr<IWICBitmapDecoder> decoder;
        hr = localFactory->CreateDecoderFromStream(stream.Get(), NULL, WICDecodeMetadataCacheOnLoad, &decoder);

        if (!IsSequenceValid(mySeqId)) {
            return;
        }

        if (FAILED(hr)) {
            // Intercept HEIC/AVIF failures and prompt to install the lightweight native codec
            if (ext && (_wcsicmp(ext, L".heic") == 0 || _wcsicmp(ext, L".heif") == 0 || _wcsicmp(ext, L".avif") == 0)) {
                if (MessageBoxW(m_ctx.hWnd,
                    L"To view HEIC and AVIF images natively, you need the free 'HEIF Image Extensions' from the Microsoft Store.\n\nWould you like to open the Store page?",
                    L"Missing Image Codec", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                    // Deep link directly to the Microsoft Store page for the official HEIF extension
                    ShellExecuteW(nullptr, L"open", L"ms-windows-store://pdp/?ProductId=9PMMSR1CGPWG", nullptr, nullptr, SW_SHOW);
                }
            }
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

                UINT maxDim = 3840; // 4K max base 
                ComPtr<IWICBitmapSource> sourceToCache = frame;
                bool downscaled = false;
                float ratio = 1.0f;
                bool loadedPreview = false;

                // Extract the embedded preview for raw/tiff
                ComPtr<IWICBitmapSource> preview;
                if (SUCCEEDED(decoder->GetPreview(&preview))) {
                    UINT previewW = 0, previewH = 0;
                    if (SUCCEEDED(preview->GetSize(&previewW, &previewH)) && previewW > 0 && previewH > 0) {
                        sourceToCache = preview;
                        loadedPreview = true;

                        if (previewW < frameWidth || previewH < frameHeight) {
                            downscaled = true;
                            // Deep zoom when past preview's resolution
                            ratio = std::min(static_cast<float>(previewW) / frameWidth, static_cast<float>(previewH) / frameHeight);
                        }

                        // Prevent memory spikes for massive previews
                        if (previewW > maxDim || previewH > maxDim) {
                            float prevRatio = std::min(static_cast<float>(maxDim) / previewW, static_cast<float>(maxDim) / previewH);
                            UINT newW = static_cast<UINT>(previewW * prevRatio);
                            UINT newH = static_cast<UINT>(previewH * prevRatio);

                            ComPtr<IWICBitmapScaler> scaler;
                            if (SUCCEEDED(localFactory->CreateBitmapScaler(&scaler))) {
                                if (SUCCEEDED(scaler->Initialize(preview.Get(), newW, newH, WICBitmapInterpolationModeFant))) {
                                    sourceToCache = scaler;
                                    downscaled = true;
                                    ratio = std::min(static_cast<float>(newW) / frameWidth, static_cast<float>(newH) / frameHeight);
                                }
                            }
                        }
                    }
                }

                // Standard load if no preview 
                if (!loadedPreview && (frameWidth > maxDim || frameHeight > maxDim)) {
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
                            // Only proceed if codec smaller than original
                            if (actualWidth < frameWidth && actualHeight < frameHeight) {
                                WICPixelFormatGUID closestFormat = GUID_WICPixelFormat32bppPBGRA;
                                if (SUCCEEDED(sourceTransform->GetClosestPixelFormat(&closestFormat))) {
                                    ComPtr<IWICBitmap> fastBitmap;
                                    if (SUCCEEDED(localFactory->CreateBitmap(actualWidth, actualHeight, closestFormat, WICBitmapCacheOnLoad, &fastBitmap))) {
                                        WICRect rc = { 0, 0, (INT)actualWidth, (INT)actualHeight };
                                        ComPtr<IWICBitmapLock> lock;
                                        if (SUCCEEDED(fastBitmap->Lock(&rc, WICBitmapLockWrite, &lock))) {
                                            UINT cbStride = 0, cbBufferSize = 0;
                                            BYTE* pbBuffer = nullptr;
                                            lock->GetStride(&cbStride);
                                            lock->GetDataPointer(&cbBufferSize, &pbBuffer);

                                            if (SUCCEEDED(sourceTransform->CopyPixels(nullptr, actualWidth, actualHeight, &closestFormat, WICBitmapTransformRotate0, cbStride, cbBufferSize, pbBuffer))) {
                                                sourceToCache = fastBitmap;
                                                nativeScaled = true;
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

        std::vector<UINT> allFramesDelays;
        std::vector<AppContext::AnimationFrameMetadata> allFramesMetadata;
        UINT canvasWidth = 0, canvasHeight = 0;

        ComPtr<IWICMetadataQueryReader> decoderMetadata;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(&decoderMetadata))) {
            wil::unique_prop_variant propValue;
            if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Width", &propValue)) && propValue.vt == VT_UI2) canvasWidth = propValue.uiVal;
            propValue.reset();
            if (SUCCEEDED(decoderMetadata->GetMetadataByName(L"/logscrdesc/Height", &propValue)) && propValue.vt == VT_UI2) canvasHeight = propValue.uiVal;
        }

        for (UINT i = 0; i < frameCount; ++i) {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(i, &frame))) continue;

            UINT frameWidth = 0, frameHeight = 0;
            frame->GetSize(&frameWidth, &frameHeight);
            if (canvasWidth == 0 || canvasHeight == 0) {
                canvasWidth = frameWidth; canvasHeight = frameHeight;
            }

            AppContext::AnimationFrameMetadata meta;
            meta.width = frameWidth; meta.height = frameHeight;

            ComPtr<IWICMetadataQueryReader> metadataReader;
            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                wil::unique_prop_variant propValue;
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Delay", &propValue)) && propValue.vt == VT_UI2) meta.delay = propValue.uiVal * 10;
                propValue.reset();
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/grctlext/Disposal", &propValue)) && propValue.vt == VT_UI1) meta.disposal = propValue.bVal;
                propValue.reset();
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/imgdesc/Left", &propValue)) && propValue.vt == VT_UI2) meta.left = propValue.uiVal;
                propValue.reset();
                if (SUCCEEDED(metadataReader->GetMetadataByName(L"/imgdesc/Top", &propValue)) && propValue.vt == VT_UI2) meta.top = propValue.uiVal;
            }
            if (meta.delay <= 10) meta.delay = 100;
            allFramesDelays.push_back(meta.delay);
            allFramesMetadata.push_back(meta);

            if (!IsSequenceValid(mySeqId)) return;
        }

        if (allFramesMetadata.empty()) {
            PostMessage(m_ctx.hWnd, WM_APP_IMAGE_LOAD_FAILED, 0, (LPARAM)mySeqId);
            return;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
            m_ctx.stagedDelays = std::move(allFramesDelays);
            m_ctx.stagedFrameMetadata = std::move(allFramesMetadata);
            m_ctx.stagedRawFileData = std::move(rawData); // Transfer file memory to context

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
        if (!m_ctx.stagedStaticConverter && m_ctx.stagedFrames.empty() && m_ctx.stagedSvgData.empty() && m_ctx.stagedFrameMetadata.empty()) {
            m_ctx.isLoading = false;
            return;
        }

        // Clear the old image state before displaying new
        KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
        m_ctx.d2dBitmap = nullptr;
        m_ctx.highResImageSource = nullptr;
        m_ctx.animationD2DBitmaps.clear();
        m_ctx.animationFrameDelays.clear();
        m_ctx.wicConverter = nullptr;
        m_ctx.wicConverterOriginal = nullptr;
        m_ctx.svgDocument = nullptr;
        m_ctx.svgData.clear();
        m_ctx.isSvg = false;
        m_ctx.isAnimated = false;
        m_ctx.isAnimationPaused = false;
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
        else if (!m_ctx.stagedFrameMetadata.empty()) {
            bool animated = m_ctx.stagedFrameMetadata.size() > 1;
            if (m_ctx.originalContainerFormat == GUID_ContainerFormatTiff) animated = false;

            m_ctx.isAnimated = animated;
            m_ctx.animationFrameMetadata = std::move(m_ctx.stagedFrameMetadata);
            m_ctx.animationFrameDelays = std::move(m_ctx.stagedDelays);
            m_ctx.rawFileData = std::move(m_ctx.stagedRawFileData);
            m_ctx.originalWidth = m_ctx.stagedWidth;
            m_ctx.originalHeight = m_ctx.stagedHeight;

            ComPtr<IWICStream> stream;
            if (SUCCEEDED(m_ctx.wicFactory->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromMemory(m_ctx.rawFileData.data(), static_cast<DWORD>(m_ctx.rawFileData.size())))) {
                m_ctx.wicFactory->CreateDecoderFromStream(stream.Get(), NULL, WICDecodeMetadataCacheOnLoad, &m_ctx.animationDecoder);

                // Keep  stream alive in the context
                m_ctx.wicStream = stream;
            }

            m_ctx.lastCompositedFrame = -1;
            UINT canvasSize = m_ctx.stagedWidth * m_ctx.stagedHeight * 4;
            m_ctx.animationCanvas.assign(canvasSize, 0);
            m_ctx.animationCanvasPrev.assign(canvasSize, 0);
            m_ctx.currentAnimatedConverter = nullptr;

            m_ctx.currentAnimationFrame = 0;
            if (m_ctx.startAtEnd && !animated) {
                m_ctx.currentAnimationFrame = static_cast<UINT>(m_ctx.animationFrameMetadata.size() - 1);
            }

            if (m_ctx.animationDecoder) {
                m_ctx.currentAnimatedConverter = GetCompositedAnimationFrame(m_ctx.currentAnimationFrame);
                m_ctx.wicConverter = m_ctx.currentAnimatedConverter;
                m_ctx.wicConverterOriginal = m_ctx.currentAnimatedConverter;
            }

            if (animated && !m_ctx.animationFrameDelays.empty()) {
                SetTimer(m_ctx.hWnd, ANIMATION_TIMER_ID, m_ctx.animationFrameDelays[m_ctx.currentAnimationFrame], nullptr);
            }
        }
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
                // Abort if user navigated away while queued 
                if (!IsSequenceValid(currentSeqId)) return;

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
        m_ctx.wicConverter = nullptr;
        m_ctx.wicConverterOriginal = nullptr;
        m_ctx.undoStack.clear();
        m_ctx.stagedFrameMetadata.clear();
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

ComPtr<IWICBitmapSource> ViewerApp::GetCompositedAnimationFrame(UINT targetIndex) {
    if (!m_ctx.animationDecoder || targetIndex >= m_ctx.animationFrameMetadata.size()) return nullptr;

    UINT canvasWidth = m_ctx.originalWidth;
    UINT canvasHeight = m_ctx.originalHeight;
    UINT canvasStride = canvasWidth * 4;

    // Standard GIF rendering sequence
    UINT startIndex = m_ctx.lastCompositedFrame + 1;
    if (m_ctx.lastCompositedFrame == -1 || targetIndex < startIndex) {
        std::fill(m_ctx.animationCanvas.begin(), m_ctx.animationCanvas.end(), 0);
        std::fill(m_ctx.animationCanvasPrev.begin(), m_ctx.animationCanvasPrev.end(), 0);
        startIndex = 0;
    }

    for (UINT i = startIndex; i <= targetIndex; ++i) {
        const auto& meta = m_ctx.animationFrameMetadata[i];

        // Save previous state for disposal method 3
        if (meta.disposal == 3) {
            m_ctx.animationCanvasPrev = m_ctx.animationCanvas;
        }

        // Decode and composite the current frame
        ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(m_ctx.animationDecoder->GetFrame(i, &frame))) {
            if (ComPtr<IWICFormatConverter> converter = ConvertToFormat(m_ctx.wicFactory.Get(), frame.Get())) {
                UINT frameStride = meta.width * 4;
                UINT frameSize = frameStride * meta.height;
                std::vector<BYTE> framePixels(frameSize);

                if (SUCCEEDED(converter->CopyPixels(nullptr, frameStride, frameSize, framePixels.data()))) {
                    for (UINT y = 0; y < meta.height; ++y) {
                        if (meta.top + y >= canvasHeight) break;
                        BYTE* destRow = m_ctx.animationCanvas.data() + (meta.top + y) * canvasStride + (meta.left * 4);
                        BYTE* srcRow = framePixels.data() + y * frameStride;

                        for (UINT x = 0; x < meta.width; ++x) {
                            if (meta.left + x >= canvasWidth) break;
                            UINT dp = x * 4, sp = x * 4;
                            BYTE alpha = srcRow[sp + 3];

                            // PBGRA SrcOver Blending
                            if (alpha == 255) {
                                destRow[dp] = srcRow[sp];
                                destRow[dp + 1] = srcRow[sp + 1];
                                destRow[dp + 2] = srcRow[sp + 2];
                                destRow[dp + 3] = 255;
                            }
                            else if (alpha > 0) {
                                BYTE inv = 255 - alpha;
                                destRow[dp] = srcRow[sp] + (destRow[dp] * inv) / 255;
                                destRow[dp + 1] = srcRow[sp + 1] + (destRow[dp + 1] * inv) / 255;
                                destRow[dp + 2] = srcRow[sp + 2] + (destRow[dp + 2] * inv) / 255;
                                destRow[dp + 3] = alpha + (destRow[dp + 3] * inv) / 255;
                            }
                        }
                    }
                }
            }
        }

        // The disposal method of the target frame applies to the next frame
        if (i == targetIndex) break;

        // Apply disposal for next frame
        if (meta.disposal == 2) {
            for (UINT y = 0; y < meta.height; ++y) {
                if (meta.top + y >= canvasHeight) break;
                BYTE* destRow = m_ctx.animationCanvas.data() + (meta.top + y) * canvasStride + (meta.left * 4);
                for (UINT x = 0; x < meta.width; ++x) {
                    if (meta.left + x >= canvasWidth) break;
                    UINT dp = x * 4;
                    destRow[dp] = 0; destRow[dp + 1] = 0; destRow[dp + 2] = 0; destRow[dp + 3] = 0;
                }
            }
        }
        else if (meta.disposal == 3) {
            m_ctx.animationCanvas = m_ctx.animationCanvasPrev;
        }
    }

    m_ctx.lastCompositedFrame = targetIndex;

    // New standalone bitmap with deep copy of the memory
    ComPtr<IWICBitmap> finalBmp;
    if (SUCCEEDED(m_ctx.wicFactory->CreateBitmap(canvasWidth, canvasHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &finalBmp))) {
        WICRect rc = { 0, 0, static_cast<INT>(canvasWidth), static_cast<INT>(canvasHeight) };
        ComPtr<IWICBitmapLock> lock;
        if (SUCCEEDED(finalBmp->Lock(&rc, WICBitmapLockWrite, &lock))) {
            UINT cbStride = 0, cbBufferSize = 0;
            BYTE* pbBuffer = nullptr;
            lock->GetStride(&cbStride);
            lock->GetDataPointer(&cbBufferSize, &pbBuffer);

            if (pbBuffer) {
                // Copy row by row to respect WIC 
                for (UINT y = 0; y < canvasHeight; ++y) {
                    memcpy(pbBuffer + y * cbStride, m_ctx.animationCanvas.data() + y * canvasStride, canvasStride);
                }
            }
        }

        // Return the WIC bitmap directly 
        return finalBmp;
    }

    return nullptr;
}