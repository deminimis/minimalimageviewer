#include "exif_utils.h"
#include "viewer.h"
#include <propvarutil.h>

extern AppContext g_ctx;

std::wstring GetMetadataString(IWICMetadataQueryReader* pReader, const wchar_t* query) {
    PROPVARIANT propValue;
    PropVariantInit(&propValue);
    std::wstring val = L"N/A";
    if (FAILED(pReader->GetMetadataByName(query, &propValue))) {
        PropVariantClear(&propValue);
        return val;
    }

    wchar_t buffer[256] = { 0 };

    if (wcscmp(query, L"/app1/ifd/exif/{rational=33437}") == 0 && propValue.vt == (VT_UI4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double fstop_val = (double)propValue.caul.pElems[0] / (double)propValue.caul.pElems[1];
        swprintf_s(buffer, 256, L"f/%.1f", fstop_val);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{rational=33434}") == 0 && propValue.vt == (VT_UI4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double num = (double)propValue.caul.pElems[0];
        double den = (double)propValue.caul.pElems[1];
        if (den == 0) den = 1.0;
        if (num == 1 && den > 1) {
            swprintf_s(buffer, 256, L"1/%.0f s", den);
        }
        else {
            swprintf_s(buffer, 256, L"%.4f s", num / den);
        }
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=34855}") == 0 && propValue.vt == VT_UI2) {
        swprintf_s(buffer, 256, L"ISO %u", propValue.uiVal);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{rational=37386}") == 0 && propValue.vt == (VT_UI4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double focal_val = (double)propValue.caul.pElems[0] / (double)propValue.caul.pElems[1];
        swprintf_s(buffer, 256, L"%.0f mm", focal_val);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=41989}") == 0 && propValue.vt == VT_UI2) {
        swprintf_s(buffer, 256, L"%u mm", propValue.uiVal);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{srational=37380}") == 0 && propValue.vt == (VT_I4 | VT_VECTOR) && propValue.caul.cElems == 2) {
        double bias_val = (double)propValue.caul.pElems[0] / (double)propValue.caul.pElems[1];
        swprintf_s(buffer, 256, L"%.2f EV", bias_val);
        val = buffer;
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=37383}") == 0 && propValue.vt == VT_UI2) {
        switch (propValue.uiVal) {
        case 0: val = L"Unknown"; break;
        case 1: val = L"Average"; break;
        case 2: val = L"Center Weighted Average"; break;
        case 3: val = L"Spot"; break;
        case 4: val = L"Multi-spot"; break;
        case 5: val = L"Pattern"; break;
        case 6: val = L"Partial"; break;
        default: val = L"Other"; break;
        }
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=37385}") == 0 && propValue.vt == VT_UI2) {
        if (propValue.uiVal & 0x1) val = L"Fired";
        else val = L"Did not fire";
        if (propValue.uiVal & 0x4) val += L", Strobe";
        if (propValue.uiVal & 0x10) val += L", Auto";
        if (propValue.uiVal & 0x40) val += L", Red-eye reduction";
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=34850}") == 0 && propValue.vt == VT_UI2) {
        switch (propValue.uiVal) {
        case 0: val = L"Not defined"; break;
        case 1: val = L"Manual"; break;
        case 2: val = L"Normal program (Auto)"; break;
        case 3: val = L"Aperture priority"; break;
        case 4: val = L"Shutter priority"; break;
        case 5: val = L"Creative program"; break;
        case 6: val = L"Action program"; break;
        case 7: val = L"Portrait mode"; break;
        case 8: val = L"Landscape mode"; break;
        default: val = L"Other"; break;
        }
    }
    else if (wcscmp(query, L"/app1/ifd/exif/{ushort=41987}") == 0 && propValue.vt == VT_UI2) {
        switch (propValue.uiVal) {
        case 0: val = L"Auto"; break;
        case 1: val = L"Manual"; break;
        default: val = L"Other"; break;
        }
    }
    else if (SUCCEEDED(PropVariantToString(propValue, buffer, 256))) {
        val = buffer;
    }

    PropVariantClear(&propValue);
    return val;
}

std::wstring GetContainerFormatName(const GUID& guid) {
    if (guid == GUID_ContainerFormatPng) return L"PNG";
    if (guid == GUID_ContainerFormatJpeg) return L"JPEG";
    if (guid == GUID_ContainerFormatBmp) return L"BMP";
    if (guid == GUID_ContainerFormatGif) return L"GIF";
    if (guid == GUID_ContainerFormatTiff) return L"TIFF";
    if (guid == GUID_ContainerFormatIco) return L"ICO";
    if (guid == GUID_ContainerFormatWmp) return L"HD Photo / JPEG XR";
    if (guid == GUID_ContainerFormatDds) return L"DDS";
    if (guid == GUID_ContainerFormatHeif) return L"HEIF";
    return L"Unknown";
}

std::wstring GetBitDepth(IWICBitmapFrameDecode* pFrame) {
    WICPixelFormatGUID pixelFormatGuid;
    if (FAILED(pFrame->GetPixelFormat(&pixelFormatGuid))) {
        return L"N/A";
    }

    ComPtr<IWICComponentInfo> componentInfo;
    if (FAILED(g_ctx.wicFactory->CreateComponentInfo(pixelFormatGuid, &componentInfo))) {
        return L"N/A";
    }

    ComPtr<IWICPixelFormatInfo> pixelFormatInfo;
    if (FAILED(componentInfo->QueryInterface(IID_PPV_ARGS(&pixelFormatInfo)))) {
        return L"N/A";
    }

    UINT bpp = 0;
    if (SUCCEEDED(pixelFormatInfo->GetBitsPerPixel(&bpp))) {
        return std::to_wstring(bpp) + L"-bit";
    }
    return L"N/A";
}