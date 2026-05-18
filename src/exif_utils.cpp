#include "exif_utils.h"
#include "viewer.h"
#include <propvarutil.h>



#include <propsys.h>
#include <propkey.h>

std::wstring GetPropertyString(IPropertyStore* pStore, REFPROPERTYKEY key) {
    if (!pStore) return L"N/A";

    PROPVARIANT propValue;
    PropVariantInit(&propValue);
    std::wstring val = L"N/A";

    if (SUCCEEDED(pStore->GetValue(key, &propValue))) {
        PWSTR pszDisplayValue = nullptr;
        if (SUCCEEDED(PSFormatForDisplayAlloc(key, propValue, PDFF_DEFAULT, &pszDisplayValue))) {
            if (pszDisplayValue && wcslen(pszDisplayValue) > 0) {
                val = pszDisplayValue;
            }
            CoTaskMemFree(pszDisplayValue);
        }
        PropVariantClear(&propValue);
    }
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

std::wstring GetBitDepth(IWICBitmapFrameDecode* pFrame, IWICImagingFactory* wicFactory) {
    WICPixelFormatGUID pixelFormatGuid;
    if (FAILED(pFrame->GetPixelFormat(&pixelFormatGuid))) {
        return L"N/A";
    }

    ComPtr<IWICComponentInfo> componentInfo;
    if (FAILED(wicFactory->CreateComponentInfo(pixelFormatGuid, &componentInfo))) {
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