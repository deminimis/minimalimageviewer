#include "viewer.h"

extern AppContext g_ctx;

void OpenFileAction() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = g_ctx.hWnd;
    ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2\0All Files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        LoadImageFromFile(szFile);
    }
}

void DeleteCurrentImage() {
    if (g_ctx.currentImageIndex < 0 || g_ctx.imageFiles.empty()) return;

    std::wstring filePath = g_ctx.imageFiles[g_ctx.currentImageIndex];
    std::wstring pathDoubleNull = filePath + L'\0';

    SHFILEOPSTRUCTW fileOp = { 0 };
    fileOp.hwnd = g_ctx.hWnd;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = pathDoubleNull.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;

    if (SHFileOperationW(&fileOp) == 0 && !fileOp.fAnyOperationsAborted) {
        g_ctx.imageFiles.erase(g_ctx.imageFiles.begin() + g_ctx.currentImageIndex);
        if (g_ctx.imageFiles.empty()) {
            g_ctx.currentImageIndex = -1;
            {
                CriticalSectionLock lock(g_ctx.wicMutex);
                g_ctx.wicConverter = nullptr;
                g_ctx.wicConverterOriginal = nullptr;
                g_ctx.d2dBitmap = nullptr;
                g_ctx.loadingFilePath = L"";
            }
            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
            SetWindowTextW(g_ctx.hWnd, L"Minimal Image Viewer");
        }
        else {
            if (g_ctx.currentImageIndex >= static_cast<int>(g_ctx.imageFiles.size())) {
                g_ctx.currentImageIndex = 0;
            }
            LoadImageFromFile(g_ctx.imageFiles[g_ctx.currentImageIndex]);
        }
    }
}

void HandleDropFiles(HDROP hDrop) {
    wchar_t filePath[MAX_PATH];
    if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) {
        LoadImageFromFile(filePath);
    }
    DragFinish(hDrop);
}

void HandleCopy() {
    if (g_ctx.loadingFilePath.empty()) return;

    if (OpenClipboard(g_ctx.hWnd)) {
        EmptyClipboard();
        size_t size = (g_ctx.loadingFilePath.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + size + sizeof(wchar_t));
        if (hMem) {
            BYTE* pData = (BYTE*)GlobalLock(hMem);
            if (pData) {
                DROPFILES* pDrop = (DROPFILES*)pData;
                pDrop->pFiles = sizeof(DROPFILES);
                pDrop->pt = { 0, 0 };
                pDrop->fNC = FALSE;
                pDrop->fWide = TRUE;
                wchar_t* pPath = (wchar_t*)(pData + sizeof(DROPFILES));
                wcscpy_s(pPath, g_ctx.loadingFilePath.length() + 1, g_ctx.loadingFilePath.c_str());

                GlobalUnlock(hMem);
                SetClipboardData(CF_HDROP, hMem);
            }
            else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }
}

void HandlePaste() {
    if (OpenClipboard(g_ctx.hWnd)) {
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
                CriticalSectionLock lock(g_ctx.wicMutex);
                ComPtr<IWICBitmap> wicBitmap;
                HRESULT hr = g_ctx.wicFactory->CreateBitmapFromHBITMAP(hBitmap, NULL, WICBitmapUseAlpha, &wicBitmap);

                if (SUCCEEDED(hr)) {
                    ComPtr<IWICFormatConverter> converter;
                    hr = g_ctx.wicFactory->CreateFormatConverter(&converter);

                    if (SUCCEEDED(hr)) {
                        hr = converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
                        if (SUCCEEDED(hr)) {
                            // reset state for new pasted image
                            g_ctx.wicConverter = converter;
                            g_ctx.wicConverterOriginal = converter;
                            g_ctx.d2dBitmap = nullptr;
                            g_ctx.animationD2DBitmaps.clear();
                            g_ctx.animationFrameConverters.clear();
                            g_ctx.animationFrameDelays.clear();
                            g_ctx.isAnimated = false;
                            // clear file context
                            g_ctx.imageFiles.clear();
                            g_ctx.currentImageIndex = -1;
                            g_ctx.currentDirectory = L"";
                            g_ctx.loadingFilePath = L"Clipboard Image";
                            g_ctx.originalContainerFormat = GUID_ContainerFormatPng;

                            g_ctx.zoomFactor = 1.0f;
                            g_ctx.offsetX = 0;
                            g_ctx.offsetY = 0;

                            // stop animations
                            KillTimer(g_ctx.hWnd, ANIMATION_TIMER_ID);
                            SetWindowTextW(g_ctx.hWnd, L"Clipboard Image");
                            InvalidateRect(g_ctx.hWnd, nullptr, FALSE);
                        }
                    }
                }
            }
        }
        CloseClipboard();
    }
}

void OpenFileLocationAction() {
    if (g_ctx.loadingFilePath.empty()) return;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(g_ctx.loadingFilePath.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}