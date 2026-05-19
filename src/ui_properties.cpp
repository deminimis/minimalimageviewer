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

struct PropsWndData {
    ViewerApp* pApp;
    std::unique_ptr<ImageProperties> pProps;
};

LRESULT CALLBACK ViewerApp::PropsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
        PropsWndData* pData = reinterpret_cast<PropsWndData*>(pcs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pData));

        // Create the ListView control
        HWND hListView = CreateWindowExW(
            0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            0, 0, 0, 0,
            hWnd, (HMENU)1, pcs->hInstance, nullptr
        );

        // Set extended list view styles
        ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // Add columns
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.cx = 150;
        lvc.pszText = const_cast<LPWSTR>(L"Property");
        ListView_InsertColumn(hListView, 0, &lvc);

        lvc.cx = 400;
        lvc.pszText = const_cast<LPWSTR>(L"Value");
        ListView_InsertColumn(hListView, 1, &lvc);

        // Populate data
        if (pData && pData->pProps) {
            ImageProperties* pProps = pData->pProps.get();
            int row = 0;

            auto addRow = [&](const wchar_t* prop, const std::wstring& val) {
                if (val.empty() || val == L"N/A") return; 

                LVITEMW lvi = { 0 };
                lvi.mask = LVIF_TEXT;
                lvi.iItem = row;
                lvi.iSubItem = 0;
                lvi.pszText = const_cast<LPWSTR>(prop);
                ListView_InsertItem(hListView, &lvi);

                ListView_SetItemText(hListView, row, 1, const_cast<LPWSTR>(val.c_str()));
                row++;
                };

            addRow(L"File Path", pProps->filePath);
            addRow(L"Image Format", pProps->imageFormat);
            addRow(L"Dimensions", pProps->dimensions);
            addRow(L"Orientation", pProps->orientation);
            addRow(L"Bit Depth", pProps->bitDepth);
            addRow(L"DPI", pProps->dpi);
            addRow(L"File Size", pProps->fileSize);
            addRow(L"Attributes", pProps->attributes);
            addRow(L"Created", pProps->createdDate);
            addRow(L"Modified", pProps->modifiedDate);
            addRow(L"Accessed", pProps->accessedDate);
            addRow(L"Camera Make", pProps->cameraMake);
            addRow(L"Camera Model", pProps->cameraModel);
            addRow(L"Date Taken", pProps->dateTaken);
            addRow(L"F-stop", pProps->fStop);
            addRow(L"Exposure", pProps->exposureTime);
            addRow(L"ISO", pProps->iso);
            addRow(L"Focal Length", pProps->focalLength);
            addRow(L"35mm Focal Length", pProps->focalLength35mm);
            addRow(L"Exposure Bias", pProps->exposureBias);
            addRow(L"Exposure Program", pProps->exposureProgram);
            addRow(L"White Balance", pProps->whiteBalance);
            addRow(L"Metering Mode", pProps->meteringMode);
            addRow(L"Flash", pProps->flash);
            addRow(L"Lens Model", pProps->lensModel);
            addRow(L"Author", pProps->author);
            addRow(L"Copyright", pProps->copyright);
            addRow(L"Software", pProps->software);
        }
        break;
    }
    case WM_SIZE: {
        HWND hListView = GetDlgItem(hWnd, 1);
        if (hListView) {
            MoveWindow(hListView, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY: {
        PropsWndData* pData = reinterpret_cast<PropsWndData*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        if (pData) {
            pData->pApp->m_ctx.hPropsWnd = nullptr;
            std::unique_ptr<PropsWndData> cleanup(pData);
        }
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void ViewerApp::ShowImageProperties() {
    if (m_ctx.hPropsWnd) {
        SetForegroundWindow(m_ctx.hPropsWnd);
        return;
    }

    if (m_ctx.currentImageIndex < 0 || m_ctx.currentImageIndex >= static_cast<int>(m_ctx.imageFiles.size())) {
        return;
    }

    auto pData = std::make_unique<PropsWndData>();
    pData->pApp = this;
    pData->pProps = std::make_unique<ImageProperties>(GetCurrentOsdProperties());

    static const wchar_t* PROPS_CLASS_NAME = L"MinimalImageViewerProperties";
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = PropsWndProc;
        wcex.hInstance = m_ctx.hInst;
        wcex.hIcon = LoadIcon(m_ctx.hInst, MAKEINTRESOURCE(IDI_APPICON));
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = PROPS_CLASS_NAME;
        if (RegisterClassExW(&wcex)) {
            classRegistered = true;
        }
    }

    RECT rcParent = {};
    GetWindowRect(m_ctx.hWnd, &rcParent);
    int wndWidth = 600;
    int wndHeight = 700;
    int xPos = rcParent.left + (rcParent.right - rcParent.left - wndWidth) / 2;
    int yPos = rcParent.top + (rcParent.bottom - rcParent.top - wndHeight) / 2;

    m_ctx.hPropsWnd = CreateWindowW(
        PROPS_CLASS_NAME,
        L"Image Properties",
        (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU) & ~WS_THICKFRAME,
        xPos, yPos, wndWidth, wndHeight,
        m_ctx.hWnd,
        nullptr,
        m_ctx.hInst,
        pData.get() 
    );

    if (m_ctx.hPropsWnd) {
        ShowWindow(m_ctx.hPropsWnd, SW_SHOW);
        pData.release(); 
    }
}