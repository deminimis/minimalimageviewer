#include "viewer.h"

extern AppContext g_ctx;

static ComPtr<IWICBitmapSource> GetSaveSource(const GUID& targetFormat) {
    CriticalSectionLock lock(g_ctx.wicMutex);
    ComPtr<IWICBitmapSource> source;

    if (g_ctx.isAnimated && g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size()) {
        source = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
    }
    else if (g_ctx.wicConverter) {
        source = g_ctx.wicConverter;
    }
    else {
        return nullptr;
    }

    if (g_ctx.rotationAngle != 0 || g_ctx.isFlippedHorizontal) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (SUCCEEDED(g_ctx.wicFactory->CreateBitmapFlipRotator(&rotator))) {
            WICBitmapTransformOptions options = WICBitmapTransformRotate0;
            switch (g_ctx.rotationAngle) {
            case 90:  options = WICBitmapTransformRotate90;  break;
            case 180: options = WICBitmapTransformRotate180; break;
            case 270: options = WICBitmapTransformRotate270; break;
            }
            if (g_ctx.isFlippedHorizontal) {
                options = static_cast<WICBitmapTransformOptions>(options | WICBitmapTransformFlipHorizontal);
            }

            if (SUCCEEDED(rotator->Initialize(source, options))) {
                source = rotator;
            }
        }
    }

    if (g_ctx.isCropActive) {
        ComPtr<IWICBitmapClipper> clipper;
        if (SUCCEEDED(g_ctx.wicFactory->CreateBitmapClipper(&clipper))) {
            WICRect rc;
            rc.X = static_cast<INT>(floor(g_ctx.cropRectLocal.left));
            rc.Y = static_cast<INT>(floor(g_ctx.cropRectLocal.top));
            rc.Width = static_cast<INT>(ceil(g_ctx.cropRectLocal.right)) - rc.X;
            rc.Height = static_cast<INT>(ceil(g_ctx.cropRectLocal.bottom)) - rc.Y;

            if (rc.Width > 0 && rc.Height > 0) {
                if (SUCCEEDED(clipper->Initialize(source, &rc))) {
                    source = clipper;
                }
            }
        }
    }

    if (g_ctx.isGrayscale) {
        ComPtr<IWICFormatConverter> grayConverter;
        if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&grayConverter))) {
            if (SUCCEEDED(grayConverter->Initialize(source, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                source = grayConverter;
            }
        }
    }

    WICPixelFormatGUID sourcePixelFormat{};
    if (FAILED(source->GetPixelFormat(&sourcePixelFormat))) return nullptr;

    if (targetFormat == GUID_ContainerFormatJpeg && sourcePixelFormat != GUID_WICPixelFormat24bppBGR) {
        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&converter))) {
            if (SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut))) {
                source = converter;
            }
        }
    }
    return source;
}

void SaveImageAs() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;

    wchar_t szFile[MAX_PATH] = L"Untitled.png";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = g_ctx.hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"PNG File (*.png)\0*.png\0JPEG File (*.jpg)\0*.jpg\0BMP File (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;

    GUID containerFormat = GUID_ContainerFormatPng;
    const wchar_t* ext = PathFindExtensionW(ofn.lpstrFile);
    if (ext) {
        if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) containerFormat = GUID_ContainerFormatJpeg;
        else if (_wcsicmp(ext, L".bmp") == 0) containerFormat = GUID_ContainerFormatBmp;
    }

    ComPtr<IWICBitmapSource> source = GetSaveSource(containerFormat);
    if (!source) {
        MessageBoxW(g_ctx.hWnd, L"Could not get image source to save.", L"Save Error", MB_ICONERROR);
        return;
    }

    HRESULT hr = E_FAIL;
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;

        hr = g_ctx.wicFactory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(ofn.lpstrFile, GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = g_ctx.wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
        if (SUCCEEDED(hr)) hr = frame->Initialize(props);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(source, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
    }

    if (SUCCEEDED(hr)) {
        LoadImageFromFile(ofn.lpstrFile);
    }
    else {
        MessageBoxW(g_ctx.hWnd, L"Failed to save image.", L"Save As Error", MB_ICONERROR);
    }
}

void SaveImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
        UINT imgWidth, imgHeight;
        if (GetCurrentImageSize(&imgWidth, &imgHeight)) {
            SaveImageAs();
        }
        return;
    }

    const auto& originalPath = g_ctx.imageFiles[g_ctx.currentImageIndex];

    if (g_ctx.rotationAngle == 0 && !g_ctx.isFlippedHorizontal && !g_ctx.isCropActive && !g_ctx.isGrayscale) {
        MessageBoxW(g_ctx.hWnd, L"No changes to save.", L"Save", MB_OK | MB_ICONINFORMATION);
        return;
    }

    GUID containerFormat{};
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        containerFormat = g_ctx.originalContainerFormat;
    }

    if (containerFormat == GUID_NULL) {
        MessageBoxW(g_ctx.hWnd, L"Could not determine original file format. Use 'Save As'.", L"Save Error", MB_ICONERROR);
        return;
    }

    ComPtr<IWICBitmapSource> source = GetSaveSource(containerFormat);
    if (!source) {
        MessageBoxW(g_ctx.hWnd, L"Could not get image source to save.", L"Save Error", MB_ICONERROR);
        return;
    }

    std::wstring tempPath = originalPath + L".tmp_save";
    HRESULT hr = E_FAIL;
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;

        hr = g_ctx.wicFactory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(tempPath.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = g_ctx.wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(source, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
    }

    if (SUCCEEDED(hr)) {
        if (ReplaceFileW(originalPath.c_str(), tempPath.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            LoadImageFromFile(originalPath.c_str());
            g_ctx.rotationAngle = 0;
            g_ctx.isFlippedHorizontal = false;
            InvalidateRect(g_ctx.hWnd, NULL, FALSE);
        }
        else {
            DeleteFileW(tempPath.c_str());
            MessageBoxW(g_ctx.hWnd, L"Failed to replace the original file.", L"Save Error", MB_ICONERROR);
        }
    }
    else {
        DeleteFileW(tempPath.c_str());
        MessageBoxW(g_ctx.hWnd, L"Failed to save image to temporary file.", L"Save Error", MB_ICONERROR);
    }
}

static void SaveImageWithResize(const std::wstring& filePath, const GUID& containerFormat, UINT newWidth, UINT newHeight) {
    ComPtr<IWICBitmapSource> source;
    {
        CriticalSectionLock lock(g_ctx.wicMutex);
        if (g_ctx.isAnimated && g_ctx.currentAnimationFrame < g_ctx.animationFrameConverters.size()) {
            source = g_ctx.animationFrameConverters[g_ctx.currentAnimationFrame];
        }
        else if (g_ctx.wicConverter) {
            source = g_ctx.wicConverter;
        }
        else {
            MessageBoxW(g_ctx.hWnd, L"Could not get image source to resize.", L"Resize Error", MB_ICONERROR);
            return;
        }
    }

    if (g_ctx.rotationAngle != 0 || g_ctx.isFlippedHorizontal) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (SUCCEEDED(g_ctx.wicFactory->CreateBitmapFlipRotator(&rotator))) {
            WICBitmapTransformOptions options = WICBitmapTransformRotate0;
            switch (g_ctx.rotationAngle) {
            case 90:  options = WICBitmapTransformRotate90;  break;
            case 180: options = WICBitmapTransformRotate180; break;
            case 270: options = WICBitmapTransformRotate270; break;
            }
            if (g_ctx.isFlippedHorizontal) {
                options = static_cast<WICBitmapTransformOptions>(options | WICBitmapTransformFlipHorizontal);
            }
            if (SUCCEEDED(rotator->Initialize(source, options))) {
                source = rotator;
            }
        }
    }

    if (g_ctx.isCropActive) {
        ComPtr<IWICBitmapClipper> clipper;
        if (SUCCEEDED(g_ctx.wicFactory->CreateBitmapClipper(&clipper))) {
            WICRect rc;
            rc.X = static_cast<INT>(floor(g_ctx.cropRectLocal.left));
            rc.Y = static_cast<INT>(floor(g_ctx.cropRectLocal.top));
            rc.Width = static_cast<INT>(ceil(g_ctx.cropRectLocal.right)) - rc.X;
            rc.Height = static_cast<INT>(ceil(g_ctx.cropRectLocal.bottom)) - rc.Y;
            if (rc.Width > 0 && rc.Height > 0) {
                if (SUCCEEDED(clipper->Initialize(source, &rc))) {
                    source = clipper;
                }
            }
        }
    }

    if (g_ctx.isGrayscale) {
        ComPtr<IWICFormatConverter> grayConverter;
        if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&grayConverter))) {
            if (SUCCEEDED(grayConverter->Initialize(source, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                source = grayConverter;
            }
        }
    }

    ComPtr<IWICBitmapScaler> scaler;
    if (SUCCEEDED(g_ctx.wicFactory->CreateBitmapScaler(&scaler))) {
        if (SUCCEEDED(scaler->Initialize(source, newWidth, newHeight, WICBitmapInterpolationModeFant))) {
            source = scaler;
        }
        else {
            MessageBoxW(g_ctx.hWnd, L"Failed to initialize image scaler.", L"Resize Error", MB_ICONERROR);
            return;
        }
    }
    else {
        MessageBoxW(g_ctx.hWnd, L"Failed to create image scaler.", L"Resize Error", MB_ICONERROR);
        return;
    }

    WICPixelFormatGUID sourcePixelFormat{};
    if (SUCCEEDED(source->GetPixelFormat(&sourcePixelFormat))) {
        if (containerFormat == GUID_ContainerFormatJpeg && sourcePixelFormat != GUID_WICPixelFormat24bppBGR) {
            ComPtr<IWICFormatConverter> converter;
            if (SUCCEEDED(g_ctx.wicFactory->CreateFormatConverter(&converter))) {
                if (SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut))) {
                    source = converter;
                }
            }
        }
    }

    HRESULT hr = E_FAIL;
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;

        hr = g_ctx.wicFactory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = g_ctx.wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
        if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
        if (SUCCEEDED(hr)) hr = frame->Initialize(props);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(source, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = encoder->Commit();
    }

    if (SUCCEEDED(hr)) {
        LoadImageFromFile(filePath.c_str());
    }
    else {
        MessageBoxW(g_ctx.hWnd, L"Failed to save resized image.", L"Resize Error", MB_ICONERROR);
    }
}

struct ResizeDialogParams {
    UINT origWidth;
    UINT origHeight;
    UINT newWidth;
    UINT newHeight;
};
static bool g_isUpdating = false;

static INT_PTR CALLBACK ResizeDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        ResizeDialogParams* pParams = reinterpret_cast<ResizeDialogParams*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pParams);
        SetDlgItemInt(hDlg, IDC_EDIT_WIDTH, pParams->origWidth, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_HEIGHT, pParams->origHeight, FALSE);
        CheckDlgButton(hDlg, IDC_CHECK_ASPECT, BST_CHECKED);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE) {
            if (g_isUpdating) return (INT_PTR)FALSE;
            if (!IsDlgButtonChecked(hDlg, IDC_CHECK_ASPECT)) return (INT_PTR)FALSE;

            g_isUpdating = true;
            ResizeDialogParams* pParams = reinterpret_cast<ResizeDialogParams*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
            if (pParams && pParams->origWidth > 0 && pParams->origHeight > 0) {
                double aspect = static_cast<double>(pParams->origHeight) / pParams->origWidth;
                int id = LOWORD(wParam);
                BOOL success = FALSE;
                UINT val = GetDlgItemInt(hDlg, id, &success, FALSE);
                if (success) {
                    if (id == IDC_EDIT_WIDTH) {
                        UINT newHeight = static_cast<UINT>(val * aspect + 0.5);
                        SetDlgItemInt(hDlg, IDC_EDIT_HEIGHT, newHeight, FALSE);
                    }
                    else if (id == IDC_EDIT_HEIGHT) {
                        UINT newWidth = static_cast<UINT>(val / aspect + 0.5);
                        SetDlgItemInt(hDlg, IDC_EDIT_WIDTH, newWidth, FALSE);
                    }
                }
            }
            g_isUpdating = false;
            return (INT_PTR)TRUE;
        }

        switch (LOWORD(wParam)) {
        case IDOK: {
            ResizeDialogParams* pParams = reinterpret_cast<ResizeDialogParams*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
            BOOL successW, successH;
            pParams->newWidth = GetDlgItemInt(hDlg, IDC_EDIT_WIDTH, &successW, FALSE);
            pParams->newHeight = GetDlgItemInt(hDlg, IDC_EDIT_HEIGHT, &successH, FALSE);
            if (successW && successH && pParams->newWidth > 0 && pParams->newHeight > 0) {
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBoxW(hDlg, L"Please enter valid (non-zero) positive numbers for width and height.", L"Invalid Input", MB_ICONERROR);
            }
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void ResizeImageAction() {
    ResizeDialogParams params = {};
    if (!GetCurrentImageSize(&params.origWidth, &params.origHeight)) {
        MessageBoxW(g_ctx.hWnd, L"No image loaded to resize.", L"Resize Error", MB_ICONERROR);
        return;
    }
    params.newWidth = params.origWidth;
    params.newHeight = params.origHeight;

    if (DialogBoxParam(g_ctx.hInst, MAKEINTRESOURCE(IDD_RESIZE_DIALOG), g_ctx.hWnd, ResizeDialogProc, (LPARAM)&params) == IDOK) {

        wchar_t szFile[MAX_PATH] = L"Untitled.png";
        const wchar_t* filter = L"PNG File (*.png)\0*.png\0JPEG File (*.jpg)\0*.jpg\0BMP File (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
        UINT filterIndex = 1;
        const wchar_t* defaultExt = L"png";

        GUID originalFormat = GUID_NULL;
        {
            CriticalSectionLock lock(g_ctx.wicMutex);
            originalFormat = g_ctx.originalContainerFormat;
        }

        if (originalFormat == GUID_ContainerFormatJpeg) {
            filterIndex = 2;
            defaultExt = L"jpg";
            wcscpy_s(szFile, L"Untitled.jpg");
        }
        else if (originalFormat == GUID_ContainerFormatBmp) {
            filterIndex = 3;
            defaultExt = L"bmp";
            wcscpy_s(szFile, L"Untitled.bmp");
        }

        if (g_ctx.currentImageIndex >= 0 && g_ctx.currentImageIndex < static_cast<int>(g_ctx.imageFiles.size())) {
            const std::wstring& originalPath = g_ctx.imageFiles[g_ctx.currentImageIndex];
            wchar_t originalFileName[MAX_PATH];
            wcscpy_s(originalFileName, MAX_PATH, originalPath.c_str());
            PathRemoveExtensionW(originalFileName);
            PathStripPathW(originalFileName);

            swprintf_s(szFile, MAX_PATH, L"%s_resized.%s", originalFileName, defaultExt);
        }

        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = g_ctx.hWnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = filterIndex;
        ofn.lpstrDefExt = defaultExt;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

        if (GetSaveFileNameW(&ofn)) {
            GUID containerFormat = GUID_ContainerFormatPng;
            const wchar_t* ext = PathFindExtensionW(ofn.lpstrFile);
            if (ext) {
                if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) containerFormat = GUID_ContainerFormatJpeg;
                else if (_wcsicmp(ext, L".bmp") == 0) containerFormat = GUID_ContainerFormatBmp;
            }
            SaveImageWithResize(ofn.lpstrFile, containerFormat, params.newWidth, params.newHeight);
        }
    }
}