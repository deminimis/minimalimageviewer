#pragma once
#include "viewer.h"

std::wstring GetMetadataString(IWICMetadataQueryReader* pReader, const wchar_t* query);
std::wstring GetContainerFormatName(const GUID& guid);
std::wstring GetBitDepth(IWICBitmapFrameDecode* pFrame);