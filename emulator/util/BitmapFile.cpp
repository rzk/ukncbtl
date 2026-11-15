/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// BitmapFile.cpp

#include "stdafx.h"
#include "BitmapFile.h"
#include "BmpFile.h"
#include "ImagePixels.h"
#ifdef UKNCBTL_SDL3_PNG
#include "PngFile.h"
#endif

#include <wincodec.h>
#include <wincodecsdk.h>
#include <vector>
#include <memory>

template <typename T> struct ComRelease
{
    void operator()(T* value) const { if (value != nullptr) value->Release(); }
};
#pragma comment(lib, "WindowsCodecs.lib")


//////////////////////////////////////////////////////////////////////
// Globals

static std::unique_ptr<IWICImagingFactory, ComRelease<IWICImagingFactory>> BitmapFile_factory;
static bool BitmapFile_comInitialized = false;


void BitmapFile_Init()
{
    if (BitmapFile_factory)
        return;
    const HRESULT initialized = BitmapFile_comInitialized ? S_FALSE : CoInitialize(nullptr);
    BitmapFile_comInitialized = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return;
    IWICImagingFactory* factory = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory))))
        BitmapFile_factory.reset(factory);
}

void BitmapFile_Done()
{
    BitmapFile_factory.reset();
    if (BitmapFile_comInitialized)
        CoUninitialize();
    BitmapFile_comInitialized = false;
}


//////////////////////////////////////////////////////////////////////


bool BitmapFile_SaveImageFile(
    std::span<const uint32_t> pixels,
    LPCTSTR sFileName, BitmapFileFormat format,
    int width, int height)
{
    ASSERT(sFileName != nullptr);

    if (format == BitmapFileFormatBmp)
        return sFileName && bmp_file::Save(std::filesystem::path(sFileName), pixels, width, height).has_value();

#ifdef UKNCBTL_SDL3_PNG
    return sFileName && png_file::Save(std::filesystem::path(sFileName), pixels, width, height).has_value();
#endif

    IWICImagingFactory * piFactory = BitmapFile_factory.get();
    ASSERT(piFactory != nullptr);
    if (piFactory == nullptr || sFileName == nullptr || width <= 0 || height <= 0 ||
        static_cast<uint64_t>(width) * height * 4 > UINT_MAX ||
        static_cast<uint64_t>(width) * height > pixels.size())
        return false;

    WICPixelFormatGUID formatGUID = GUID_WICPixelFormat24bppBGR;
    IWICBitmapEncoder* rawEncoder = nullptr;
    if (FAILED(piFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &rawEncoder))) return false;
    std::unique_ptr<IWICBitmapEncoder, ComRelease<IWICBitmapEncoder>> encoder(rawEncoder);
    IWICStream* rawStream = nullptr;
    if (FAILED(piFactory->CreateStream(&rawStream))) return false;
    std::unique_ptr<IWICStream, ComRelease<IWICStream>> stream(rawStream);
    if (FAILED(stream->InitializeFromFilename(sFileName, GENERIC_WRITE)) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache))) return false;
    IWICBitmapFrameEncode* rawFrame = nullptr;
    IPropertyBag2* rawProperties = nullptr;
    const HRESULT frameResult = encoder->CreateNewFrame(&rawFrame, &rawProperties);
    std::unique_ptr<IWICBitmapFrameEncode, ComRelease<IWICBitmapFrameEncode>> frame(rawFrame);
    std::unique_ptr<IPropertyBag2, ComRelease<IPropertyBag2>> properties(rawProperties);
    if (FAILED(frameResult)) return false;
    if (FAILED(frame->Initialize(properties.get())) || FAILED(frame->SetSize(width, height)) ||
        FAILED(frame->SetPixelFormat(&formatGUID)) || !IsEqualGUID(formatGUID, GUID_WICPixelFormat24bppBGR))
        return false;

    // Convert upside-down 32bpp bitmap to normal 24bpp bitmap
    const UINT cbStride = static_cast<UINT>(width) * 3;
    const UINT cbBufferSize = static_cast<UINT>(height) * cbStride;
    std::vector<BYTE> pixelBuffer(cbBufferSize);
    if (!image_pixels::ToTopDownBgr24(pixels, pixelBuffer, width, height)) return false;
    return SUCCEEDED(frame->WritePixels(height, cbStride, cbBufferSize, pixelBuffer.data())) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}


//////////////////////////////////////////////////////////////////////
