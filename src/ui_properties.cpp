#include "viewer.h"
#include "exif_utils.h"
#include <string>
#include <stdio.h>

extern AppContext g_ctx;

static std::wstring FormatFileTime(const FILETIME& ft) {
    SYSTEMTIME stUTC = {}, stLocal = {};
    if (!FileTimeToSystemTime(&ft, &stUTC)) {
        return L"N/A";
    }
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &stUTC, &stLocal)) {
        return L"N/A";
    }

    wchar_t szDate[256], szTime[256], szDateTime[512];
    GetDateFormatW(LOCALE_USER_DEFAULT, 0, &stLocal, nullptr, szDate, 256);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &stLocal, nullptr, szTime, 256);
    swprintf_s(szDateTime, 512, L"%s  %s", szDate, szTime);
    return szDateTime;
}

static std::wstring FormatFileSize(const LARGE_INTEGER& fileSize) {
    double size = static_cast<double>(fileSize.QuadPart);
    const wchar_t* unit = L"Bytes";
    wchar_t buffer[256];

    if (size >= 1024.0 * 1024.0 * 1024.0) {
        size /= (1024.0 * 1024.0 * 1024.0);
        unit = L"GB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else if (size >= 1024.0 * 1024.0) {
        size /= (1024.0 * 1024.0);
        unit = L"MB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else if (size >= 1024.0) {
        size /= 1024.0;
        unit = L"KB";
        swprintf_s(buffer, 256, L"%.2f %s (%llu Bytes)", size, unit, fileSize.QuadPart);
    }
    else {
        swprintf_s(buffer, 256, L"%llu Bytes", fileSize.QuadPart);
    }
    return buffer;
}

ImageProperties GetCurrentOsdProperties() {
    ImageProperties pProps = {};

    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        if (!g_ctx.currentFilePathOverride.empty()) {
            pProps.filePath = g_ctx.currentFilePathOverride;
        }
        return pProps;
    }

    const std::wstring& filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];
    pProps.filePath = filePath;

    UINT imgWidth = 0, imgHeight = 0;
    if (GetCurrentImageSize(&imgWidth, &imgHeight)) {
        pProps.dimensions = std::to_wstring(imgWidth) + L" x " + std::to_wstring(imgHeight) + L" pixels";
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER fileSize;
        fileSize.HighPart = fad.nFileSizeHigh;
        fileSize.LowPart = fad.nFileSizeLow;
        pProps.fileSize = FormatFileSize(fileSize);

        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) pProps.attributes += L"Read-only; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) pProps.attributes += L"Hidden; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) pProps.attributes += L"System; ";
        if (pProps.attributes.empty()) pProps.attributes = L"Normal";

    }

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> metadataReader;

    if (SUCCEEDED(CreateDecoderFromFile(filePath.c_str(), &decoder))) {
        GUID containerFormat;
        if (SUCCEEDED(decoder->GetContainerFormat(&containerFormat))) {
            pProps.imageFormat = GetContainerFormatName(containerFormat);
            if (g_ctx.isAnimated) {
                pProps.imageFormat += L" (Animated)";
            }
        }

        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {

            pProps.bitDepth = GetBitDepth(frame);

            double dpiX, dpiY;
            if (SUCCEEDED(frame->GetResolution(&dpiX, &dpiY))) {
                pProps.dpi = std::to_wstring(static_cast<int>(dpiX + 0.5)) + L" x " + std::to_wstring(static_cast<int>(dpiY + 0.5)) + L" DPI";
            }

            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                pProps.fStop = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33437}");
                pProps.exposureTime = GetMetadataString(metadataReader, L"/app1/ifd/exif/{rational=33434}");
                pProps.iso = GetMetadataString(metadataReader, L"/app1/ifd/exif/{ushort=34855}");
                pProps.software = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=305}");
                pProps.author = GetMetadataString(metadataReader, L"/app1/ifd/{ushort=315}");
            }
        }
    }
    return pProps;
}

LRESULT CALLBACK PropsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
        ImageProperties* pProps = (ImageProperties*)pcs->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pProps);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        ImageProperties* pProps = (ImageProperties*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

        if (pProps) {
            SetBkMode(hdc, TRANSPARENT);
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SelectObject(hdc, hFont);

            int y = 15;
            int x_label = 15;
            int x_value = 160;
            int y_step = 20;

            auto drawProp = [&](const wchar_t* label, const std::wstring& val, bool extraSpace = false) {
                if (extraSpace) y += y_step / 2;
                TextOutW(hdc, x_label, y, label, (int)wcslen(label));
                TextOutW(hdc, x_value, y, val.c_str(), (int)val.length());
                y += y_step;
                };

            drawProp(L"File Path:", pProps->filePath);
            drawProp(L"Image Format:", pProps->imageFormat, true);
            drawProp(L"Dimensions:", pProps->dimensions);
            drawProp(L"Bit Depth:", pProps->bitDepth);
            drawProp(L"DPI:", pProps->dpi);

            drawProp(L"File Size:", pProps->fileSize, true);
            drawProp(L"Attributes:", pProps->attributes);
            drawProp(L"Created:", pProps->createdDate);
            drawProp(L"Modified:", pProps->modifiedDate);
            drawProp(L"Accessed:", pProps->accessedDate);

            drawProp(L"Camera Make:", pProps->cameraMake, true);
            drawProp(L"Camera Model:", pProps->cameraModel);
            drawProp(L"Date Taken:", pProps->dateTaken);
            drawProp(L"F-stop:", pProps->fStop);
            drawProp(L"Exposure:", pProps->exposureTime);
            drawProp(L"ISO:", pProps->iso);
            drawProp(L"Focal Length:", pProps->focalLength);
            drawProp(L"35mm Focal Length:", pProps->focalLength35mm);
            drawProp(L"Exposure Bias:", pProps->exposureBias);
            drawProp(L"Exposure Program:", pProps->exposureProgram);
            drawProp(L"White Balance:", pProps->whiteBalance);
            drawProp(L"Metering Mode:", pProps->meteringMode);
            drawProp(L"Flash:", pProps->flash);
            drawProp(L"Lens Model:", pProps->lensModel);

            drawProp(L"Author:", pProps->author, true);
            drawProp(L"Copyright:", pProps->copyright);
            drawProp(L"Software:", pProps->software);
        }
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY: {
        ImageProperties* pProps = (ImageProperties*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        delete pProps;
        g_ctx.hPropsWnd = nullptr;
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void ShowImageProperties() {
    if (g_ctx.hPropsWnd) {
        SetForegroundWindow(g_ctx.hPropsWnd);
        return;
    }

    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        return;
    }

    const std::wstring& filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];

    ImageProperties* pProps = new ImageProperties();

    pProps->filePath = filePath;

    UINT imgWidth = 0, imgHeight = 0;
    if (GetCurrentImageSize(&imgWidth, &imgHeight)) {
        pProps->dimensions = std::to_wstring(imgWidth) + L" x " + std::to_wstring(imgHeight) + L" pixels";
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER fileSize;
        fileSize.HighPart = fad.nFileSizeHigh;
        fileSize.LowPart = fad.nFileSizeLow;
        pProps->fileSize = FormatFileSize(fileSize);
        pProps->createdDate = FormatFileTime(fad.ftCreationTime);
        pProps->modifiedDate = FormatFileTime(fad.ftLastWriteTime);
        pProps->accessedDate = FormatFileTime(fad.ftLastAccessTime);

        // FIX: Changed dots (.) to arrows (->) below
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) pProps->attributes += L"Read-only; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) pProps->attributes += L"Hidden; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) pProps->attributes += L"System; ";
        if (pProps->attributes.empty()) pProps->attributes = L"Normal";
    }
    else {
        pProps->fileSize = L"N/A";
        pProps->createdDate = L"N/A";
        pProps->modifiedDate = L"N/A";
        pProps->accessedDate = L"N/A";
        pProps->attributes = L"N/A";
    }

    // Initialize defaults
    pProps->imageFormat = L"N/A";
    pProps->bitDepth = L"N/A";
    pProps->dpi = L"N/A";
    pProps->cameraMake = L"N/A";
    pProps->cameraModel = L"N/A";
    pProps->dateTaken = L"N/A";
    pProps->fStop = L"N/A";
    pProps->exposureTime = L"N/A";
    pProps->iso = L"N/A";
    pProps->software = L"N/A";
    pProps->focalLength = L"N/A";
    pProps->focalLength35mm = L"N/A";
    pProps->exposureBias = L"N/A";
    pProps->meteringMode = L"N/A";
    pProps->flash = L"N/A";
    pProps->exposureProgram = L"N/A";
    pProps->whiteBalance = L"N/A";
    pProps->author = L"N/A";
    pProps->copyright = L"N/A";
    pProps->lensModel = L"N/A";

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> metadataReader;

    if (SUCCEEDED(CreateDecoderFromFile(filePath.c_str(), &decoder))) {
        GUID containerFormat;
        if (SUCCEEDED(decoder->GetContainerFormat(&containerFormat))) {
            pProps->imageFormat = GetContainerFormatName(containerFormat);
            if (g_ctx.isAnimated) {
                pProps->imageFormat += L" (Animated)";
            }
        }

        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
            pProps->bitDepth = GetBitDepth(frame);

            double dpiX, dpiY;
            if (SUCCEEDED(frame->GetResolution(&dpiX, &dpiY))) {
                pProps->dpi = std::to_wstring(static_cast<int>(dpiX + 0.5)) + L" x " + std::to_wstring(static_cast<int>(dpiY + 0.5)) + L" DPI";
            }

            if (SUCCEEDED(frame->GetMetadataQueryReader(&metadataReader))) {
                auto getMeta = [&](const wchar_t* query) { return GetMetadataString(metadataReader, query); };
                pProps->dateTaken = getMeta(L"/app1/ifd/exif/{ushort=36867}");
                pProps->cameraMake = getMeta(L"/app1/ifd/{ushort=271}");
                pProps->cameraModel = getMeta(L"/app1/ifd/{ushort=272}");
                pProps->fStop = getMeta(L"/app1/ifd/exif/{rational=33437}");
                pProps->exposureTime = getMeta(L"/app1/ifd/exif/{rational=33434}");
                pProps->iso = getMeta(L"/app1/ifd/exif/{ushort=34855}");
                pProps->software = getMeta(L"/app1/ifd/{ushort=305}");
                pProps->focalLength = getMeta(L"/app1/ifd/exif/{rational=37386}");
                pProps->focalLength35mm = getMeta(L"/app1/ifd/exif/{ushort=41989}");
                pProps->exposureBias = getMeta(L"/app1/ifd/exif/{srational=37380}");
                pProps->meteringMode = getMeta(L"/app1/ifd/exif/{ushort=37383}");
                pProps->flash = getMeta(L"/app1/ifd/exif/{ushort=37385}");
                pProps->exposureProgram = getMeta(L"/app1/ifd/exif/{ushort=34850}");
                pProps->whiteBalance = getMeta(L"/app1/ifd/exif/{ushort=41987}");
                pProps->author = getMeta(L"/app1/ifd/{ushort=315}");
                pProps->copyright = getMeta(L"/app1/ifd/{ushort=33432}");
                pProps->lensModel = getMeta(L"/app1/ifd/exif/{ushort=42036}");
            }
        }
    }

    static const wchar_t* PROPS_CLASS_NAME = L"MinimalImageViewerProperties";
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = PropsWndProc;
        wcex.hInstance = g_ctx.hInst;
        wcex.hIcon = LoadIcon(g_ctx.hInst, MAKEINTRESOURCE(IDI_APPICON));
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = PROPS_CLASS_NAME;
        if (RegisterClassExW(&wcex)) {
            classRegistered = true;
        }
    }

    RECT rcParent = {};
    GetWindowRect(g_ctx.hWnd, &rcParent);
    int wndWidth = 600;
    int wndHeight = 700;
    int xPos = rcParent.left + (rcParent.right - rcParent.left - wndWidth) / 2;
    int yPos = rcParent.top + (rcParent.bottom - rcParent.top - wndHeight) / 2;

    g_ctx.hPropsWnd = CreateWindowW(
        PROPS_CLASS_NAME,
        L"Image Properties",
        (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU) & ~WS_THICKFRAME,
        xPos, yPos, wndWidth, wndHeight,
        g_ctx.hWnd,
        nullptr,
        g_ctx.hInst,
        pProps
    );

    if (g_ctx.hPropsWnd) {
        ShowWindow(g_ctx.hPropsWnd, SW_SHOW);
    }
    else {
        delete pProps;
    }
}