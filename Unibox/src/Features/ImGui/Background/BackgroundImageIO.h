#pragma once
#include "BackgroundPolicy.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

namespace MenuBackground
{
    struct ComScope
    {
        HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        ~ComScope() {if(SUCCEEDED(result)) CoUninitialize();}
        bool Valid() const {return SUCCEEDED(result)||result==RPC_E_CHANGED_MODE;}
    };
    inline bool Decode(const std::filesystem::path& path,Image& out,std::string& error,int longest=MaxDimension)
    {
        using Microsoft::WRL::ComPtr;
        out={};ComScope com;
        auto fail=[&](const char* text){error=text;return false;};
        if(!com.Valid()) return fail("Could not initialize the image decoder.");
        ComPtr<IWICImagingFactory> factory;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))
            return fail("Windows image decoder is unavailable.");
        ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
        if(FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))
            ||FAILED(decoder->GetFrame(0,&frame))) return fail("Cannot open this image. Try PNG, JPEG, BMP, GIF or TIFF.");
        UINT width=0,height=0;
        if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>16384||height>16384||uint64_t(width)*height>64000000)
            return fail("Image is empty or too large (maximum 64 megapixels / 16384 per side).");
        ComPtr<IWICBitmapSource> source=frame;
        // Respect common camera/phone orientation metadata before resizing.
        ComPtr<IWICMetadataQueryReader> metadata;
        if(SUCCEEDED(frame->GetMetadataQueryReader(&metadata)))
        {
            PROPVARIANT v{};
            HRESULT result=metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&v);
            if(FAILED(result)) result=metadata->GetMetadataByName(L"/ifd/{ushort=274}",&v);
            const unsigned orientation=SUCCEEDED(result)&&v.vt==VT_UI2?v.uiVal:1;
            PropVariantClear(&v);
            static constexpr WICBitmapTransformOptions transforms[]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,
                WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,
                WICBitmapTransformOptions(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,
                WICBitmapTransformOptions(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
            if(orientation>1&&orientation<=8)
            {
                ComPtr<IWICBitmapFlipRotator> rotate;
                if(SUCCEEDED(factory->CreateBitmapFlipRotator(&rotate))&&SUCCEEDED(rotate->Initialize(source.Get(),transforms[orientation]))) source=rotate;
            }
        }
        source->GetSize(&width,&height);
        out.naturalWidth=int(width);out.naturalHeight=int(height);
        longest=std::clamp(longest,1,MaxDimension);
        if((std::max)(width,height)>UINT(longest))
        {
            const float ratio=float(longest)/float((std::max)(width,height));
            width=(std::max)(1u,UINT(std::lround(width*ratio)));height=(std::max)(1u,UINT(std::lround(height*ratio)));
            ComPtr<IWICBitmapScaler> scaler;
            if(FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(source.Get(),width,height,WICBitmapInterpolationModeFant)))
                return fail("Could not resize the image.");
            source=scaler;
        }
        ComPtr<IWICFormatConverter> converter;
        if(FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return fail("Unsupported image pixel format.");
        out.width=int(width);out.height=int(height);out.bgra.resize(size_t(width)*height*4);
        if(FAILED(converter->CopyPixels(nullptr,width*4,UINT(out.bgra.size()),out.bgra.data()))) {out={};return fail("Could not decode the image pixels.");}
        error.clear();return true;
    }
}
