#pragma once
#include "viewer.h"

#include <propsys.h>
#include <propkey.h>

std::wstring GetPropertyString(IPropertyStore* pStore, REFPROPERTYKEY key);
std::wstring GetContainerFormatName(const GUID& guid);
std::wstring GetBitDepth(IWICBitmapFrameDecode* pFrame, IWICImagingFactory* wicFactory);