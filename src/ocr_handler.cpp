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

static void CopyToClipboardAndNotify(HWND hWnd, const std::wstring& text, UINT successMsg) {
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        size_t sizeInBytes = (text.length() + 1) * sizeof(wchar_t);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);
        if (hg) {
            void* pData = GlobalLock(hg);
            if (pData) {
                memcpy(pData, text.c_str(), sizeInBytes);
                GlobalUnlock(hg);
                SetClipboardData(CF_UNICODETEXT, hg);
                PostMessage(hWnd, successMsg, 0, 0);
                CloseClipboard();
                return;
            }
            GlobalFree(hg);
        }
        CloseClipboard();
    }
    PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)3);
}

static void PerformOcrTask(std::wstring filePath, HWND hWnd, bool useArea, D2D1_RECT_F area) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    auto ocrEngine = OcrEngine::TryCreateFromUserProfileLanguages();
    if (!ocrEngine) {
        PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)5);
        winrt::uninit_apartment();
        return;
    }

    try {
        auto file = StorageFile::GetFileFromPathAsync(filePath.c_str()).get();
        auto stream = file.OpenAsync(FileAccessMode::Read).get();
        auto decoder = BitmapDecoder::CreateAsync(stream).get();
        winrt::Windows::Graphics::Imaging::SoftwareBitmap softwareBitmap{ nullptr };

        if (useArea) {
            auto frame = decoder.GetFrameAsync(0).get();
            winrt::Windows::Graphics::Imaging::BitmapTransform transform;
            winrt::Windows::Graphics::Imaging::BitmapBounds bounds;
            bounds.X = static_cast<uint32_t>(floor(area.left));
            bounds.Y = static_cast<uint32_t>(floor(area.top));
            bounds.Width = static_cast<uint32_t>(ceil(area.right)) - bounds.X;
            bounds.Height = static_cast<uint32_t>(ceil(area.bottom)) - bounds.Y;

            if (bounds.Width <= 0 || bounds.Height <= 0) {
                PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)4);
                winrt::uninit_apartment();
                return;
            }

            transform.Bounds(bounds);
            softwareBitmap = frame.GetSoftwareBitmapAsync(
                BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                transform, ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage
            ).get();
        }
        else {
            softwareBitmap = decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied).get();
        }

        auto ocrResult = ocrEngine.RecognizeAsync(softwareBitmap).get();
        if (ocrResult.Text().empty()) {
            PostMessage(hWnd, WM_APP_OCR_DONE_NOTEXT, 0, (LPARAM)(useArea ? 1 : 0));
        }
        else {
            std::wstring allText;
            for (uint32_t i = 0; i < ocrResult.Lines().Size(); ++i) {
                allText += ocrResult.Lines().GetAt(i).Text().c_str();
                allText += L"\r\n";
            }
            CopyToClipboardAndNotify(hWnd, allText, useArea ? WM_APP_OCR_DONE_AREA : WM_APP_OCR_DONE_TEXT);
        }
    }
    catch (winrt::hresult_error const&) { PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)1); }
    catch (...) { PostMessage(hWnd, WM_APP_OCR_FAILED, 0, (LPARAM)2); }

    winrt::uninit_apartment();
}

void PerformOcr() {
    if (g_ctx.loadingFilePath.empty()) {
        MessageBoxW(g_ctx.hWnd, L"No image file loaded. OCR cannot be performed on pasted images.", L"OCR Error", MB_OK | MB_ICONWARNING);
        return;
    }
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::thread(PerformOcrTask, g_ctx.loadingFilePath, g_ctx.hWnd, false, D2D1_RECT_F{}).detach();
}

void PerformOcrArea(D2D1_RECT_F ocrRectLocal) {
    if (g_ctx.loadingFilePath.empty()) {
        MessageBoxW(g_ctx.hWnd, L"No image file loaded. OCR cannot be performed on pasted images.", L"OCR Error", MB_OK | MB_ICONWARNING);
        return;
    }
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::thread(PerformOcrTask, g_ctx.loadingFilePath, g_ctx.hWnd, true, ocrRectLocal).detach();
}