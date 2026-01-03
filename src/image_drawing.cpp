#include "viewer.h"
#include <d2d1helper.h>

extern AppContext g_ctx;

bool GetCurrentImageSize(UINT* width, UINT* height) {
    CriticalSectionLock lock(g_ctx.wicMutex);
    if (g_ctx.isAnimated && !g_ctx.animationFrameConverters.empty()) {
        return SUCCEEDED(g_ctx.animationFrameConverters[0]->GetSize(width, height));
    }
    else if (g_ctx.wicConverter) {
        return SUCCEEDED(g_ctx.wicConverter->GetSize(width, height));
    }
    return false;
}

void CreateDeviceResources() {
    if (!g_ctx.renderTarget) {
        RECT rc;
        GetClientRect(g_ctx.hWnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

        HRESULT hr = g_ctx.d2dFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(g_ctx.hWnd, size),
            &g_ctx.renderTarget
        );

        if (SUCCEEDED(hr)) {
            hr = g_ctx.renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White),
                &g_ctx.textBrush
            );
        }
        if (SUCCEEDED(hr)) {
            hr = g_ctx.renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.7f),
                &g_ctx.cropRectBrush
            );
        }
        if (SUCCEEDED(hr)) {
            hr = g_ctx.renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f),
                &g_ctx.fadeBrush
            );
        }
        if (SUCCEEDED(hr)) {
            hr = g_ctx.renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White),
                &g_ctx.ocrMessageBrush
            );
        }
        if (SUCCEEDED(hr)) {
            hr = g_ctx.renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.7f),
                &g_ctx.ocrMessageBgBrush
            );
        }
        if (SUCCEEDED(hr)) {
            hr = g_ctx.writeFactory->CreateTextFormat(
                L"Segoe UI",
                NULL,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                14.0f,
                L"en-us",
                &g_ctx.textFormat
            );
        }
        if (SUCCEEDED(hr)) {
            g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    if (g_ctx.bgColor == BackgroundColor::Transparent) {
        if (!g_ctx.checkerboardBrush && g_ctx.renderTarget) {
            const int dim = 8;
            const int w = dim * 2;
            const int h = dim * 2;
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

            HRESULT hr = g_ctx.renderTarget->CreateBitmap(size, pixels.data(), w * 4, &props, &checkerboardBitmap);

            if (SUCCEEDED(hr)) {
                D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                g_ctx.renderTarget->CreateBitmapBrush(checkerboardBitmap, brushProps, &g_ctx.checkerboardBrush);
            }
        }
    }
    else {
        if (g_ctx.checkerboardBrush) {
            g_ctx.checkerboardBrush = nullptr;
        }
    }
}

void DiscardDeviceResources() {
    CriticalSectionLock lock(g_ctx.wicMutex);
    g_ctx.renderTarget = nullptr;
    g_ctx.d2dBitmap = nullptr;
    g_ctx.textBrush = nullptr;
    g_ctx.textFormat = nullptr;
    g_ctx.checkerboardBrush = nullptr;
    g_ctx.cropRectBrush = nullptr;
    g_ctx.fadeBrush = nullptr;
    g_ctx.ocrMessageBrush = nullptr;
    g_ctx.ocrMessageBgBrush = nullptr;
    g_ctx.animationD2DBitmaps.clear();
}

static void DrawOsdOverlay(ID2D1HwndRenderTarget* renderTarget) {
    ImageProperties props = GetCurrentOsdProperties();
    if (props.filePath.empty()) return;

    D2D1_SIZE_F rtSize = renderTarget->GetSize();
    float padding = 10.0f;
    float lineHeight = 18.0f;
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
    if (FAILED(g_ctx.writeFactory->CreateTextLayout(
        osdText.c_str(),
        static_cast<UINT32>(osdText.length()),
        g_ctx.textFormat,
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
    renderTarget->FillRectangle(bgRect, bgBrush);

    D2D1_RECT_F textRect = D2D1::RectF(bgX + padding, bgY + padding, bgX + bgWidth - padding, bgY + bgHeight - padding);

    g_ctx.textBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    renderTarget->DrawTextLayout(D2D1::Point2F(textRect.left, textRect.top), textLayout, g_ctx.textBrush);
}

static void DrawEyedropperOverlay(ID2D1HwndRenderTarget* renderTarget) {
    if (g_ctx.colorStringRgb.empty() && !g_ctx.didCopyColor) return;

    std::wstring text;
    if (g_ctx.didCopyColor) {
        text = L"Copied " + g_ctx.colorStringHex + L"!";
    }
    else {
        text = g_ctx.colorStringRgb + L"\n" + g_ctx.colorStringHex;
    }

    D2D1_SIZE_F rtSize = renderTarget->GetSize();
    float padding = 10.0f;

    renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

    ComPtr<IDWriteTextLayout> textLayout;
    if (FAILED(g_ctx.writeFactory->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        g_ctx.textFormat,
        rtSize.width,
        rtSize.height,
        &textLayout
    ))) return;

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    float bgWidth = metrics.widthIncludingTrailingWhitespace + padding * 2;
    float bgHeight = metrics.height + padding * 2;

    D2D1_POINT_2F mousePos = { (float)g_ctx.currentMousePos.x, (float)g_ctx.currentMousePos.y };
    float bgX = mousePos.x + 20.0f;
    float bgY = mousePos.y + 20.0f;

    if (bgX + bgWidth > rtSize.width) {
        bgX = mousePos.x - 20.0f - bgWidth;
    }
    if (bgY + bgHeight > rtSize.height) {
        bgY = mousePos.y - 20.0f - bgHeight;
    }
    bgX = std::max(padding, bgX);
    bgY = std::max(padding, bgY);

    D2D1_RECT_F bgRect = D2D1::RectF(bgX, bgY, bgX + bgWidth, bgY + bgHeight);

    ComPtr<ID2D1SolidColorBrush> bgBrush;
    renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.7f), &bgBrush);
    if (bgBrush) {
        renderTarget->FillRectangle(bgRect, bgBrush);
    }

    D2D1_RECT_F textRect = D2D1::RectF(bgX + padding, bgY + padding, bgX + bgWidth - padding, bgY + bgHeight - padding);

    g_ctx.textBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    renderTarget->DrawTextLayout(D2D1::Point2F(textRect.left, textRect.top), textLayout, g_ctx.textBrush);
}

static void DrawOcrMessageOverlay(ID2D1HwndRenderTarget* renderTarget) {
    if (g_ctx.ocrMessage.empty() || !g_ctx.ocrMessageBrush || !g_ctx.ocrMessageBgBrush || !g_ctx.textFormat) return;

    ULONGLONG elapsedTime = GetTickCount64() - g_ctx.ocrMessageStartTime;
    float opacity = 1.0f;

    if (elapsedTime > 700) {
        opacity = std::max(0.0f, 1.0f - static_cast<float>(elapsedTime - 700) / 300.0f);
    }
    if (opacity == 0.0f) return;

    D2D1_SIZE_F rtSize = renderTarget->GetSize();
    float padding = 15.0f;

    renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

    ComPtr<IDWriteTextLayout> textLayout;
    if (FAILED(g_ctx.writeFactory->CreateTextLayout(
        g_ctx.ocrMessage.c_str(),
        static_cast<UINT32>(g_ctx.ocrMessage.length()),
        g_ctx.textFormat,
        rtSize.width - 4 * padding,
        rtSize.height,
        &textLayout
    ))) return;

    g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    DWRITE_TEXT_METRICS metrics;
    textLayout->GetMetrics(&metrics);

    float bgWidth = metrics.widthIncludingTrailingWhitespace + padding * 2;
    float bgHeight = metrics.height + padding * 2;
    float bgX = (rtSize.width - bgWidth) / 2.0f;
    float bgY = (rtSize.height - bgHeight) / 2.0f;

    D2D1_RECT_F bgRect = D2D1::RectF(bgX, bgY, bgX + bgWidth, bgY + bgHeight);
    D2D1_ROUNDED_RECT roundedBgRect = D2D1::RoundedRect(bgRect, 5.0f, 5.0f);

    g_ctx.ocrMessageBgBrush->SetOpacity(opacity * 0.7f);
    g_ctx.ocrMessageBrush->SetOpacity(opacity);

    renderTarget->FillRoundedRectangle(roundedBgRect, g_ctx.ocrMessageBgBrush);

    D2D1_RECT_F textRect = D2D1::RectF(bgX, bgY + padding, bgX + bgWidth, bgY + bgHeight - padding);
    renderTarget->DrawTextLayout(D2D1::Point2F(textRect.left, textRect.top), textLayout, g_ctx.ocrMessageBrush);

    g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

void Render() {
    CreateDeviceResources();
    if (!g_ctx.renderTarget) return;

    g_ctx.renderTarget->BeginDraw();

    if (g_ctx.bgColor == BackgroundColor::Transparent && g_ctx.checkerboardBrush) {
        D2D1_SIZE_F rtSize = g_ctx.renderTarget->GetSize();
        g_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        g_ctx.renderTarget->FillRectangle(D2D1::RectF(0, 0, rtSize.width, rtSize.height), g_ctx.checkerboardBrush);
    }
    else {
        D2D1_COLOR_F color;
        switch (g_ctx.bgColor) {
        case BackgroundColor::Black: color = D2D1::ColorF(0.0f, 0.0f, 0.0f); break;
        case BackgroundColor::White: color = D2D1::ColorF(1.0f, 1.0f, 1.0f); break;
        default:
        case BackgroundColor::Grey: color = D2D1::ColorF(0.117f, 0.117f, 0.117f); break;
        }
        g_ctx.renderTarget->Clear(color);
    }

    if (g_ctx.isLoading) {
        RECT rc;
        GetClientRect(g_ctx.hWnd, &rc);
        D2D1_RECT_F layoutRect = D2D1::RectF(
            static_cast<float>(rc.left),
            static_cast<float>(rc.top),
            static_cast<float>(rc.right),
            static_cast<float>(rc.bottom)
        );

        D2D1_COLOR_F textColor;
        if (g_ctx.bgColor == BackgroundColor::White || g_ctx.bgColor == BackgroundColor::Transparent) {
            textColor = D2D1::ColorF(D2D1::ColorF::Black);
        }
        else {
            textColor = D2D1::ColorF(D2D1::ColorF::White);
        }
        g_ctx.textBrush->SetColor(textColor);

        g_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        g_ctx.renderTarget->DrawTextW(
            L"Loading...",
            10,
            g_ctx.textFormat,
            layoutRect,
            g_ctx.textBrush
        );
        g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    else {
        ComPtr<ID2D1Bitmap> bitmapToDraw;
        bool hasImage = false;

        if (g_ctx.isAnimated) {
            CriticalSectionLock lock(g_ctx.wicMutex);
            if (g_ctx.animationD2DBitmaps.empty() && !g_ctx.animationFrameConverters.empty()) {
                for (const auto& converter : g_ctx.animationFrameConverters) {
                    ComPtr<IWICBitmapSource> source(static_cast<IWICFormatConverter*>(converter));

                    if (g_ctx.isGrayscale) {
                        ComPtr<IWICFormatConverter> grayConverter;
                        if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&grayConverter))) {
                            if (SUCCEEDED(grayConverter->Initialize(source, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                                ComPtr<IWICFormatConverter> finalConverter;
                                if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&finalConverter))) {
                                    if (SUCCEEDED(finalConverter->Initialize(grayConverter, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                                        source = finalConverter;
                                    }
                                }
                            }
                        }
                    }

                    ComPtr<ID2D1Bitmap> d2dFrameBitmap;
                    if (SUCCEEDED(g_ctx.renderTarget->CreateBitmapFromWicBitmap(source, nullptr, &d2dFrameBitmap))) {
                        g_ctx.animationD2DBitmaps.push_back(d2dFrameBitmap);
                    }
                }
                g_ctx.d2dBitmap = nullptr;
                g_ctx.wicConverter = nullptr;
            }
            if (g_ctx.currentAnimationFrame < g_ctx.animationD2DBitmaps.size()) {
                bitmapToDraw = g_ctx.animationD2DBitmaps[g_ctx.currentAnimationFrame];
            }
            hasImage = !g_ctx.animationD2DBitmaps.empty();
        }
        else {
            CriticalSectionLock lock(g_ctx.wicMutex);
            if (!g_ctx.d2dBitmap && g_ctx.wicConverter) {
                ComPtr<IWICBitmapSource> source(static_cast<IWICFormatConverter*>(g_ctx.wicConverter));

                if (g_ctx.isGrayscale) {
                    ComPtr<IWICFormatConverter> grayConverter;
                    if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&grayConverter))) {
                        if (SUCCEEDED(grayConverter->Initialize(source, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                            ComPtr<IWICFormatConverter> finalConverter;
                            if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&finalConverter))) {
                                if (SUCCEEDED(finalConverter->Initialize(grayConverter, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                                    source = finalConverter;
                                }
                            }
                        }
                    }
                }

                g_ctx.renderTarget->CreateBitmapFromWicBitmap(
                    source,
                    nullptr,
                    &g_ctx.d2dBitmap
                );
                g_ctx.animationD2DBitmaps.clear();
            }
            bitmapToDraw = g_ctx.d2dBitmap;
            hasImage = (g_ctx.wicConverter != nullptr);
        }

        if (bitmapToDraw && !IsIconic(g_ctx.hWnd)) {
            D2D1_SIZE_F bmpSize = bitmapToDraw->GetSize();
            D2D1_SIZE_F rtSize = g_ctx.renderTarget->GetSize();
            D2D1_POINT_2F bmpCenter = D2D1::Point2F(bmpSize.width / 2.f, bmpSize.height / 2.f);
            D2D1_POINT_2F windowCenter = D2D1::Point2F(rtSize.width / 2.f, rtSize.height / 2.f);

            float scaleX = g_ctx.isFlippedHorizontal ? -g_ctx.zoomFactor : g_ctx.zoomFactor;
            float scaleY = g_ctx.zoomFactor;

            g_ctx.renderTarget->SetTransform(
                D2D1::Matrix3x2F::Rotation(static_cast<float>(g_ctx.rotationAngle), bmpCenter) *
                D2D1::Matrix3x2F::Scale(scaleX, scaleY, bmpCenter) *
                D2D1::Matrix3x2F::Translation(windowCenter.x - bmpCenter.x + g_ctx.offsetX, windowCenter.y - bmpCenter.y + g_ctx.offsetY)
            );

            g_ctx.renderTarget->DrawBitmap(
                bitmapToDraw,
                nullptr,
                1.0f,
                g_ctx.zoomFactor < 1.0f ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            );

            if ((g_ctx.isSelectingCropRect || g_ctx.isCropActive || g_ctx.isCropPending) && g_ctx.fadeBrush) {
                D2D1_RECT_F localRect;
                if (g_ctx.isSelectingCropRect) {
                    float x1, y1, x2, y2;
                    ConvertWindowToImagePoint(g_ctx.cropStartPoint, x1, y1);
                    POINT endPoint = { (LONG)g_ctx.cropRectWindow.right, (LONG)g_ctx.cropRectWindow.bottom };
                    ConvertWindowToImagePoint(endPoint, x2, y2);
                    localRect = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));
                }
                else {
                    localRect = g_ctx.cropRectLocal;
                }

                localRect.left = std::max(0.0f, localRect.left);
                localRect.top = std::max(0.0f, localRect.top);
                localRect.right = std::min(bmpSize.width, localRect.right);
                localRect.bottom = std::min(bmpSize.height, localRect.bottom);

                if (localRect.left < localRect.right && localRect.top < localRect.bottom) {
                    g_ctx.renderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, bmpSize.width, localRect.top), g_ctx.fadeBrush);
                    g_ctx.renderTarget->FillRectangle(D2D1::RectF(0.0f, localRect.bottom, bmpSize.width, bmpSize.height), g_ctx.fadeBrush);
                    g_ctx.renderTarget->FillRectangle(D2D1::RectF(0.0f, localRect.top, localRect.left, localRect.bottom), g_ctx.fadeBrush);
                    g_ctx.renderTarget->FillRectangle(D2D1::RectF(localRect.right, localRect.top, bmpSize.width, localRect.bottom), g_ctx.fadeBrush);
                }
            }
        }
        else if (!hasImage && g_ctx.textFormat && g_ctx.textBrush) {
            RECT rc;
            GetClientRect(g_ctx.hWnd, &rc);
            D2D1_RECT_F layoutRect = D2D1::RectF(
                static_cast<float>(rc.left),
                static_cast<float>(rc.top),
                static_cast<float>(rc.right),
                static_cast<float>(rc.bottom)
            );

            D2D1_COLOR_F textColor;
            if (g_ctx.bgColor == BackgroundColor::White || g_ctx.bgColor == BackgroundColor::Transparent) {
                textColor = D2D1::ColorF(D2D1::ColorF::Black);
            }
            else {
                textColor = D2D1::ColorF(D2D1::ColorF::White);
            }
            g_ctx.textBrush->SetColor(textColor);

            g_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            g_ctx.renderTarget->DrawTextW(
                L"Right-click for options or drag an image here",
                46,
                g_ctx.textFormat,
                layoutRect,
                g_ctx.textBrush
            );
            g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }

        if (g_ctx.isOsdVisible && hasImage) {
            DrawOsdOverlay(g_ctx.renderTarget);
        }

        if (g_ctx.isEyedropperActive) {
            DrawEyedropperOverlay(g_ctx.renderTarget);
        }

        if ((g_ctx.isSelectingCropRect || g_ctx.isDraggingOcrRect) && g_ctx.cropRectBrush) {
            g_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            D2D1_RECT_F rect = g_ctx.isSelectingCropRect ? g_ctx.cropRectWindow : g_ctx.ocrRectWindow;
            if (rect.left > rect.right) std::swap(rect.left, rect.right);
            if (rect.top > rect.bottom) std::swap(rect.top, rect.bottom);

            if (g_ctx.isDraggingOcrRect) {
                g_ctx.cropRectBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Red, 1.0f));
            }
            else {
                g_ctx.cropRectBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White, 0.7f));
            }
            g_ctx.renderTarget->DrawRectangle(rect, g_ctx.cropRectBrush, 1.0f);
        }
        else if ((g_ctx.isCropActive || g_ctx.isCropPending) && g_ctx.cropRectBrush) {
            POINT p1, p2, p3, p4;
            ConvertImageToWindowPoint(g_ctx.cropRectLocal.left, g_ctx.cropRectLocal.top, p1);
            ConvertImageToWindowPoint(g_ctx.cropRectLocal.right, g_ctx.cropRectLocal.top, p2);
            ConvertImageToWindowPoint(g_ctx.cropRectLocal.right, g_ctx.cropRectLocal.bottom, p3);
            ConvertImageToWindowPoint(g_ctx.cropRectLocal.left, g_ctx.cropRectLocal.bottom, p4);

            g_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            g_ctx.cropRectBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White, 0.7f));
            g_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p1.x, (float)p1.y), D2D1::Point2F((float)p2.x, (float)p2.y), g_ctx.cropRectBrush, 2.0f);
            g_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p2.x, (float)p2.y), D2D1::Point2F((float)p3.x, (float)p3.y), g_ctx.cropRectBrush, 2.0f);
            g_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p3.x, (float)p3.y), D2D1::Point2F((float)p4.x, (float)p4.y), g_ctx.cropRectBrush, 2.0f);
            g_ctx.renderTarget->DrawLine(D2D1::Point2F((float)p4.x, (float)p4.y), D2D1::Point2F((float)p1.x, (float)p1.y), g_ctx.cropRectBrush, 2.0f);
        }

        if (g_ctx.isCropPending && g_ctx.textFormat && g_ctx.textBrush) {
            D2D1_SIZE_F rtSize = g_ctx.renderTarget->GetSize();
            D2D1_RECT_F layoutRect = D2D1::RectF(
                0.0f,
                10.0f,
                rtSize.width,
                rtSize.height
            );

            D2D1_COLOR_F textColor;
            if (g_ctx.bgColor == BackgroundColor::White) {
                textColor = D2D1::ColorF(D2D1::ColorF::Black);
            }
            else {
                textColor = D2D1::ColorF(D2D1::ColorF::White);
            }
            g_ctx.textBrush->SetColor(textColor);

            g_ctx.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            g_ctx.renderTarget->DrawTextW(
                L"Press Enter to apply crop, Esc to cancel",
                40,
                g_ctx.textFormat,
                layoutRect,
                g_ctx.textBrush
            );
            g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }

    if (g_ctx.isOcrMessageVisible) {
        DrawOcrMessageOverlay(g_ctx.renderTarget);
    }

    HRESULT hr = g_ctx.renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
    }
}

void FitImageToWindow() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;

    RECT clientRect;
    GetClientRect(g_ctx.hWnd, &clientRect);
    if (IsRectEmpty(&clientRect)) return;

    float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
    float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
    float imageWidth = static_cast<float>(imgWidth);
    float imageHeight = static_cast<float>(imgHeight);

    if (g_ctx.rotationAngle == 90 || g_ctx.rotationAngle == 270) {
        std::swap(imageWidth, imageHeight);
    }

    if (imageWidth <= 0 || imageHeight <= 0) return;

    g_ctx.zoomFactor = std::min(clientWidth / imageWidth, clientHeight / imageHeight);
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void ZoomImage(float factor, POINT pt) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;

    RECT clientRect;
    GetClientRect(g_ctx.hWnd, &clientRect);
    float windowCenterX = (clientRect.right - clientRect.left) / 2.0f;
    float windowCenterY = (clientRect.bottom - clientRect.top) / 2.0f;

    float mouseXBeforeZoom = pt.x - (windowCenterX + g_ctx.offsetX);
    float mouseYBeforeZoom = pt.y - (windowCenterY + g_ctx.offsetY);

    float newZoomFactor = g_ctx.zoomFactor * factor;
    newZoomFactor = std::max(0.01f, std::min(100.0f, newZoomFactor));

    float mouseXAfterZoom = mouseXBeforeZoom * (newZoomFactor / g_ctx.zoomFactor);
    float mouseYAfterZoom = mouseYBeforeZoom * (newZoomFactor / g_ctx.zoomFactor);

    g_ctx.offsetX += (mouseXBeforeZoom - mouseXAfterZoom);
    g_ctx.offsetY += (mouseYBeforeZoom - mouseYAfterZoom);
    g_ctx.zoomFactor = newZoomFactor;

    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void RotateImage(bool clockwise) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;
    g_ctx.rotationAngle += clockwise ? 90 : -90;
    g_ctx.rotationAngle = (g_ctx.rotationAngle % 360 + 360) % 360;
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void FlipImage() {
    g_ctx.isFlippedHorizontal = !g_ctx.isFlippedHorizontal;
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void ConvertWindowToImagePoint(POINT pt, float& localX, float& localY) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        localX = 0; localY = 0;
        return;
    }

    RECT cr;
    GetClientRect(g_ctx.hWnd, &cr);
    float windowCenterX = (cr.right - cr.left) / 2.0f;
    float windowCenterY = (cr.bottom - cr.top) / 2.0f;

    float scaleX = g_ctx.isFlippedHorizontal ? -g_ctx.zoomFactor : g_ctx.zoomFactor;
    float scaleY = g_ctx.zoomFactor;
    if (scaleX == 0.0f) scaleX = 1.0f;
    if (scaleY == 0.0f) scaleY = 1.0f;

    float translatedX = pt.x - (windowCenterX + g_ctx.offsetX);
    float translatedY = pt.y - (windowCenterY + g_ctx.offsetY);

    float scaledX = translatedX / scaleX;
    float scaledY = translatedY / scaleY;

    double rad = -g_ctx.rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = static_cast<float>(cos(rad));
    float sinTheta = static_cast<float>(sin(rad));

    float unrotatedX = scaledX * cosTheta - scaledY * sinTheta;
    float unrotatedY = scaledX * sinTheta + scaledY * cosTheta;

    localX = unrotatedX + imgWidth / 2.0f;
    localY = unrotatedY + imgHeight / 2.0f;
}

void ConvertImageToWindowPoint(float localX, float localY, POINT& pt) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) {
        pt = { 0, 0 };
        return;
    }

    float unrotatedX = localX - imgWidth / 2.0f;
    float unrotatedY = localY - imgHeight / 2.0f;

    double rad = g_ctx.rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = static_cast<float>(cos(rad));
    float sinTheta = static_cast<float>(sin(rad));
    float scaledX = unrotatedX * cosTheta - unrotatedY * sinTheta;
    float scaledY = unrotatedX * sinTheta + unrotatedY * cosTheta;

    float scaleFactorX = g_ctx.isFlippedHorizontal ? -g_ctx.zoomFactor : g_ctx.zoomFactor;
    float scaleFactorY = g_ctx.zoomFactor;
    float translatedX = scaledX * scaleFactorX;
    float translatedY = scaledY * scaleFactorY;

    RECT cr;
    GetClientRect(g_ctx.hWnd, &cr);
    float windowCenterX = (cr.right - cr.left) / 2.0f;
    float windowCenterY = (cr.bottom - cr.top) / 2.0f;

    pt.x = static_cast<LONG>(translatedX + windowCenterX + g_ctx.offsetX);
    pt.y = static_cast<LONG>(translatedY + windowCenterY + g_ctx.offsetY);
}

bool IsPointInImage(POINT pt, const RECT& clientRect) {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return false;

    float localX, localY;
    ConvertWindowToImagePoint(pt, localX, localY);

    return localX >= 0 && localX < imgWidth && localY >= 0 && localY < imgHeight;
}