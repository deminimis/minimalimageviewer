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

std::wstring GetContainerFormatName(const GUID& guid, IWICImagingFactory* wicFactory) {
    if (!wicFactory) return L"Unknown";

    ComPtr<IWICComponentInfo> componentInfo;
    if (FAILED(wicFactory->CreateComponentInfo(guid, &componentInfo))) {
        return L"Unknown";
    }

    UINT cchActual = 0;
    // Get buffer length
    if (FAILED(componentInfo->GetFriendlyName(0, nullptr, &cchActual)) || cchActual == 0) {
        return L"Unknown";
    }

    // Fetch name
    std::wstring name(cchActual, L'\0');
    if (SUCCEEDED(componentInfo->GetFriendlyName(cchActual, name.data(), &cchActual))) {
        name.resize(cchActual > 0 ? cchActual - 1 : 0); 
        return name;
    }

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