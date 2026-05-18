#include "viewer.h"
#include <d2d1helper.h>



bool ViewerApp::GetCurrentImageSize(UINT* width, UINT* height) {
    CriticalSectionLock lock(m_ctx.wicMutex);
    if (m_ctx.isSvg && m_ctx.svgDocument) {
        D2D1_SIZE_F size = m_ctx.svgDocument->GetViewportSize();
        *width = static_cast<UINT>(size.width);
        *height = static_cast<UINT>(size.height);
        return true;
    }
    else if (m_ctx.isAnimated && !m_ctx.animationFrameConverters.empty()) {
        return SUCCEEDED(m_ctx.animationFrameConverters[0]->GetSize(width, height));
    }
    else if (m_ctx.wicConverter) {
        return SUCCEEDED(m_ctx.wicConverter->GetSize(width, height));
    }
    return false;
}

void ViewerApp::CreateDeviceResources() {
    if (!m_ctx.renderTarget) {
        RECT rc;
        GetClientRect(m_ctx.hWnd, &rc);
        UINT width = std::max(1L, rc.right - rc.left);
        UINT height = std::max(1L, rc.bottom - rc.top);
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3 };
        ComPtr<ID3D11Device> d3dDevice;
        // Try gpu
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &d3dDevice, nullptr, nullptr);
        // Fallback to software emulation
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &d3dDevice, nullptr, nullptr);
        }

        if (FAILED(hr)) {
            return;
        }
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &d3dDevice, nullptr, nullptr);

        ComPtr<IDXGIDevice> dxgiDevice;
        if (d3dDevice) {
            d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        }

        ComPtr<ID2D1Device> d2dDevice;
        m_ctx.d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
        d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_ctx.renderTarget);

        ComPtr<IDXGIAdapter> dxgiAdapter;
        dxgiDevice->GetAdapter(&dxgiAdapter);
        ComPtr<IDXGIFactory2> dxgiFactory;
        dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
        DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
        swapDesc.Width = width;
        swapDesc.Height = height;
        swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = 2;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        dxgiFactory->CreateSwapChainForHwnd(d3dDevice.Get(), m_ctx.hWnd, &swapDesc, nullptr, nullptr, &m_ctx.swapChain);

        ComPtr<IDXGISurface> backBuffer;
        m_ctx.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> targetBmp;
        m_ctx.renderTarget->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bmpProps, &targetBmp);
        m_ctx.renderTarget->SetTarget(targetBmp.Get());

        hr = S_OK;
        if (SUCCEEDED(hr)) { hr = m_ctx.renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_ctx.textBrush); }
        if (SUCCEEDED(hr)) {
            hr = m_ctx.renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.7f), &m_ctx.cropRectBrush);
        }
        if (SUCCEEDED(hr)) {
            hr = m_ctx.renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f), &m_ctx.fadeBrush);
        }
        if (SUCCEEDED(hr)) {
            float dpiScale = GetDpiForWindow(m_ctx.hWnd) / 96.0f;
            hr = m_ctx.writeFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f * dpiScale, L"en-us", &m_ctx.textFormat);
        }
        if (SUCCEEDED(hr)) {
            m_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    if (m_ctx.renderTarget && m_ctx.isSvg && !m_ctx.svgDocument && !m_ctx.svgData.empty()) {
        ComPtr<ID2D1DeviceContext5> dc5;
        if (SUCCEEDED(m_ctx.renderTarget->QueryInterface(IID_PPV_ARGS(&dc5)))) {
            ComPtr<IStream> stream = SHCreateMemStream(m_ctx.svgData.data(), static_cast<UINT>(m_ctx.svgData.size()));
            if (stream) {
                dc5->CreateSvgDocument(stream.Get(), D2D1::SizeF(1000.0f, 1000.0f), &m_ctx.svgDocument);
            }
        }
    }

    if (m_ctx.bgColor == BackgroundColor::Transparent) {
        if (!m_ctx.checkerboardBrush && m_ctx.renderTarget) {
            const int dim = 8;
            const int w = dim * 2; const int h = dim * 2;
            std::vector<UINT32> pixels(w * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    bool isLight = ((x / dim) % 2) == ((y / dim) % 2);
                    pixels[y * w + x] = isLight ? 0xFFCCCCCC : 0xFF999999;
                }
            }
            ComPtr<ID2D1Bitmap> checkerboardBitmap;
            D2D1_SIZE_U size = D2D1::SizeU(w, h);
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (SUCCEEDED(m_ctx.renderTarget->CreateBitmap(size, pixels.data(), w * 4, &props, &checkerboardBitmap))) {
                D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                m_ctx.renderTarget->CreateBitmapBrush(checkerboardBitmap.Get(), brushProps, &m_ctx.checkerboardBrush);
            }
        }
    }
    else {
        if (m_ctx.checkerboardBrush) { m_ctx.checkerboardBrush = nullptr; }
    }
}

void ViewerApp::DiscardDeviceResources() {
    CriticalSectionLock lock(m_ctx.wicMutex);
    m_ctx.renderTarget = nullptr;
    m_ctx.textBrush = nullptr;
    m_ctx.textFormat = nullptr;
    m_ctx.checkerboardBrush = nullptr;
    m_ctx.cropRectBrush = nullptr;
    m_ctx.fadeBrush = nullptr;
    m_ctx.animationD2DBitmaps.clear();
    m_ctx.svgDocument = nullptr;
}

void ViewerApp::DrawOsdOverlay(ID2D1DeviceContext* renderTarget) {
    ImageProperties props = GetCurrentOsdProperties();
    if (props.filePath.empty()) return;

    D2D1_SIZE_F rtSize = renderTarget->GetSize();
    float dpiScale = GetDpiForWindow(m_ctx.hWnd) / 96.0f;
    float padding = 10.0f * dpiScale;
    float lineHeight = 18.0f * dpiScale;
    float textHeight = lineHeight * 8 + padding * 2;

    std::wstring osdText;
    osdText += L"Image Format: " + props.imageFormat + L"\n";
    osdText += L"Dimensions: " + props.dimensions + L"\n";
    osdText += L"Bit Depth: " + props.bitDepth + L"\n";
    osdText += L"DPI: " + props.dpi + L"\n";
    osdText += L"\n";
    osdText += L"File Size: " + props.fileSize + L"\n";
    osdText += L"Attributes: " + props.attributes + L"\n";
    osdText += L"\n";
    osdText += L"F-stop: " + props.fStop + L"  Exposure: " + props.exposureTime + L"  ISO: " + props.iso + L"\n";
    osdText += L"Author: " + props.author + L"  Software: " + props.software + L"\n";

    renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

    ComPtr<IDWriteTextLayout> textLayout;
    if (FAILED(m_ctx.writeFactory->CreateTextLayout(
        osdText.c_str(),
        static_cast<UINT32>(osdText.length()),
        m_ctx.textFormat.Get(),
        rtSize.width - 2 * padding,
        rtSize.height,
        &textLayout
    ))) return;
    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    float bgWidth = metrics.widthIncludingTrailingWhitespace + padding * 2;
    float bgHeight = metrics.height + padding * 2;
    float bgX = padding;
    float bgY = rtSize.height - bgHeight - padding;
    D2D1_RECT_F bgRect = D2D1::RectF(bgX, bgY, bgX + bgWidth, bgY + bgHeight);

    ComPtr<ID2D1SolidColorBrush> bgBrush;
    renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f), &bgBrush);
    renderTarget->FillRectangle(bgRect, bgBrush.Get());
    D2D1_RECT_F textRect = D2D1::RectF(bgX + padding, bgY + padding, bgX + bgWidth - padding, bgY + bgHeight - padding);

    m_ctx.textBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    renderTarget->DrawTextLayout(D2D1::Point2F(textRect.left, textRect.top), textLayout.Get(), m_ctx.textBrush.Get());
}

void ViewerApp::Render() {
    CreateDeviceResources();
    if (!m_ctx.renderTarget) return;

    m_ctx.renderTarget->BeginDraw();
    if (m_ctx.bgColor == BackgroundColor::Transparent && m_ctx.checkerboardBrush) {
        D2D1_SIZE_F rtSize = m_ctx.renderTarget->GetSize();
        m_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        m_ctx.renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), m_ctx.checkerboardBrush.Get());
    }
    else {
        D2D1_COLOR_F color;
        switch (m_ctx.bgColor) {
        case BackgroundColor::Black: color = D2D1::ColorF(0.0f, 0.0f, 0.0f); break;
        case BackgroundColor::White: color = D2D1::ColorF(1.0f, 1.0f, 1.0f); break;
        default:
        case BackgroundColor::Grey: color = D2D1::ColorF(0.117f, 0.117f, 0.117f);
            break;
        }
        m_ctx.renderTarget->Clear(color);
    }

    if (m_ctx.isLoading && (GetTickCount64() - m_ctx.loadStartTime >= 700)) {
        RECT rc;
        GetClientRect(m_ctx.hWnd, &rc);
        D2D1_RECT_F layoutRect = D2D1::RectF(
            static_cast<float>(rc.left),
            static_cast<float>(rc.top),
            static_cast<float>(rc.right),
            static_cast<float>(rc.bottom)
        );
        D2D1_COLOR_F textColor;
        if (m_ctx.bgColor == BackgroundColor::White || m_ctx.bgColor == BackgroundColor::Transparent) {
            textColor = D2D1::ColorF(D2D1::ColorF::Black);
        }
        else {
            textColor = D2D1::ColorF(D2D1::ColorF::White);
        }
        m_ctx.textBrush->SetColor(textColor);

        m_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        m_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_ctx.renderTarget->DrawTextW(
            L"Loading...",
            10,
            m_ctx.textFormat.Get(),
            layoutRect,
            m_ctx.textBrush.Get()
        );
        m_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    else {
        ComPtr<ID2D1Bitmap> bitmapToDraw;
        bool hasImage = false;

        if (m_ctx.isAnimated) {
            CriticalSectionLock lock(m_ctx.wicMutex);
            if (m_ctx.animationD2DBitmaps.size() != m_ctx.animationFrameConverters.size()) {
                m_ctx.animationD2DBitmaps.assign(m_ctx.animationFrameConverters.size(), nullptr);
                m_ctx.d2dBitmap = nullptr;
                m_ctx.wicConverter = nullptr;
            }

            // Lazy load and aggressive unload
            if (m_ctx.currentAnimationFrame < m_ctx.animationFrameConverters.size()) {
                if (!m_ctx.animationD2DBitmaps[m_ctx.currentAnimationFrame]) {


                    for (size_t i = 0; i < m_ctx.animationD2DBitmaps.size(); ++i) {
                        if (i != m_ctx.currentAnimationFrame) {
                            m_ctx.animationD2DBitmaps[i] = nullptr;
                        }
                    }

                    ComPtr<IWICBitmapSource> source = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
                    ComPtr<ID2D1Bitmap> d2dFrameBitmap;
                    if (SUCCEEDED(m_ctx.renderTarget->CreateBitmapFromWicBitmap(source.Get(), nullptr, &d2dFrameBitmap))) {
                        m_ctx.animationD2DBitmaps[m_ctx.currentAnimationFrame] = d2dFrameBitmap;
                    }
                }
                bitmapToDraw = m_ctx.animationD2DBitmaps[m_ctx.currentAnimationFrame];
            }
            hasImage = (bitmapToDraw != nullptr);
        }
        else if (!m_ctx.isSvg) {
            CriticalSectionLock lock(m_ctx.wicMutex);
            if (!m_ctx.d2dBitmap && m_ctx.wicConverter) {
                ComPtr<IWICBitmapSource> source = m_ctx.wicConverter;
                m_ctx.renderTarget->CreateBitmapFromWicBitmap(
                    source.Get(),
                    nullptr,
                    &m_ctx.d2dBitmap
                );
                m_ctx.animationD2DBitmaps.clear();
            }
            bitmapToDraw = m_ctx.d2dBitmap;
            hasImage = (m_ctx.wicConverter != nullptr);
        }

        bool isSvgToDraw = (m_ctx.isSvg && m_ctx.svgDocument);
        if (isSvgToDraw) hasImage = true;

        if ((bitmapToDraw || isSvgToDraw) && !IsIconic(m_ctx.hWnd)) {
            D2D1_SIZE_F bmpSize;
            if (isSvgToDraw) {
                bmpSize = m_ctx.svgDocument->GetViewportSize();
            }
            else {
                bmpSize = bitmapToDraw->GetSize();
            }

            D2D1_SIZE_F rtSize = m_ctx.renderTarget->GetSize();
            D2D1_POINT_2F bmpCenter = D2D1::Point2F(bmpSize.width / 2.f, bmpSize.height / 2.f);
            D2D1_POINT_2F windowCenter = D2D1::Point2F(rtSize.width / 2.f, rtSize.height / 2.f);

            float scaleX = m_ctx.isFlippedHorizontal ? -m_ctx.zoomFactor : m_ctx.zoomFactor;
            float scaleY = m_ctx.zoomFactor;

            m_ctx.renderTarget->SetTransform(
                D2D1::Matrix3x2F::Rotation(static_cast<float>(m_ctx.rotationAngle), bmpCenter) *
                D2D1::Matrix3x2F::Scale(scaleX, scaleY, bmpCenter) *
                D2D1::Matrix3x2F::Translation(windowCenter.x - bmpCenter.x + m_ctx.offsetX, windowCenter.y - bmpCenter.y + m_ctx.offsetY)
            );
            float opacity = 1.0f;
            if (m_ctx.isFading) {
                ULONGLONG elapsed = GetTickCount64() - m_ctx.fadeStartTime;
                const float FADE_DURATION = 120.0f;
                if (elapsed >= FADE_DURATION) {
                    m_ctx.isFading = false;
                }
                else {
                    opacity = static_cast<float>(elapsed) / FADE_DURATION;
                }
            }

            if (isSvgToDraw) {
                ComPtr<ID2D1DeviceContext5> dc5;
                if (SUCCEEDED(m_ctx.renderTarget->QueryInterface(IID_PPV_ARGS(&dc5)))) {
                    dc5->DrawSvgDocument(m_ctx.svgDocument.Get());
                }
            }
            else {
                bool isIntegerZoom = (m_ctx.zoomFactor > 1.01f && std::abs(m_ctx.zoomFactor - std::round(m_ctx.zoomFactor)) < 0.001f);
                D2D1_BITMAP_INTERPOLATION_MODE interpModeBmp = (!m_ctx.smoothScaling || isIntegerZoom) ?
                    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
                m_ctx.renderTarget->DrawBitmap(
                    bitmapToDraw.Get(), nullptr, opacity,
                    interpModeBmp
                );
            }

            if ((m_ctx.isSelectingCropRect || m_ctx.isCropPending) && m_ctx.fadeBrush) {
                D2D1_RECT_F localRect;
                if (m_ctx.isSelectingCropRect) {
                    float x1, y1, x2, y2;
                    ConvertWindowToImagePoint(m_ctx.cropStartPoint, x1, y1);
                    POINT endPoint = { (LONG)m_ctx.cropRectWindow.right, (LONG)m_ctx.cropRectWindow.bottom };
                    ConvertWindowToImagePoint(endPoint, x2, y2);
                    localRect = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));
                }
                else {
                    localRect = m_ctx.cropRectLocal;
                }

                localRect.left = std::max(0.0f, localRect.left);
                localRect.top = std::max(0.0f, localRect.top);
                localRect.right = std::min(bmpSize.width, localRect.right);
                localRect.bottom = std::min(bmpSize.height, localRect.bottom);
                if (localRect.left < localRect.right && localRect.top < localRect.bottom) {
                    m_ctx.renderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, bmpSize.width, localRect.top), m_ctx.fadeBrush.Get());
                    m_ctx.renderTarget->FillRectangle(D2D1::RectF(0.0f, localRect.bottom, bmpSize.width, bmpSize.height), m_ctx.fadeBrush.Get());
                    m_ctx.renderTarget->FillRectangle(D2D1::RectF(0.0f, localRect.top, localRect.left, localRect.bottom), m_ctx.fadeBrush.Get());
                    m_ctx.renderTarget->FillRectangle(D2D1::RectF(localRect.right, localRect.top, bmpSize.width, localRect.bottom), m_ctx.fadeBrush.Get());
                }
            }
        }
        else if (!hasImage && !m_ctx.isLoading && m_ctx.textFormat && m_ctx.textBrush) {
            RECT rc;
            GetClientRect(m_ctx.hWnd, &rc);
            D2D1_RECT_F layoutRect = D2D1::RectF(
                static_cast<float>(rc.left),
                static_cast<float>(rc.top),
                static_cast<float>(rc.right),
                static_cast<float>(rc.bottom)
            );
            D2D1_COLOR_F textColor;
            if (m_ctx.bgColor == BackgroundColor::White || m_ctx.bgColor == BackgroundColor::Transparent) {
                textColor = D2D1::ColorF(D2D1::ColorF::Black);
            }
            else {
                textColor = D2D1::ColorF(D2D1::ColorF::White);
            }
            m_ctx.textBrush->SetColor(textColor);

            m_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            m_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_ctx.renderTarget->DrawTextW(
                L"Right-click for options or drag an image here",
                46,
                m_ctx.textFormat.Get(),
                layoutRect,
                m_ctx.textBrush.Get()
            );
            m_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        if (m_ctx.isOsdVisible && hasImage) {
            DrawOsdOverlay(m_ctx.renderTarget.Get());
        }

        if (m_ctx.isSelectingCropRect && m_ctx.cropRectBrush) {
            m_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            D2D1_RECT_F rect = m_ctx.cropRectWindow;
            if (rect.left > rect.right) std::swap(rect.left, rect.right);
            if (rect.top > rect.bottom) std::swap(rect.top, rect.bottom);
            m_ctx.cropRectBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White, 0.7f));
            m_ctx.renderTarget->DrawRectangle(rect, m_ctx.cropRectBrush.Get(), 1.0f);
        }
        else if (m_ctx.isCropPending && m_ctx.cropRectBrush) {
            POINT p1, p2, p3, p4;
            ConvertImageToWindowPoint(m_ctx.cropRectLocal.left, m_ctx.cropRectLocal.top, p1);
            ConvertImageToWindowPoint(m_ctx.cropRectLocal.right, m_ctx.cropRectLocal.top, p2);
            ConvertImageToWindowPoint(m_ctx.cropRectLocal.right, m_ctx.cropRectLocal.bottom, p3);
            ConvertImageToWindowPoint(m_ctx.cropRectLocal.left, m_ctx.cropRectLocal.bottom, p4);

            float dpiScale = GetDpiForWindow(m_ctx.hWnd) / 96.0f;
            float lineThick = 2.0f * dpiScale;

            m_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            m_ctx.cropRectBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White, 0.7f));
            m_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p1.x, (float)p1.y), D2D1::Point2F((float)p2.x, (float)p2.y), m_ctx.cropRectBrush.Get(), lineThick);
            m_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p2.x, (float)p2.y), D2D1::Point2F((float)p3.x, (float)p3.y), m_ctx.cropRectBrush.Get(), lineThick);
            m_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p3.x, (float)p3.y), D2D1::Point2F((float)p4.x, (float)p4.y), m_ctx.cropRectBrush.Get(), lineThick);
            m_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p4.x, (float)p4.y), D2D1::Point2F((float)p1.x, (float)p1.y), m_ctx.cropRectBrush.Get(), lineThick);
        }

        if (m_ctx.isCropPending && m_ctx.textFormat && m_ctx.textBrush) {
            float dpiScale = GetDpiForWindow(m_ctx.hWnd) / 96.0f;
            D2D1_SIZE_F rtSize = m_ctx.renderTarget->GetSize();
            D2D1_RECT_F layoutRect = D2D1::RectF(
                0.0f,
                10.0f * dpiScale,
                rtSize.width,
                rtSize.height
            );
            D2D1_COLOR_F textColor;
            if (m_ctx.bgColor == BackgroundColor::White) {
                textColor = D2D1::ColorF(D2D1::ColorF::Black);
            }
            else {
                textColor = D2D1::ColorF(D2D1::ColorF::White);
            }
            m_ctx.textBrush->SetColor(textColor);

            m_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            m_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            m_ctx.renderTarget->DrawTextW(
                L"Press Enter to apply crop, Esc to cancel",
                40,
                m_ctx.textFormat.Get(),
                layoutRect,
                m_ctx.textBrush.Get()
            );
            m_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            m_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    HRESULT hr = m_ctx.renderTarget->EndDraw();
    if (m_ctx.swapChain) {
        hr = m_ctx.swapChain->Present(1, 0);
    }
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        DiscardDeviceResources();
        InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
    }
    else if (m_ctx.isFading && !m_ctx.isLoading) {
        InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
    }
}

void ViewerApp::FitImageToWindow() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;

    RECT clientRect;
    GetClientRect(m_ctx.hWnd, &clientRect);
    if (IsRectEmpty(&clientRect)) return;

    float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
    float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
    float imageWidth = static_cast<float>(imgWidth);
    float imageHeight = static_cast<float>(imgHeight);

    if (m_ctx.rotationAngle == 90 || m_ctx.rotationAngle == 270) {
        std::swap(imageWidth, imageHeight);
    }

    if (imageWidth <= 0 || imageHeight <= 0) return;
    m_ctx.zoomFactor = std::min(clientWidth / imageWidth, clientHeight / imageHeight);
    m_ctx.offsetX = 0.0f;
    m_ctx.offsetY = 0.0f;
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::ZoomImage(float factor, POINT pt) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;

    RECT clientRect;
    GetClientRect(m_ctx.hWnd, &clientRect);
    float windowCenterX = (clientRect.right - clientRect.left) / 2.0f;
    float windowCenterY = (clientRect.bottom - clientRect.top) / 2.0f;

    float mouseXBeforeZoom = pt.x - (windowCenterX + m_ctx.offsetX);
    float mouseYBeforeZoom = pt.y - (windowCenterY + m_ctx.offsetY);

    float newZoomFactor = m_ctx.zoomFactor * factor;
    newZoomFactor = std::max(0.01f, std::min(100.0f, newZoomFactor));

    float mouseXAfterZoom = mouseXBeforeZoom * (newZoomFactor / m_ctx.zoomFactor);
    float mouseYAfterZoom = mouseYBeforeZoom * (newZoomFactor / m_ctx.zoomFactor);

    m_ctx.offsetX += (mouseXBeforeZoom - mouseXAfterZoom);
    m_ctx.offsetY += (mouseYBeforeZoom - mouseYAfterZoom);
    m_ctx.zoomFactor = newZoomFactor;

    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::RotateImage(bool clockwise) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;
    m_ctx.rotationAngle += clockwise ? 90 : -90;
    m_ctx.rotationAngle = (m_ctx.rotationAngle % 360 + 360) % 360;
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::FlipImage() {
    m_ctx.isFlippedHorizontal = !m_ctx.isFlippedHorizontal;
    InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
}

void ViewerApp::ConvertWindowToImagePoint(POINT pt, float& localX, float& localY) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        localX = 0; localY = 0;
        return;
    }

    RECT cr;
    GetClientRect(m_ctx.hWnd, &cr);
    float windowCenterX = (cr.right - cr.left) / 2.0f;
    float windowCenterY = (cr.bottom - cr.top) / 2.0f;

    float scaleX = m_ctx.isFlippedHorizontal ? -m_ctx.zoomFactor : m_ctx.zoomFactor;
    float scaleY = m_ctx.zoomFactor;
    if (scaleX == 0.0f) scaleX = 1.0f;
    if (scaleY == 0.0f) scaleY = 1.0f;

    float translatedX = pt.x - (windowCenterX + m_ctx.offsetX);
    float translatedY = pt.y - (windowCenterY + m_ctx.offsetY);

    float scaledX = translatedX / scaleX;
    float scaledY = translatedY / scaleY;

    double rad = -m_ctx.rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = static_cast<float>(cos(rad));
    float sinTheta = static_cast<float>(sin(rad));

    float unrotatedX = scaledX * cosTheta - scaledY * sinTheta;
    float unrotatedY = scaledX * sinTheta + scaledY * cosTheta;

    localX = unrotatedX + imgWidth / 2.0f;
    localY = unrotatedY + imgHeight / 2.0f;

    if (m_ctx.renderScale > 0.0f && m_ctx.renderScale != 1.0f) {
        localX /= m_ctx.renderScale;
        localY /= m_ctx.renderScale;
    }
}

void ViewerApp::ConvertImageToWindowPoint(float localX, float localY, POINT& pt) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        pt = { 0, 0 };
        return;
    }

    if (m_ctx.renderScale > 0.0f && m_ctx.renderScale != 1.0f) {
        localX *= m_ctx.renderScale;
        localY *= m_ctx.renderScale;
    }

    float unrotatedX = localX - imgWidth / 2.0f;
    float unrotatedY = localY - imgHeight / 2.0f;

    double rad = m_ctx.rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = static_cast<float>(cos(rad));
    float sinTheta = static_cast<float>(sin(rad));
    float scaledX = unrotatedX * cosTheta - unrotatedY * sinTheta;
    float scaledY = unrotatedX * sinTheta + unrotatedY * cosTheta;

    float scaleFactorX = m_ctx.isFlippedHorizontal ? -m_ctx.zoomFactor : m_ctx.zoomFactor;
    float scaleFactorY = m_ctx.zoomFactor;
    float translatedX = scaledX * scaleFactorX;
    float translatedY = scaledY * scaleFactorY;

    RECT cr;
    GetClientRect(m_ctx.hWnd, &cr);
    float windowCenterX = (cr.right - cr.left) / 2.0f;
    float windowCenterY = (cr.bottom - cr.top) / 2.0f;

    pt.x = static_cast<LONG>(translatedX + windowCenterX + m_ctx.offsetX);
    pt.y = static_cast<LONG>(translatedY + windowCenterY + m_ctx.offsetY);
}