#include "viewer.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

extern AppContext g_ctx;

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Ocr;
using namespace Windows::Storage;

static void PerformOcr_Thread(std::wstring filePath, HWND hWnd) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    auto ocrEngine = OcrEngine::TryCreateFromUserProfileLanguages();
    if (ocrEngine == nullptr) {
        PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)5);
        winrt::uninit_apartment();
        return;
    }

    try {
        auto file = StorageFile::GetFileFromPathAsync(filePath.c_str()).get();
        auto stream = file.OpenAsync(FileAccessMode::Read).get();
        auto decoder = BitmapDecoder::CreateAsync(stream).get();
        auto softwareBitmap = decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied).get();
        auto ocrResult = ocrEngine.RecognizeAsync(softwareBitmap).get();

        std::wstring allText;
        if (ocrResult.Text().empty()) {
            PostMessage(hWnd, WM_APP_OCR_DONE_NOTEXT, 0, (LPARAM)0);
            winrt::uninit_apartment();
            return;
        }

        for (uint32_t i = 0; i < ocrResult.Lines().Size(); ++i) {
            auto line = ocrResult.Lines().GetAt(i);
            allText += line.Text().c_str();
            allText += L"\r\n";
        }

        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            size_t sizeInBytes = (allText.length() + 1) * sizeof(wchar_t);
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);
            if (hg) {
                void* pData = GlobalLock(hg);
                if (pData) {
                    memcpy(pData, allText.c_str(), sizeInBytes);
                    GlobalUnlock(hg);
                    SetClipboardData(CF_UNICODETEXT, hg);
                    PostMessage(hWnd, WM_APP_OCR_DONE_TEXT, 0, 0);
                }
                else {
                    GlobalFree(hg);
                    PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)3);
                }
            }
            else {
                PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)3);
            }
            CloseClipboard();
        }
        else {
            PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)3);
        }
    }
    catch (winrt::hresult_error const&) {
        PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)1);
    }
    catch (...) {
        PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)2);
    }

    winrt::uninit_apartment();
}

void PerformOcr() {
    if (g_ctx.loadingFilePath.empty()) {
        MessageBoxW(g_ctx.hWnd, L"No image file loaded. OCR cannot be performed on pasted images.", L"OCR Error", MB_OK | MB_ICONWARNING);
        return;
    }

    SetCursor(LoadCursor(nullptr, IDC_WAIT));

    std::wstring filePath = g_ctx.loadingFilePath;
    HWND hWnd = g_ctx.hWnd;

    std::thread(PerformOcr_Thread, filePath, hWnd).detach();
}

static void PerformOcrArea_Thread(std::wstring filePath, D2D1_RECT_F ocrRectLocal, HWND hWnd) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    auto ocrEngine = OcrEngine::TryCreateFromUserProfileLanguages();
    if (ocrEngine == nullptr) {
        PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)5);
        winrt::uninit_apartment();
        return;
    }

    try {
        auto file = StorageFile::GetFileFromPathAsync(filePath.c_str()).get();
        auto stream = file.OpenAsync(FileAccessMode::Read).get();
        auto decoder = BitmapDecoder::CreateAsync(stream).get();
        auto frame = decoder.GetFrameAsync(0).get();

        winrt::Windows::Graphics::Imaging::BitmapTransform transform;
        winrt::Windows::Graphics::Imaging::BitmapBounds bounds;

        bounds.X = static_cast<uint32_t>(floor(ocrRectLocal.left));
        bounds.Y = static_cast<uint32_t>(floor(ocrRectLocal.top));
        bounds.Width = static_cast<uint32_t>(ceil(ocrRectLocal.right)) - bounds.X;
        bounds.Height = static_cast<uint32_t>(ceil(ocrRectLocal.bottom)) - bounds.Y;

        if (bounds.Width <= 0 || bounds.Height <= 0) {
            PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)4);
            winrt::uninit_apartment();
            return;
        }

        transform.Bounds(bounds);

        auto softwareBitmap = frame.GetSoftwareBitmapAsync(
            BitmapPixelFormat::Bgra8,
            BitmapAlphaMode::Premultiplied,
            transform,
            ExifOrientationMode::IgnoreExifOrientation,
            ColorManagementMode::DoNotColorManage
        ).get();

        auto ocrResult = ocrEngine.RecognizeAsync(softwareBitmap).get();

        std::wstring allText;
        if (ocrResult.Text().empty()) {
            PostMessage(hWnd, WM_APP_OCR_DONE_NOTEXT, 0, (LPARAM)1);
            winrt::uninit_apartment();
            return;
        }

        for (uint32_t i = 0; i < ocrResult.Lines().Size(); ++i) {
            auto line = ocrResult.Lines().GetAt(i);
            allText += line.Text().c_str();
            allText += L"\r\n";
        }

        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            size_t sizeInBytes = (allText.length() + 1) * sizeof(wchar_t);
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);
            if (hg) {
                void* pData = GlobalLock(hg);
                if (pData) {
                    memcpy(pData, allText.c_str(), sizeInBytes);
                    GlobalUnlock(hg);
                    SetClipboardData(CF_UNICODETEXT, hg);
                    PostMessage(hWnd, WM_APP_OCR_DONE_AREA, 0, 0);
                }
                else {
                    GlobalFree(hg);
                    PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)3);
                }
            }
            else {
                PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)3);
            }
            CloseClipboard();
        }
        else {
            PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)3);
        }
    }
    catch (winrt::hresult_error const&) {
        PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)1);
    }
    catch (...) {
        PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)2);
    }

    winrt::uninit_apartment();
}

void PerformOcrArea(D2D1_RECT_F ocrRectLocal) {
    if (g_ctx.loadingFilePath.empty()) {
        MessageBoxW(g_ctx.hWnd, L"No image file loaded. OCR cannot be performed on pasted images.", L"OCR Error", MB_OK | MB_ICONWARNING);
        return;
    }

    SetCursor(LoadCursor(nullptr, IDC_WAIT));

    std::wstring filePath = g_ctx.loadingFilePath;
    HWND hWnd = g_ctx.hWnd;

    std::thread(PerformOcrArea_Thread, filePath, ocrRectLocal, hWnd).detach();
}