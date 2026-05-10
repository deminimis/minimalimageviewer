#include "viewer.h"



void ViewerApp::OpenFileAction() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_ctx.hWnd;
    ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2;*.svg\0SVG Files (*.svg)\0*.svg\0PNG Files (*.png)\0*.png\0JPEG Files (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        LoadImageFromFile(szFile);
    }
}

void ViewerApp::DeleteCurrentImage() {
    if (m_ctx.currentImageIndex < 0 || m_ctx.imageFiles.empty()) return;

    std::wstring filePath = m_ctx.imageFiles[m_ctx.currentImageIndex];
    std::wstring pathDoubleNull = filePath + L'\0';

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.hwnd = m_ctx.hWnd;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = pathDoubleNull.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;

    if (SHFileOperationW(&fileOp) == 0 && !fileOp.fAnyOperationsAborted) {
        m_ctx.imageFiles.erase(m_ctx.imageFiles.begin() + m_ctx.currentImageIndex);
        if (m_ctx.imageFiles.empty()) {
            m_ctx.currentImageIndex = -1;
            {
                CriticalSectionLock lock(m_ctx.wicMutex);
                m_ctx.wicConverter = nullptr;
                m_ctx.wicConverterOriginal = nullptr;
                m_ctx.undoStack.clear();
                m_ctx.d2dBitmap = nullptr;
                m_ctx.loadingFilePath = L"";
            }
            InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
            SetWindowTextW(m_ctx.hWnd, L"Minimal Image Viewer");
        }
        else {
            if (m_ctx.currentImageIndex >= static_cast<int>(m_ctx.imageFiles.size())) {
                m_ctx.currentImageIndex = 0;
            }
            LoadImageFromFile(m_ctx.imageFiles[m_ctx.currentImageIndex]);
        }
    }
}

void ViewerApp::HandleDropFiles(HDROP hDrop) {
    wchar_t filePath[MAX_PATH];
    if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) {
        LoadImageFromFile(filePath);
    }
    DragFinish(hDrop);
}

void ViewerApp::HandleCopy() {
    if (OpenClipboard(m_ctx.hWnd)) {
        EmptyClipboard();

        // Copy as CF_HDROP
        if (!m_ctx.loadingFilePath.empty() && m_ctx.loadingFilePath != L"Clipboard Image") {
            size_t size = (m_ctx.loadingFilePath.length() + 1) * sizeof(wchar_t);
            HGLOBAL hMemDrop = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + size + sizeof(wchar_t));
            if (hMemDrop) {
                BYTE* pData = (BYTE*)GlobalLock(hMemDrop);
                if (pData) {
                    DROPFILES* pDrop = (DROPFILES*)pData;
                    pDrop->pFiles = sizeof(DROPFILES);
                    pDrop->pt = { 0, 0 };
                    pDrop->fNC = FALSE;
                    pDrop->fWide = TRUE;
                    wchar_t* pPath = (wchar_t*)(pData + sizeof(DROPFILES));
                    wcscpy_s(pPath, m_ctx.loadingFilePath.length() + 1, m_ctx.loadingFilePath.c_str());

                    GlobalUnlock(hMemDrop);
                    SetClipboardData(CF_HDROP, hMemDrop);
                }
                else {
                    GlobalFree(hMemDrop);
                }
            }
        }

        // Copy as CF_DIB 
        ComPtr<IWICBitmapSource> source;
        int rotAngle = 0;
        bool flipped = false;

        {
            CriticalSectionLock lock(m_ctx.wicMutex);
            source = m_ctx.wicConverter;
            rotAngle = m_ctx.rotationAngle;
            flipped = m_ctx.isFlippedHorizontal;
        }

        if (source && m_ctx.wicFactory) {
            // Apply rotation/flip so the clipboard matches the current view
            if (rotAngle != 0 || flipped) {
                ComPtr<IWICBitmapFlipRotator> rotator;
                if (SUCCEEDED(m_ctx.wicFactory->CreateBitmapFlipRotator(&rotator))) {
                    WICBitmapTransformOptions options = WICBitmapTransformRotate0;
                    switch (rotAngle) {
                    case 90:  options = WICBitmapTransformRotate90; break;
                    case 180: options = WICBitmapTransformRotate180; break;
                    case 270: options = WICBitmapTransformRotate270; break;
                    }
                    if (flipped) {
                        options = static_cast<WICBitmapTransformOptions>(options | WICBitmapTransformFlipHorizontal);
                    }
                    if (SUCCEEDED(rotator->Initialize(source.Get(), options))) {
                        source = rotator;
                    }
                }
            }

            // Convert to 32bpp BGRA for DIB
            ComPtr<IWICFormatConverter> converter;
            if (SUCCEEDED(m_ctx.wicFactory->CreateFormatConverter(&converter))) {
                if (SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom))) {
                    UINT width = 0, height = 0;
                    if (SUCCEEDED(converter->GetSize(&width, &height))) {
                        UINT stride = width * 4;
                        UINT cbImage = stride * height;
                        UINT cbHeader = sizeof(BITMAPINFOHEADER);
                        UINT cbTotal = cbHeader + cbImage;

                        HGLOBAL hMemDib = GlobalAlloc(GMEM_MOVEABLE, cbTotal);
                        if (hMemDib) {
                            BYTE* pData = (BYTE*)GlobalLock(hMemDib);
                            if (pData) {
                                BITMAPINFOHEADER* bmi = (BITMAPINFOHEADER*)pData;
                                ZeroMemory(bmi, sizeof(BITMAPINFOHEADER));
                                bmi->biSize = sizeof(BITMAPINFOHEADER);
                                bmi->biWidth = width;
                                bmi->biHeight = -(LONG)height; 
                                bmi->biPlanes = 1;
                                bmi->biBitCount = 32;
                                bmi->biCompression = BI_RGB;

                                BYTE* pPixels = pData + cbHeader;
                                WICRect rc = { 0, 0, (INT)width, (INT)height };
                                if (SUCCEEDED(converter->CopyPixels(&rc, stride, cbImage, pPixels))) {
                                    GlobalUnlock(hMemDib);
                                    SetClipboardData(CF_DIB, hMemDib);
                                }
                                else {
                                    GlobalUnlock(hMemDib);
                                    GlobalFree(hMemDib);
                                }
                            }
                            else {
                                GlobalFree(hMemDib);
                            }
                        }
                    }
                }
            }
        }
        CloseClipboard();
    }
}

void ViewerApp::HandlePaste() {
    if (OpenClipboard(m_ctx.hWnd)) {
        if (IsClipboardFormatAvailable(CF_HDROP)) {
            HANDLE hData = GetClipboardData(CF_HDROP);
            if (hData) {
                HDROP hDrop = (HDROP)hData;
                wchar_t filePath[MAX_PATH];
                if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) {
                    LoadImageFromFile(filePath);
                }
            }
        }
        else if (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB)) {
            HBITMAP hBitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
            if (hBitmap) {
                CriticalSectionLock lock(m_ctx.wicMutex);
                ComPtr<IWICBitmap> wicBitmap;
                HRESULT hr = m_ctx.wicFactory->CreateBitmapFromHBITMAP(hBitmap, NULL, WICBitmapUseAlpha, &wicBitmap);

                if (SUCCEEDED(hr)) {
                    ComPtr<IWICFormatConverter> converter;
                    hr = m_ctx.wicFactory->CreateFormatConverter(&converter);

                    if (SUCCEEDED(hr)) {
                        hr = converter->Initialize(wicBitmap.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
                        if (SUCCEEDED(hr)) {
                            // reset state for new pasted image
                            m_ctx.wicConverter = converter;
                            m_ctx.wicConverterOriginal = converter;
                            m_ctx.d2dBitmap = nullptr;
                            m_ctx.animationD2DBitmaps.clear();
                            m_ctx.animationFrameConverters.clear();
                            m_ctx.animationFrameDelays.clear();
                            m_ctx.isAnimated = false;
                            // clear file context
                            m_ctx.imageFiles.clear();
                            m_ctx.currentImageIndex = -1;
                            m_ctx.currentDirectory = L"";
                            m_ctx.loadingFilePath = L"Clipboard Image";
                            m_ctx.originalContainerFormat = GUID_ContainerFormatPng;

                            m_ctx.zoomFactor = 1.0f;
                            m_ctx.offsetX = 0;
                            m_ctx.offsetY = 0;

                            // stop animations
                            KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
                            SetWindowTextW(m_ctx.hWnd, L"Clipboard Image");
                            InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
                        }
                    }
                }
            }
        }
        CloseClipboard();
    }
}

void ViewerApp::OpenFileLocationAction() {
    if (m_ctx.loadingFilePath.empty()) return;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(m_ctx.loadingFilePath.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}