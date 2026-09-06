#include "viewer.h"



void ViewerApp::OpenFileAction() {
    std::wstring initialDir;
    if (!m_ctx.currentDirectory.empty()) {
        if (GetFileAttributesW(m_ctx.currentDirectory.c_str()) == INVALID_FILE_ATTRIBUTES) {
            // Folder deleted: safely fall back to Pictures folder to avoid System32
            PWSTR picturesPath = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &picturesPath))) {
                initialDir = picturesPath;
                CoTaskMemFree(picturesPath);
            }
            else {
                initialDir = L"C:\\";
            }
        }
        else {
            initialDir = m_ctx.currentDirectory;
        }
    }

    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_ctx.hWnd;
    ofn.lpstrFilter = L"All Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tiff;*.tif;*.ico;*.webp;*.heic;*.heif;*.avif;*.cr2;*.cr3;*.nef;*.dng;*.arw;*.orf;*.rw2;*.svg;*.qoi;*.hdr\0HDR Files (*.hdr)\0*.hdr\0SVG Files (*.svg)\0*.svg\0QOI Files (*.qoi)\0*.qoi\0PNG Files (*.png)\0*.png\0JPEG Files (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0WebP Files (*.webp)\0*.webp\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    if (!initialDir.empty()) {
        ofn.lpstrInitialDir = initialDir.c_str();
    }
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        LoadImageFromFile(szFile);
    }
}

void ViewerApp::DeleteCurrentImage() {
    if (m_ctx.currentImageIndex < 0 || m_ctx.imageFiles.empty()) return;

    if (m_ctx.askToDelete) {
        if (MessageBoxW(m_ctx.hWnd, Tr(StrId::ConfirmDelete), Tr(StrId::ConfirmDeleteTitle), MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    std::wstring filePath = m_ctx.imageFiles[m_ctx.currentImageIndex];

    ComPtr<IFileOperation> fileOp;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileOp));

    if (SUCCEEDED(hr)) {
        // Send to recycle bin with no prompt
        hr = fileOp->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION);
        if (SUCCEEDED(hr)) {
            ComPtr<IShellItem> itemToDelete;
            hr = SHCreateItemFromParsingName(filePath.c_str(), nullptr, IID_PPV_ARGS(&itemToDelete));

            if (SUCCEEDED(hr)) {
                hr = fileOp->DeleteItem(itemToDelete.Get(), nullptr);

                if (SUCCEEDED(hr)) {
                    hr = fileOp->PerformOperations();

                    if (SUCCEEDED(hr)) {
                        BOOL aborted = FALSE;
                        fileOp->GetAnyOperationsAborted(&aborted);

                        if (!aborted) {
                            m_ctx.imageFiles.erase(m_ctx.imageFiles.begin() + m_ctx.currentImageIndex);

                            if (m_ctx.imageFiles.empty()) {
                                m_ctx.currentImageIndex = -1;
                                {
                                    std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
                                    m_ctx.wicConverter = nullptr;
                                    m_ctx.wicConverterOriginal = nullptr;
                                    m_ctx.undoStack.clear();
                                    m_ctx.d2dBitmap = nullptr;
                                    m_ctx.loadingFilePath = L"";
                                }
                                InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
                                SetWindowTextW(m_ctx.hWnd, L"Minimal Image Viewer v2.0.4");
                            }
                            else {
                                if (m_ctx.currentImageIndex >= static_cast<int>(m_ctx.imageFiles.size())) {
                                    m_ctx.currentImageIndex = 0;
                                }
                                LoadImageFromFile(m_ctx.imageFiles[m_ctx.currentImageIndex]);
                            }
                        }
                    }
                }
            }
        }
    }
}

void ViewerApp::HandleDropFiles(HDROP hDrop) {
    UINT charsRequired = DragQueryFileW(hDrop, 0, nullptr, 0);

    if (charsRequired > 0) {
        std::wstring filePath(charsRequired + 1, L'\0');
        if (DragQueryFileW(hDrop, 0, filePath.data(), charsRequired + 1)) {
            filePath.resize(charsRequired); 
            LoadImageFromFile(filePath);
        }
    }
    DragFinish(hDrop);
}

void ViewerApp::HandleCopy() {
    if (OpenClipboard(m_ctx.hWnd)) {
        EmptyClipboard();
        // Copy as CF_HDROP (File Path Only)
        if (!m_ctx.loadingFilePath.empty() && m_ctx.loadingFilePath != L"Clipboard Image") {
            size_t size = (m_ctx.loadingFilePath.length() + 1) * sizeof(wchar_t);
            HGLOBAL hMemDrop = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + size + sizeof(wchar_t));
            if (hMemDrop) {
                BYTE* pData = static_cast<BYTE*>(GlobalLock(hMemDrop));
                if (pData) {
                    DROPFILES* pDrop = reinterpret_cast<DROPFILES*>(pData);
                    pDrop->pFiles = sizeof(DROPFILES);
                    pDrop->pt = { 0, 0 };
                    pDrop->fNC = FALSE;
                    pDrop->fWide = TRUE;
                    wchar_t* pPath = reinterpret_cast<wchar_t*>(pData + sizeof(DROPFILES));
                    wcscpy_s(pPath, m_ctx.loadingFilePath.length() + 1, m_ctx.loadingFilePath.c_str());

                    GlobalUnlock(hMemDrop);
                    SetClipboardData(CF_HDROP, hMemDrop);
                }
                else {
                    GlobalFree(hMemDrop);
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
                HDROP hDrop = static_cast<HDROP>(hData);
                UINT charsRequired = DragQueryFileW(hDrop, 0, nullptr, 0);
                if (charsRequired > 0) {
                    std::wstring filePath(charsRequired + 1, L'\0');
                    if (DragQueryFileW(hDrop, 0, filePath.data(), charsRequired + 1)) {
                        filePath.resize(charsRequired);
                        LoadImageFromFile(filePath);
                    }
                }
            }
        }
        else if (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB)) {
            HBITMAP hBitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
            if (hBitmap) {
               std::lock_guard<std::recursive_mutex> lock(m_ctx.wicMutex);
                ComPtr<IWICBitmap> wicBitmap;
                HRESULT hr = m_ctx.wicFactory->CreateBitmapFromHBITMAP(hBitmap, NULL, WICBitmapUseAlpha, &wicBitmap);

                if (SUCCEEDED(hr)) {
                    if (ComPtr<IWICFormatConverter> converter = ConvertToFormat(m_ctx.wicFactory.Get(), wicBitmap.Get())) {
                        // reset state for new pasted image
                        m_ctx.wicConverter = converter;
                        m_ctx.wicConverterOriginal = converter;
                        m_ctx.d2dBitmap = nullptr;
                        m_ctx.animationFrameMetadata.clear();
                        m_ctx.animationFrameDelays.clear();
                        m_ctx.isAnimated = false;
                        // clear file context
                        m_ctx.imageFiles.clear();
                        m_ctx.currentImageIndex = -1;
                        m_ctx.currentDirectory = L"";
                        m_ctx.loadingFilePath = L"Clipboard Image";
                        m_ctx.originalContainerFormat = GUID_ContainerFormatPng;
                        m_ctx.isOsdCacheValid = false;

                        m_ctx.zoomFactor = 1.0f;
                        m_ctx.offsetX = 0;
                        m_ctx.offsetY = 0;

                        // stop animations
                        KillTimer(m_ctx.hWnd, ANIMATION_TIMER_ID);
                        SetWindowTextW(m_ctx.hWnd, L"Clipboard Image - Minimal Image Viewer v2.0.3");
                        InvalidateRect(m_ctx.hWnd, nullptr, FALSE);
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

void ViewerApp::SetWallpaper() {
    if (m_ctx.loadingFilePath.empty() || m_ctx.loadingFilePath == L"Clipboard Image") {
        MessageBoxW(m_ctx.hWnd, L"No image loaded.", L"Set Wallpaper", MB_ICONINFORMATION);
        return;
    }

    ComPtr<IWICBitmapSource> source = GetSaveSource(GUID_ContainerFormatBmp);
    if (!source) {
        MessageBoxW(m_ctx.hWnd, L"Could not get image source.", L"Set Wallpaper", MB_ICONERROR);
        return;
    }

    PWSTR localAppDataPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppDataPath))) {
        MessageBoxW(m_ctx.hWnd, L"Could not get AppData path.", L"Set Wallpaper", MB_ICONERROR);
        return;
    }
    std::wstring wallpaperDir = std::wstring(localAppDataPath) + L"\\MinimalImageViewer";
    CoTaskMemFree(localAppDataPath);
    SHCreateDirectoryExW(nullptr, wallpaperDir.c_str(), nullptr);

    std::wstring wallpaperPath = wallpaperDir + L"\\wallpaper.bmp";
    HRESULT hr = EncodeAndSaveImage(source, wallpaperPath, GUID_ContainerFormatBmp);
    if (FAILED(hr)) {
        MessageBoxW(m_ctx.hWnd, L"Failed to export image for wallpaper.", L"Set Wallpaper", MB_ICONERROR);
        return;
    }

    if (SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)wallpaperPath.c_str(), SPIF_SENDCHANGE | SPIF_UPDATEINIFILE)) {
        MessageBoxW(m_ctx.hWnd, L"Wallpaper set successfully.", L"Set Wallpaper", MB_OK | MB_ICONINFORMATION);
    }
    else {
        MessageBoxW(m_ctx.hWnd, L"Failed to set wallpaper.", L"Set Wallpaper", MB_ICONERROR);
    }
}

void ViewerApp::RegisterFileAssociations() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring command = std::wstring(L"\"") + exePath + L"\" \"%1\"";
    std::wstring progId = L"MinimalImageViewer.Image";
    std::wstring appName = L"Minimal Image Viewer";

    HKEY hKey;

    // Register base ProgID
    std::wstring progIdPath = L"Software\\Classes\\" + progId;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, progIdPath.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"", 0, REG_SZ, reinterpret_cast<const BYTE*>(appName.c_str()), static_cast<DWORD>((appName.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    std::wstring commandPath = progIdPath + L"\\shell\\open\\command";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, commandPath.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    // Register app capabilities
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MinimalImageViewer\\Capabilities", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"ApplicationName", 0, REG_SZ, reinterpret_cast<const BYTE*>(appName.c_str()), static_cast<DWORD>((appName.length() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"ApplicationDescription", 0, REG_SZ, reinterpret_cast<const BYTE*>(appName.c_str()), static_cast<DWORD>((appName.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    // Register FileAssociations 
    const wchar_t* exts[] = { L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".webp", L".heic", L".heif", L".avif", L".svg", L".qoi", L".hdr", L".tiff", L".tif", L".ico" };
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MinimalImageViewer\\Capabilities\\FileAssociations", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        for (const auto& ext : exts) {
            RegSetValueExW(hKey, ext, 0, REG_SZ, reinterpret_cast<const BYTE*>(progId.c_str()), static_cast<DWORD>((progId.length() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(hKey);
    }

    // Register in Windows RegisteredApplications 
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        std::wstring capPath = L"Software\\MinimalImageViewer\\Capabilities";
        RegSetValueExW(hKey, L"MinimalImageViewer", 0, REG_SZ, reinterpret_cast<const BYTE*>(capPath.c_str()), static_cast<DWORD>((capPath.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    // Inject into OpenWithProgids for each extension for context menu
    for (const auto& ext : exts) {
        std::wstring extKey = L"Software\\Classes\\" + std::wstring(ext) + L"\\OpenWithProgids";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, extKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, progId.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(L""), sizeof(L""));
            RegCloseKey(hKey);
        }
    }

    // Notify shell
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}