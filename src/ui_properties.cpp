#include "viewer.h"
#include "exif_utils.h"
#include <propkey.h>
#include <string>
#include <stdio.h>
#include <memory>
#include <commctrl.h>



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
    wchar_t szSize[64];
    StrFormatByteSizeW(fileSize.QuadPart, szSize, ARRAYSIZE(szSize));

    wchar_t buffer[256];
    if (fileSize.QuadPart >= 1024) {
        swprintf_s(buffer, ARRAYSIZE(buffer), L"%s (%llu Bytes)", szSize, fileSize.QuadPart);
        return buffer;
    }
    return szSize;
}

ImageProperties ViewerApp::GetCurrentOsdProperties() {
    ImageProperties pProps = {};
    if (m_ctx.currentImageIndex < 0 || m_ctx.currentImageIndex >= static_cast<int>(m_ctx.imageFiles.size())) {
        if (!m_ctx.currentFilePathOverride.empty()) pProps.filePath = m_ctx.currentFilePathOverride;
        return pProps;
    }

    pProps.filePath = m_ctx.imageFiles[m_ctx.currentImageIndex];
    UINT w = 0, h = 0;
    if (GetCurrentImageSize(&w, &h)) pProps.dimensions = std::to_wstring(w) + L" x " + std::to_wstring(h) + L" pixels";

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(pProps.filePath.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER fs; fs.HighPart = fad.nFileSizeHigh; fs.LowPart = fad.nFileSizeLow;
        pProps.fileSize = FormatFileSize(fs);
        pProps.createdDate = FormatFileTime(fad.ftCreationTime);
        pProps.modifiedDate = FormatFileTime(fad.ftLastWriteTime);
        pProps.accessedDate = FormatFileTime(fad.ftLastAccessTime);
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) pProps.attributes += L"Read-only; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) pProps.attributes += L"Hidden; ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) pProps.attributes += L"System; ";
        if (pProps.attributes.empty()) pProps.attributes = L"Normal";
    }
    else {
        pProps.attributes = L"N/A";
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(CreateDecoderFromFile(pProps.filePath.c_str(), &decoder))) {
        GUID fmt;
        if (SUCCEEDED(decoder->GetContainerFormat(&fmt))) {
            pProps.imageFormat = GetContainerFormatName(fmt) + (m_ctx.isAnimated ? L" (Animated)" : L"");
        }
        ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
            pProps.bitDepth = GetBitDepth(frame.Get(), m_ctx.wicFactory.Get());
            double dpiX, dpiY;
            if (SUCCEEDED(frame->GetResolution(&dpiX, &dpiY))) pProps.dpi = std::to_wstring(static_cast<int>(dpiX + 0.5)) + L" x " + std::to_wstring(static_cast<int>(dpiY + 0.5)) + L" DPI";
            ComPtr<IPropertyStore> pStore;
            if (SUCCEEDED(SHGetPropertyStoreFromParsingName(pProps.filePath.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&pStore)))) {
                auto getProp = [&](REFPROPERTYKEY key) { return GetPropertyString(pStore.Get(), key); };

                pProps.dateTaken = getProp(PKEY_Photo_DateTaken);
                pProps.orientation = getProp(PKEY_Photo_Orientation);
                pProps.cameraMake = getProp(PKEY_Photo_CameraManufacturer);
                pProps.cameraModel = getProp(PKEY_Photo_CameraModel);
                pProps.fStop = getProp(PKEY_Photo_FNumber);
                pProps.exposureTime = getProp(PKEY_Photo_ExposureTime);
                pProps.iso = getProp(PKEY_Photo_ISOSpeed);
                pProps.software = getProp(PKEY_SoftwareUsed);
                pProps.focalLength = getProp(PKEY_Photo_FocalLength);
                pProps.focalLength35mm = getProp(PKEY_Photo_FocalLengthInFilm);
                pProps.exposureBias = getProp(PKEY_Photo_ExposureBias);
                pProps.meteringMode = getProp(PKEY_Photo_MeteringMode);
                pProps.flash = getProp(PKEY_Photo_Flash);
                pProps.exposureProgram = getProp(PKEY_Photo_ExposureProgram);
                pProps.whiteBalance = getProp(PKEY_Photo_WhiteBalance);
                pProps.author = getProp(PKEY_Author);
                pProps.copyright = getProp(PKEY_Copyright);
                pProps.lensModel = getProp(PKEY_Photo_LensModel);
            }
        }
    }
    return pProps;
}

void ViewerApp::ShowImageProperties() {
    if (m_ctx.currentImageIndex < 0 || m_ctx.currentImageIndex >= static_cast<int>(m_ctx.imageFiles.size())) {
        return;
    }

    std::wstring filePath = m_ctx.imageFiles[m_ctx.currentImageIndex];

    // Windows property sheet
    SHObjectProperties(m_ctx.hWnd, SHOP_FILEPATH, filePath.c_str(), L"Details");
}