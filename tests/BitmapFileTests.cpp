/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "stdafx.h"
#include <gtest/gtest.h>
#include "util/BitmapFile.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <wincodec.h>

#ifdef _DEBUG
BOOL AssertFailedLine(LPCSTR file, int line)
{
    std::fprintf(stderr, "Assertion failed at %s:%d\n", file, line);
    std::abort();
}
#endif

template<class T> struct ReleaseCom
{
    void operator()(T* value) const { if (value) value->Release(); }
};
template<class T> using ComPtr = std::unique_ptr<T, ReleaseCom<T>>;

bool HasExpectedPixels(LPCTSTR path)
{
    IWICImagingFactory* rawFactory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rawFactory)))) return false;
    ComPtr<IWICImagingFactory> factory(rawFactory);
    IWICBitmapDecoder* rawDecoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &rawDecoder))) return false;
    ComPtr<IWICBitmapDecoder> decoder(rawDecoder);
    IWICBitmapFrameDecode* rawFrame = nullptr;
    if (FAILED(decoder->GetFrame(0, &rawFrame))) return false;
    ComPtr<IWICBitmapFrameDecode> frame(rawFrame);
    IWICFormatConverter* rawConverter = nullptr;
    if (FAILED(factory->CreateFormatConverter(&rawConverter))) return false;
    ComPtr<IWICFormatConverter> converter(rawConverter);
    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    std::array<uint8_t, 16> pixels{};
    if (FAILED(converter->CopyPixels(nullptr, 8, static_cast<UINT>(pixels.size()), pixels.data()))) return false;
    // WIC rows are top-down; each pixel is BGRA.
    constexpr std::array<uint8_t, 16> expected{
        0xff,0x00,0x00,0xff, 0xff,0xff,0xff,0xff,
        0x00,0x00,0xff,0xff, 0x00,0xff,0x00,0xff};
    return pixels == expected;
}

class BitmapEdges : public testing::Test {
protected:
    std::array<TCHAR,MAX_PATH> path{};
    const std::array<uint32_t,4> pixels{0x00ff0000,0x0000ff00,0x000000ff,0x00ffffff};
#ifdef UKNCBTL_SDL3_PNG
    bool decoderComInitialized = false;
#endif
    void SetUp() override {
        std::array<TCHAR,MAX_PATH> directory{}; ASSERT_NE(0,GetTempPath(MAX_PATH,directory.data()));
        ASSERT_NE(0,GetTempFileName(directory.data(),L"bme",0,path.data()));
#ifdef UKNCBTL_SDL3_PNG
        decoderComInitialized = SUCCEEDED(CoInitialize(nullptr));
        ASSERT_TRUE(decoderComInitialized);
#endif
        BitmapFile_Init();
    }
    void TearDown() override {
        BitmapFile_Done();
#ifdef UKNCBTL_SDL3_PNG
        if (decoderComInitialized) CoUninitialize();
#endif
        DeleteFile(path.data());
    }
    bool Save(BitmapFileFormat format=BitmapFileFormatPng) { return BitmapFile_SaveImageFile(pixels,path.data(),format,2,2); }
};
TEST_F(BitmapEdges, ShortPixelSpanDoesNotOverwriteExistingImage) {
    ASSERT_TRUE(Save());
    EXPECT_FALSE(BitmapFile_SaveImageFile(std::span(pixels).first(3),path.data(),BitmapFileFormatPng,2,2));
    EXPECT_TRUE(HasExpectedPixels(path.data()));
}
TEST_F(BitmapEdges, AllFormatsRoundTripAndReleaseFileForDeletion) {
    for(auto format : {BitmapFileFormatBmp,BitmapFileFormatPng}) {
        ASSERT_TRUE(Save(format)); EXPECT_TRUE(HasExpectedPixels(path.data())); EXPECT_TRUE(DeleteFile(path.data()));
    }
}
TEST_F(BitmapEdges, InvalidDimensionsAndShortSpanPreserveExistingFile) {
    ASSERT_TRUE(Save());
    for(auto size : {std::pair{0,2},std::pair{2,0},std::pair{-1,2},std::pair{2,-1},std::pair{INT_MAX,INT_MAX},std::pair{3,2}})
        EXPECT_FALSE(BitmapFile_SaveImageFile(pixels,path.data(),BitmapFileFormatPng,size.first,size.second));
    EXPECT_TRUE(HasExpectedPixels(path.data()));
}
TEST_F(BitmapEdges, LockedDestinationFailsAndCanBeRetried) {
    HANDLE lock=CreateFile(path.data(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr); ASSERT_NE(INVALID_HANDLE_VALUE,lock);
    EXPECT_FALSE(Save()); CloseHandle(lock); EXPECT_TRUE(Save()); EXPECT_TRUE(HasExpectedPixels(path.data()));
}
TEST_F(BitmapEdges, MissingParentFailsAndLaterSaveSucceeds) {
    const auto missing=std::wstring(path.data())+L"/missing/output.png";
    EXPECT_FALSE(BitmapFile_SaveImageFile(pixels,missing.c_str(),BitmapFileFormatPng,2,2)); EXPECT_TRUE(Save());
}
TEST_F(BitmapEdges, UnknownFormatFallsBackToPng) {
    for (auto format : {3, 99}) {
        ASSERT_TRUE(Save(static_cast<BitmapFileFormat>(format)));
        std::ifstream file(std::filesystem::path(path.data()), std::ios::binary);
        std::array<unsigned char, 8> signature{};
        ASSERT_TRUE(file.read(reinterpret_cast<char*>(signature.data()), signature.size()));
        constexpr std::array<unsigned char, 8> pngSignature{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        EXPECT_EQ(signature, pngSignature);
        EXPECT_TRUE(HasExpectedPixels(path.data()));
    }
}
#ifdef UKNCBTL_SDL3_PNG
TEST_F(BitmapEdges, PngSaveDoesNotRequireWindowsCodecInitialization) {
    BitmapFile_Done();
    ASSERT_TRUE(Save());
    BitmapFile_Init();
    EXPECT_TRUE(HasExpectedPixels(path.data()));
}
#endif
TEST_F(BitmapEdges, DoneIsIdempotentAndInitializationCanBeRepeated) {
    BitmapFile_Done(); BitmapFile_Done(); BitmapFile_Init(); ASSERT_TRUE(Save());
    BitmapFile_Done(); BitmapFile_Init(); ASSERT_TRUE(Save());
}
TEST_F(BitmapEdges, RepeatedInitBalancesComInitialization) {
    BitmapFile_Init(); BitmapFile_Done();
#ifdef UKNCBTL_SDL3_PNG
    CoUninitialize(); // Drop the test decoder's apartment before probing.
    decoderComInitialized = false;
#endif
    const HRESULT changed=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    EXPECT_TRUE(SUCCEEDED(changed)) << std::hex << changed;
    if(SUCCEEDED(changed)) CoUninitialize();
    else if(changed==RPC_E_CHANGED_MODE) CoUninitialize(); // Clean up the leaked reference on regression failure.
#ifdef UKNCBTL_SDL3_PNG
    decoderComInitialized = SUCCEEDED(CoInitialize(nullptr)); // Restore it for TearDown.
    EXPECT_TRUE(decoderComInitialized);
#endif
}
TEST(BitmapLifecycle, ExistingMultithreadedApartmentRemainsOwnedByCaller) {
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED); ASSERT_TRUE(SUCCEEDED(initialized));
    BitmapFile_Init();
    BitmapFile_Done(); const HRESULT probe=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); EXPECT_EQ(RPC_E_CHANGED_MODE,probe);
    if(SUCCEEDED(probe)) CoUninitialize(); CoUninitialize();
}
TEST_F(BitmapEdges, OddWidthAndSinglePixelSaveWithCorrectOrientation) {
    for(auto size : {std::pair{1,1},std::pair{3,5}}) {
        const int width=size.first,height=size.second; std::vector<uint32_t> input(width*height+1,0xdeadbeef);
        for(int i=0;i<width*height;++i) input[i]=0xa0000000|static_cast<uint32_t>(i*0x010307);
        ASSERT_TRUE(BitmapFile_SaveImageFile(input,path.data(),BitmapFileFormatPng,width,height));
        IWICImagingFactory* raw=nullptr; ASSERT_TRUE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&raw)))); ComPtr<IWICImagingFactory> factory(raw);
        IWICBitmapDecoder* decoderRaw=nullptr; ASSERT_TRUE(SUCCEEDED(factory->CreateDecoderFromFilename(path.data(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoderRaw))); ComPtr<IWICBitmapDecoder> decoder(decoderRaw);
        IWICBitmapFrameDecode* frameRaw=nullptr; ASSERT_TRUE(SUCCEEDED(decoder->GetFrame(0,&frameRaw))); ComPtr<IWICBitmapFrameDecode> frame(frameRaw);
        IWICFormatConverter* converterRaw=nullptr; ASSERT_TRUE(SUCCEEDED(factory->CreateFormatConverter(&converterRaw))); ComPtr<IWICFormatConverter> converter(converterRaw);
        ASSERT_TRUE(SUCCEEDED(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)));
        UINT w=0,h=0; ASSERT_TRUE(SUCCEEDED(converter->GetSize(&w,&h))); EXPECT_EQ(width,w); EXPECT_EQ(height,h);
        std::vector<uint32_t> output(width*height); ASSERT_TRUE(SUCCEEDED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(output.size()*4),reinterpret_cast<BYTE*>(output.data()))));
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) EXPECT_EQ(0xff000000|(input[(height-1-y)*width+x]&0xffffff),output[y*width+x]);
        EXPECT_EQ(0xdeadbeef,input.back());
    }
}
