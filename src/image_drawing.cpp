#include "viewer.h"

extern AppContext g_ctx;

void DrawImage(HDC hdc, const RECT& clientRect, const AppContext& ctx) {
    if (!ctx.hBitmap || IsRectEmpty(&clientRect)) return;

    BITMAP bm;
    GetObject(ctx.hBitmap, sizeof(BITMAP), &bm);
    HDC memDC = CreateCompatibleDC(hdc);
    SelectObject(memDC, ctx.hBitmap);

    int srcWidth = bm.bmWidth;
    int srcHeight = bm.bmHeight;
    int clientWidth = clientRect.right - clientRect.left;
    int clientHeight = clientRect.bottom - clientRect.top;

    SetGraphicsMode(hdc, GM_ADVANCED);
    
    double rad = ctx.rotationAngle * 3.1415926535 / 180.0;
    float cosTheta = static_cast<float>(cos(rad));
    float sinTheta = static_cast<float>(sin(rad));

    XFORM xform;
    xform.eM11 = cosTheta * ctx.zoomFactor;
    xform.eM12 = sinTheta * ctx.zoomFactor;
    xform.eM21 = -sinTheta * ctx.zoomFactor;
    xform.eM22 = cosTheta * ctx.zoomFactor;
    xform.eDx = static_cast<float>(clientWidth) / 2.0f + ctx.offsetX - (srcWidth / 2.0f * xform.eM11 + srcHeight / 2.0f * xform.eM21);
    xform.eDy = static_cast<float>(clientHeight) / 2.0f + ctx.offsetY - (srcWidth / 2.0f * xform.eM12 + srcHeight / 2.0f * xform.eM22);

    SetWorldTransform(hdc, &xform);

    SetStretchBltMode(hdc, HALFTONE);
    BitBlt(hdc, 0, 0, srcWidth, srcHeight, memDC, 0, 0, SRCCOPY);

    ModifyWorldTransform(hdc, nullptr, MWT_IDENTITY);
    SetGraphicsMode(hdc, GM_COMPATIBLE);
    DeleteDC(memDC);
}

void FitImageToWindow() {
    if (!g_ctx.hBitmap) return;

    RECT clientRect;
    GetClientRect(g_ctx.hWnd, &clientRect);
    if (IsRectEmpty(&clientRect)) return;

    BITMAP bm;
    GetObject(g_ctx.hBitmap, sizeof(BITMAP), &bm);
    
    float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
    float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
    float imageWidth = static_cast<float>(bm.bmWidth);
    float imageHeight = static_cast<float>(bm.bmHeight);

    if (g_ctx.rotationAngle == 90 || g_ctx.rotationAngle == 270) {
        std::swap(imageWidth, imageHeight);
    }

    if (imageWidth <= 0 || imageHeight <= 0) return;

    g_ctx.zoomFactor = std::min(clientWidth / imageWidth, clientHeight / imageHeight);
    g_ctx.offsetX = 0.0f;
    g_ctx.offsetY = 0.0f;
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void ZoomImage(float factor) {
    if (!g_ctx.hBitmap) return;
    g_ctx.zoomFactor *= factor;
    g_ctx.zoomFactor = std::max(0.01f, std::min(100.0f, g_ctx.zoomFactor));
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

void RotateImage(bool clockwise) {
    if (!g_ctx.hBitmap) return;
    g_ctx.rotationAngle += clockwise ? 90 : -90;
    g_ctx.rotationAngle = (g_ctx.rotationAngle % 360 + 360) % 360;
    InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
}

bool IsPointInImage(POINT pt, const RECT& clientRect) {
    if (!g_ctx.hBitmap) return false;

    BITMAP bm;
    GetObject(g_ctx.hBitmap, sizeof(BITMAP), &bm);
    
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

    float localX = unrotatedX + bm.bmWidth / 2.0f;
    float localY = unrotatedY + bm.bmHeight / 2.0f;

    return localX >= 0 && localX < bm.bmWidth && localY >= 0 && localY < bm.bmHeight;
}