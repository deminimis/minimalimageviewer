#include "viewer.h"
#include <d2d1helper.h>

extern AppContext g_ctx;

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
            hr = g_ctx.writeFactory->CreateTextFormat(
                L"Segoe UI",
                NULL,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                18.0f,
                L"en-us",
                &g_ctx.textFormat
            );
        }
        if (SUCCEEDED(hr)) {
            g_ctx.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            g_ctx.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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
    std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
    g_ctx.renderTarget = nullptr;
    g_ctx.d2dBitmap = nullptr;
    g_ctx.textBrush = nullptr;
    g_ctx.textFormat = nullptr;
    g_ctx.checkerboardBrush = nullptr;
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
        g_ctx.renderTarget->DrawTextW(
            L"Loading...",
            10,
            g_ctx.textFormat,
            layoutRect,
            g_ctx.textBrush
        );
    }
    else {
        ComPtr<ID2D1Bitmap> bitmapToDraw;
        ComPtr<IWICFormatConverter> converterToUse;
        {
            std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
            if (!g_ctx.d2dBitmap && g_ctx.wicConverter) {
                g_ctx.renderTarget->CreateBitmapFromWicBitmap(
                    g_ctx.wicConverter,
                    nullptr,
                    &g_ctx.d2dBitmap
                );
            }
            bitmapToDraw = g_ctx.d2dBitmap;
            converterToUse = g_ctx.wicConverter;
        }

        if (bitmapToDraw && !IsIconic(g_ctx.hWnd)) {
            D2D1_SIZE_F bmpSize = bitmapToDraw->GetSize();
            D2D1_SIZE_F rtSize = g_ctx.renderTarget->GetSize();
            D2D1_POINT_2F bmpCenter = D2D1::Point2F(bmpSize.width / 2.f, bmpSize.height / 2.f);
            D2D1_POINT_2F windowCenter = D2D1::Point2F(rtSize.width / 2.f, rtSize.height / 2.f);

            g_ctx.renderTarget->SetTransform(
                D2D1::Matrix3x2F::Rotation(static_cast<float>(g_ctx.rotationAngle), bmpCenter) *
                D2D1::Matrix3x2F::Scale(g_ctx.zoomFactor, g_ctx.zoomFactor, bmpCenter) *
                D2D1::Matrix3x2F::Translation(windowCenter.x - bmpCenter.x + g_ctx.offsetX, windowCenter.y - bmpCenter.y + g_ctx.offsetY)
            );

            g_ctx.renderTarget->DrawBitmap(
                bitmapToDraw,
                nullptr,
                1.0f,
                g_ctx.zoomFactor < 1.0f ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            );
        }
        else if (!converterToUse && g_ctx.textFormat && g_ctx.textBrush) {
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
            g_ctx.renderTarget->DrawTextW(
                L"Right-click for options or drag an image here",
                47,
                g_ctx.textFormat,
                layoutRect,
                g_ctx.textBrush
            );
        }
    }

    HRESULT hr = g_ctx.renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
    }
}

void FitImageToWindow() {
    std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
    if (!g_ctx.wicConverter) return;

    RECT clientRect;
    GetClientRect(g_ctx.hWnd, &clientRect);
    if (IsRectEmpty(&clientRect)) return;

    UINT imgWidth, imgHeight;
    g_ctx.wicConverter->GetSize(&imgWidth, &imgHeight);

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
    std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
    if (!g_ctx.wicConverter) return;

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
    std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
    if (!g_ctx.wicConverter) return;
    g_ctx.rotationAngle += clockwise ? 90 : -90;
    g_ctx.rotationAngle = (g_ctx.rotationAngle % 360 + 360) % 360;
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

bool IsPointInImage(POINT pt, const RECT& clientRect) {
    std::lock_guard<std::mutex> lock(g_ctx.wicMutex);
    if (!g_ctx.wicConverter) return false;

    UINT imgWidth, imgHeight;
    g_ctx.wicConverter->GetSize(&imgWidth, &imgHeight);

    RECT cr;
    GetClientRect(g_ctx.hWnd, &cr);

    float windowCenterX = (cr.right - cr.left) / 2.0f;
    float windowCenterY = (cr.bottom - cr.top) / 2.0f;

    float translatedX = pt.x - (windowCenterX + g_ctx.offsetX);
    float translatedY = pt.y - (windowCenterY + g_ctx.offsetY);

    float scaledX = translatedX / g_ctx.zoomFactor;
    float scaledY = translatedY / g_ctx.zoomFactor;

    double rad = -g_ctx.rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = static_cast<float>(cos(rad));
    float sinTheta = static_cast<float>(sin(rad));

    float unrotatedX = scaledX * cosTheta - scaledY * sinTheta;
    float unrotatedY = scaledX * sinTheta + scaledY * cosTheta;

    float localX = unrotatedX + imgWidth / 2.0f;
    float localY = unrotatedY + imgHeight / 2.0f;

    return localX >= 0 && localX < imgWidth && localY >= 0 && localY < imgHeight;
}