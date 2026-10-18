#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PaletteIO.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

namespace imageio {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr) {
    if(FAILED(hr)) throw std::runtime_error("Cannot decode PNG (invalid, truncated or unsupported image).");
}
struct ComScope {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ComScope() {if(FAILED(hr) && hr!=RPC_E_CHANGED_MODE) check(hr);}
    ~ComScope() {if(SUCCEEDED(hr)) CoUninitialize();}
};
}
inline Bitmap readPng(const fs::path& path) {
    using namespace imageio;
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
    GUID container{};check(decoder->GetContainerFormat(&container));
    if(container!=GUID_ContainerFormatPng) throw std::runtime_error("Not a PNG file.");
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame));
    UINT width{},height{};check(frame->GetSize(&width,&height));
    if(!width || !height || width>INT32_MAX || height>INT32_MAX) throw std::runtime_error("Invalid PNG dimensions.");
    Bitmap result;result.png=true;result.size=fs::file_size(path);result.width=width;
    result.height=-static_cast<int64_t>(height);result.bits=24;
    // PNG IHDR supplies the original sample depth and color type.
    std::array<unsigned char,29> header{};std::ifstream input(path,std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(header.data()),header.size())) throw std::runtime_error("Truncated PNG header.");
    unsigned channels=header[25]==2?3:header[25]==4?2:header[25]==6?4:1;
    result.sourceBits=header[24]*channels;
    if(header[25]==3) {
        ComPtr<IWICPalette> palette;check(factory->CreatePalette(&palette));check(frame->CopyPalette(palette.Get()));
        UINT count{},actual{};check(palette->GetColorCount(&count));
        if(!count || count>256) throw std::runtime_error("Invalid PNG palette.");
        std::vector<WICColor> values(count);check(palette->GetColors(count,values.data(),&actual));
        if(actual!=count) throw std::runtime_error("Truncated PNG palette.");
        for(auto value:values) result.palette.push_back(color(value&0xffffff));
    }
    uint64_t stride=uint64_t(width)*3,bytes=stride*height;
    if(bytes>UINT32_MAX || bytes>std::numeric_limits<size_t>::max()) throw std::runtime_error("PNG exceeds decoder buffer size limit.");
    ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter));
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat24bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    result.pixels.resize(static_cast<size_t>(bytes));
    check(converter->CopyPixels(nullptr,static_cast<UINT>(stride),static_cast<UINT>(bytes),result.pixels.data()));
    return result;
}
inline Bitmap readImage(fs::path path) {
    path=bmpPath(path);
    auto extension=lower(path.extension().wstring());
    if(extension==L".png") return readPng(path);
    if(extension==L".bmp") return readBitmap(path);
    throw std::runtime_error("Input image must be .bmp or .png (omitted extension means .bmp).");
}
