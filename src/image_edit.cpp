#include "viewer.h"
#include <objidl.h>



HRESULT ViewerApp::EncodeAndSaveImage(ComPtr<IWICBitmapSource> source, const std::wstring& filePath, const GUID& containerFormat) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;

    HRESULT hr = m_ctx.wicFactory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = m_ctx.wicFactory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
    if (SUCCEEDED(hr)) hr = frame->WriteSource(source.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return hr;
}

void ViewerApp::CommitCrop() {
   std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
    if (!m_ctx.isCropActive || !m_ctx.wicConverterOriginal) {
        m_ctx.isCropActive = false;
        return;
    }

    ComPtr<IWICBitmapSource> source = m_ctx.wicConverterOriginal;

    ComPtr<IWICBitmapClipper> clipper;
    if (SUCCEEDED(m_ctx.wicFactory->CreateBitmapClipper(&clipper))) {
        WICRect rc;
        rc.X = static_cast<INT>(floor(m_ctx.cropRectLocal.left));
        rc.Y = static_cast<INT>(floor(m_ctx.cropRectLocal.top));
        rc.Width = static_cast<INT>(ceil(m_ctx.cropRectLocal.right)) - rc.X;
        rc.Height = static_cast<INT>(ceil(m_ctx.cropRectLocal.bottom)) - rc.Y;
        if (rc.Width > 0 && rc.Height > 0) {
            if (SUCCEEDED(clipper->Initialize(source.Get(), &rc))) {
                ComPtr<IWICFormatConverter> converter;
                if (SUCCEEDED(m_ctx.wicFactory->CreateFormatConverter(&converter))) {
                    if (SUCCEEDED(converter->Initialize(clipper.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {

                        m_ctx.undoStack.push_back(m_ctx.wicConverterOriginal);
                        m_ctx.wicConverterOriginal = converter;
                        m_ctx.isDownscaled = false; // Edits destroy high-res alignment

                        if (m_ctx.isAnimated) {
                            m_ctx.isAnimated = false;
                            m_ctx.animationFrameConverters.clear();
                            m_ctx.animationFrameDelays.clear();
                            KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
                        }
                    }
                }
            }
        }
    }
    m_ctx.isCropActive = false;
    m_ctx.cropRectLocal = { 0 };
}

void ViewerApp::ApplyEffectsToView() {
    ComPtr<IWICBitmapSource> source;
    {
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
        if (!m_ctx.wicConverterOriginal) {
            m_ctx.wicConverter = nullptr;
            return;
        }
        source = m_ctx.wicConverterOriginal;
    }

    // apply crop if active first.
    if (m_ctx.isCropActive) {
        ComPtr<IWICBitmapClipper> clipper;
        if (SUCCEEDED(m_ctx.wicFactory->CreateBitmapClipper(&clipper))) {
            WICRect rc;
            rc.X = static_cast<INT>(floor(m_ctx.cropRectLocal.left));
            rc.Y = static_cast<INT>(floor(m_ctx.cropRectLocal.top));
            rc.Width = static_cast<INT>(ceil(m_ctx.cropRectLocal.right)) - rc.X;
            rc.Height = static_cast<INT>(ceil(m_ctx.cropRectLocal.bottom)) - rc.Y;

            if (rc.Width > 0 && rc.Height > 0) {
                if (SUCCEEDED(clipper->Initialize(source.Get(), &rc))) {
                    source = clipper;
                }
            }
        }
    }
    m_ctx.renderScale = 1.0f;
    if (m_ctx.renderTarget) {
        UINT maxDim = m_ctx.renderTarget->GetMaximumBitmapSize();
        UINT w = 0, h = 0;
        if (SUCCEEDED(source->GetSize(&w, &h)) && (w > maxDim || h > maxDim)) {
            float ratio = std::min(static_cast<float>(maxDim) / w, static_cast<float>(maxDim) / h);
            m_ctx.renderScale = ratio;
            UINT newW = static_cast<UINT>(w * ratio);
            UINT newH = static_cast<UINT>(h * ratio);

            ComPtr<IWICBitmapScaler> scaler;
            if (SUCCEEDED(m_ctx.wicFactory->CreateBitmapScaler(&scaler))) {
                if (SUCCEEDED(scaler->Initialize(source.Get(), newW, newH, WICBitmapInterpolationModeFant))) {
                    source = scaler;
                }
            }
        }
    }

    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(m_ctx.wicFactory->CreateFormatConverter(&converter))) {
        if (SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
           std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
            m_ctx.wicConverter = converter;
            m_ctx.d2dBitmap = nullptr;
            m_ctx.animationD2DBitmaps.clear();
        }
    }
}

ComPtr<IWICBitmapSource> ViewerApp::ApplyCropAndTransform(ComPtr<IWICBitmapSource> source) {
    if (m_ctx.isCropActive) {
        ComPtr<IWICBitmapClipper> clipper;
        if (SUCCEEDED(m_ctx.wicFactory->CreateBitmapClipper(&clipper))) {
            WICRect rc = {
                static_cast<INT>(floor(m_ctx.cropRectLocal.left)),
                static_cast<INT>(floor(m_ctx.cropRectLocal.top)),
                static_cast<INT>(ceil(m_ctx.cropRectLocal.right)) - static_cast<INT>(floor(m_ctx.cropRectLocal.left)),
                static_cast<INT>(ceil(m_ctx.cropRectLocal.bottom)) - static_cast<INT>(floor(m_ctx.cropRectLocal.top))
            };
            if (rc.Width > 0 && rc.Height > 0 && SUCCEEDED(clipper->Initialize(source.Get(), &rc))) {
                source = clipper;
            }
        }
    }

    if (m_ctx.rotationAngle != 0 || m_ctx.isFlippedHorizontal) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (SUCCEEDED(m_ctx.wicFactory->CreateBitmapFlipRotator(&rotator))) {
            WICBitmapTransformOptions options = WICBitmapTransformRotate0;
            switch (m_ctx.rotationAngle) {
            case 90:  options = WICBitmapTransformRotate90; break;
            case 180: options = WICBitmapTransformRotate180; break;
            case 270: options = WICBitmapTransformRotate270; break;
            }
            if (m_ctx.isFlippedHorizontal) {
                options = static_cast<WICBitmapTransformOptions>(options | WICBitmapTransformFlipHorizontal);
            }
            if (SUCCEEDED(rotator->Initialize(source.Get(), options))) {
                source = rotator;
            }
        }
    }
    return source;
}

ComPtr<IWICBitmapSource> ViewerApp::GetSaveSource(const GUID& targetFormat) {
   std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
    ComPtr<IWICBitmapSource> source;

    if (m_ctx.isAnimated && m_ctx.currentAnimationFrame < m_ctx.animationFrameConverters.size()) {
        source = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
    }
    else if (m_ctx.wicConverterOriginal) {
        source = m_ctx.wicConverterOriginal;
    }
    else {
        return nullptr;
    }

    source = ApplyCropAndTransform(source);

    WICPixelFormatGUID sourcePixelFormat{};
    if (FAILED(source->GetPixelFormat(&sourcePixelFormat))) return nullptr;

    if (targetFormat == GUID_ContainerFormatJpeg && sourcePixelFormat != GUID_WICPixelFormat24bppBGR) {
        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(m_ctx.wicFactory->CreateFormatConverter(&converter))) {
            if (SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut))) {
                source = converter;
            }
        }
    }
    return source;
}

void ViewerApp::SaveImageAs() {
    UINT imgWidth, imgHeight;
    if (!GetCurrentImageSize(&imgWidth, &imgHeight)) return;

    wchar_t szFile[MAX_PATH] = L"Untitled.png";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = m_ctx.hWnd;
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
        MessageBoxW(m_ctx.hWnd, L"Could not get image source to save.", L"Save Error", MB_ICONERROR);
        return;
    }

    HRESULT hr = EncodeAndSaveImage(source, ofn.lpstrFile, containerFormat);

    if (SUCCEEDED(hr)) {
        LoadImageFromFile(ofn.lpstrFile);
    }
    else {
        MessageBoxW(m_ctx.hWnd, L"Failed to save image.", L"Save As Error", MB_ICONERROR);
    }
}

void ViewerApp::SaveImage() {
    if (m_ctx.currentImageIndex < 0 || m_ctx.currentImageIndex >= static_cast<int>(m_ctx.imageFiles.size())) {
        UINT imgWidth, imgHeight;
        if (GetCurrentImageSize(&imgWidth, &imgHeight)) {
            SaveImageAs();
        }
        return;
    }

    const std::wstring& originalPath = m_ctx.imageFiles[m_ctx.currentImageIndex];
    if (m_ctx.rotationAngle == 0 && !m_ctx.isFlippedHorizontal && !m_ctx.isCropActive) {
        MessageBoxW(m_ctx.hWnd, L"No changes to save.", L"Save", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // AVIF/HEIC save prompt
    const wchar_t* ext = PathFindExtensionW(originalPath.c_str());
    if (ext && (_wcsicmp(ext, L".heic") == 0 || _wcsicmp(ext, L".heif") == 0 || _wcsicmp(ext, L".avif") == 0)) {
        if (MessageBoxW(m_ctx.hWnd, L"HEIC and AVIF files cannot be natively overwritten. Would you like to save your edits as a PNG instead?", L"Save Edits", MB_YESNO | MB_ICONQUESTION) == IDYES) {

            wchar_t newPath[MAX_PATH];
            wcscpy_s(newPath, MAX_PATH, originalPath.c_str());
            PathRenameExtensionW(newPath, L".png");

            ComPtr<IWICBitmapSource> source = GetSaveSource(GUID_ContainerFormatPng);
            if (source && SUCCEEDED(EncodeAndSaveImage(source, newPath, GUID_ContainerFormatPng))) {
                LoadImageFromFile(newPath);
            }
            else {
                MessageBoxW(m_ctx.hWnd, L"Failed to save as PNG.", L"Save Error", MB_ICONERROR);
            }
        }
        return;
    }


    GUID containerFormat{};
    {
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
        containerFormat = m_ctx.originalContainerFormat;
    }

    if (containerFormat == GUID_NULL) {
        MessageBoxW(m_ctx.hWnd, L"Could not determine original file format. Use 'Save As'.", L"Save Error", MB_ICONERROR);
        return;
    }

    ComPtr<IWICBitmapSource> source = GetSaveSource(containerFormat);
    if (!source) {
        MessageBoxW(m_ctx.hWnd, L"Could not get image source to save.", L"Save Error", MB_ICONERROR);
        return;
    }

    std::wstring tempPath = originalPath + L".tmp_save";
    HRESULT hr = EncodeAndSaveImage(source, tempPath, containerFormat);

    if (SUCCEEDED(hr)) {
        if (ReplaceFileW(originalPath.c_str(), tempPath.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            LoadImageFromFile(originalPath.c_str());
        }
        else {
            DeleteFileW(tempPath.c_str());
            MessageBoxW(m_ctx.hWnd, L"Failed to replace the original file.", L"Save Error", MB_ICONERROR);
        }
    }
    else {
        DeleteFileW(tempPath.c_str());
        MessageBoxW(m_ctx.hWnd, L"Failed to save image to temporary file.", L"Save Error", MB_ICONERROR);
    }
}

void ViewerApp::SaveImageWithResize(const std::wstring& filePath, const GUID& containerFormat, UINT newWidth, UINT newHeight) {
    ComPtr<IWICBitmapSource> source;
    {
       std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
        if (m_ctx.isAnimated && m_ctx.currentAnimationFrame < m_ctx.animationFrameConverters.size()) {
            source = m_ctx.animationFrameConverters[m_ctx.currentAnimationFrame];
        }
        else if (m_ctx.wicConverterOriginal) {
            source = m_ctx.wicConverterOriginal;
        }
        else {
            MessageBoxW(m_ctx.hWnd, L"Could not get image source to resize.", L"Resize Error", MB_ICONERROR);
            return;
        }
    }

    source = ApplyCropAndTransform(source);

    ComPtr<IWICBitmapScaler> scaler;
    if (SUCCEEDED(m_ctx.wicFactory->CreateBitmapScaler(&scaler))) {
        if (SUCCEEDED(scaler->Initialize(source.Get(), newWidth, newHeight, WICBitmapInterpolationModeFant))) {
            source = scaler;
        }
        else {
            MessageBoxW(m_ctx.hWnd, L"Failed to initialize image scaler.", L"Resize Error", MB_ICONERROR);
            return;
        }
    }
    else {
        MessageBoxW(m_ctx.hWnd, L"Failed to create image scaler.", L"Resize Error", MB_ICONERROR);
        return;
    }

    WICPixelFormatGUID sourcePixelFormat{};
    if (SUCCEEDED(source->GetPixelFormat(&sourcePixelFormat))) {
        if (containerFormat == GUID_ContainerFormatJpeg && sourcePixelFormat != GUID_WICPixelFormat24bppBGR) {
            ComPtr<IWICFormatConverter> converter;
            if (SUCCEEDED(m_ctx.wicFactory->CreateFormatConverter(&converter))) {
                if (SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut))) {
                    source = converter;
                }
            }
        }
    }

    HRESULT hr = EncodeAndSaveImage(source, filePath, containerFormat);

    if (SUCCEEDED(hr)) {
        LoadImageFromFile(filePath.c_str());
    }
    else {
        MessageBoxW(m_ctx.hWnd, L"Failed to save resized image.", L"Resize Error", MB_ICONERROR);
    }
}

struct ResizeDialogParams {
    UINT origWidth;
    UINT origHeight;
    UINT newWidth;
    UINT newHeight;
    bool isUpdating;
};

static INT_PTR CALLBACK ResizeDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        ResizeDialogParams* pParams = reinterpret_cast<ResizeDialogParams*>(lParam);
        pParams->isUpdating = false;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pParams);
        SetDlgItemInt(hDlg, IDC_EDIT_WIDTH, pParams->origWidth, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_HEIGHT, pParams->origHeight, FALSE);
        CheckDlgButton(hDlg, IDC_CHECK_ASPECT, BST_CHECKED);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE) {
            ResizeDialogParams* pParams = reinterpret_cast<ResizeDialogParams*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
            if (!pParams || pParams->isUpdating) return (INT_PTR)FALSE;
            if (!IsDlgButtonChecked(hDlg, IDC_CHECK_ASPECT)) return (INT_PTR)FALSE;

            pParams->isUpdating = true;
            if (pParams->origWidth > 0 && pParams->origHeight > 0) {
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
            pParams->isUpdating = false;
            return (INT_PTR)TRUE;
        }

        switch (LOWORD(wParam)) {
        case IDOK: {
            ResizeDialogParams* pParams = reinterpret_cast<ResizeDialogParams*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
            if (pParams) {
                BOOL successW, successH;
                pParams->newWidth = GetDlgItemInt(hDlg, IDC_EDIT_WIDTH, &successW, FALSE);
                pParams->newHeight = GetDlgItemInt(hDlg, IDC_EDIT_HEIGHT, &successH, FALSE);
                if (successW && successH && pParams->newWidth > 0 && pParams->newHeight > 0) {
                    EndDialog(hDlg, IDOK);
                }
                else {
                    MessageBoxW(hDlg, L"Please enter valid (non-zero) positive numbers for width and height.", L"Invalid Input", MB_ICONERROR);
                }
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

void ViewerApp::ResizeImageAction() {
    ResizeDialogParams params = {};
    if (!GetCurrentImageSize(&params.origWidth, &params.origHeight)) {
        MessageBoxW(m_ctx.hWnd, L"No image loaded to resize.", L"Resize Error", MB_ICONERROR);
        return;
    }
    params.newWidth = params.origWidth;
    params.newHeight = params.origHeight;

    if (DialogBoxParam(m_ctx.hInst, MAKEINTRESOURCE(IDD_RESIZE_DIALOG), m_ctx.hWnd, ResizeDialogProc, (LPARAM)&params) == IDOK) {

        wchar_t szFile[MAX_PATH] = L"Untitled.png";
        const wchar_t* filter = L"PNG File (*.png)\0*.png\0JPEG File (*.jpg)\0*.jpg\0BMP File (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
        UINT filterIndex = 1;
        const wchar_t* defaultExt = L"png";

        GUID originalFormat = GUID_NULL;
        {
           std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
            originalFormat = m_ctx.originalContainerFormat;
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

        if (m_ctx.currentImageIndex >= 0 && m_ctx.currentImageIndex < static_cast<int>(m_ctx.imageFiles.size())) {
            const std::wstring& originalPath = m_ctx.imageFiles[m_ctx.currentImageIndex];
            wchar_t originalFileName[MAX_PATH];
            wcscpy_s(originalFileName, MAX_PATH, originalPath.c_str());
            PathRemoveExtensionW(originalFileName);
            PathStripPathW(originalFileName);

            swprintf_s(szFile, MAX_PATH, L"%s_resized.%s", originalFileName, defaultExt);
        }

        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = m_ctx.hWnd;
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