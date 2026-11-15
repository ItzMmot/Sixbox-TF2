#pragma once
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#pragma comment(lib, "windowscodecs.lib")
namespace LocalExtension
{
#pragma pack(push,1)
struct ModelHeader
{
    char magic[8] = {'S','X','M','S','K','0','0','1'};
    uint64_t session = 0, sequence = 0, capturedAt = 0;
    uint32_t width = 0, height = 0, x = 0, y = 0, cropWidth = 0, cropHeight = 0, bytes = 0;
};
#pragma pack(pop)
static_assert(sizeof(ModelHeader) == 60);
struct ModelPixels
{
    std::vector<unsigned char> pixels;
    unsigned width = 0, height = 0;
    uint64_t capturedAt = 0, sequence = 0;
};
inline void WicCheck(HRESULT result)
{
    if (FAILED(result)) throw std::runtime_error("Native PNG encoder failed");
}
class ModelEncoder
{
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    bool com = false;
public:
    ~ModelEncoder() { factory.Reset(); if (com) CoUninitialize(); }
    std::vector<unsigned char> Encode(const ModelPixels& input, uint64_t session)
    {
        if (!input.width || !input.height || input.width > 8192 || input.height > 8192
            || uint64_t(input.width)*input.height > 33177600 || input.pixels.size() != size_t(input.width)*input.height*3)
            throw std::runtime_error("Invalid model image dimensions");
        ModelHeader header; header.session=session; header.sequence=input.sequence; header.capturedAt=input.capturedAt;
        header.width=input.width; header.height=input.height;
        unsigned left=input.width,top=input.height,right=0,bottom=0;
        for (unsigned y=0;y<input.height;++y)
        {
            const auto* row=input.pixels.data()+size_t(y)*input.width*3;
            for (unsigned x=0;x<input.width;++x)
            {
                const auto* rgb=row+x*3;
                if (!(rgb[0]|rgb[1]|rgb[2])) continue;
                left=(std::min)(left,x);right=(std::max)(right,x+1);
                top=(std::min)(top,y);bottom=(std::max)(bottom,y+1);
            }
        }
        std::vector<unsigned char> output(sizeof(header));
        if (right>left && bottom>top)
        {
            header.x=left;header.y=top;header.cropWidth=right-left;header.cropHeight=bottom-top;
            std::vector<unsigned char> bgra(size_t(header.cropWidth)*header.cropHeight*4);
            for (unsigned y=top;y<bottom;++y)
            {
                const auto* source=input.pixels.data()+(size_t(y)*input.width+left)*3;
                auto* dest=bgra.data()+size_t(y-top)*header.cropWidth*4;
                for (unsigned x=0;x<header.cropWidth;++x,source+=3,dest+=4)
                {
                    const unsigned alpha=(std::max)({source[0],source[1],source[2]});
                    if (!alpha) continue;
                    dest[0]=static_cast<unsigned char>((source[2]*255+alpha/2)/alpha);
                    dest[1]=static_cast<unsigned char>((source[1]*255+alpha/2)/alpha);
                    dest[2]=static_cast<unsigned char>((source[0]*255+alpha/2)/alpha);
                    dest[3]=static_cast<unsigned char>(alpha);
                }
            }
            if (!factory)
            {
                if (!com)
                {
                    WicCheck(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
                    com=true;
                }
                WicCheck(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
            }
            Microsoft::WRL::ComPtr<IStream> stream;
            Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
            Microsoft::WRL::ComPtr<IPropertyBag2> properties;
            WicCheck(CreateStreamOnHGlobal(nullptr,TRUE,&stream));
            WicCheck(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
            WicCheck(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
            WicCheck(encoder->CreateNewFrame(&frame,&properties));
            WicCheck(frame->Initialize(properties.Get()));
            WicCheck(frame->SetSize(header.cropWidth,header.cropHeight));
            auto format=GUID_WICPixelFormat32bppBGRA;
            WicCheck(frame->SetPixelFormat(&format));
            if (!IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)) throw std::runtime_error("PNG encoder pixel format mismatch");
            WicCheck(frame->WritePixels(header.cropHeight,header.cropWidth*4,UINT(bgra.size()),bgra.data()));
            WicCheck(frame->Commit());WicCheck(encoder->Commit());
            STATSTG stats{};WicCheck(stream->Stat(&stats,STATFLAG_NONAME));
            if (stats.cbSize.QuadPart>16*1024*1024) throw std::runtime_error("Model PNG exceeds transport limit");
            header.bytes=stats.cbSize.LowPart;
            LARGE_INTEGER origin{};WicCheck(stream->Seek(origin,STREAM_SEEK_SET,nullptr));
            output.resize(sizeof(header)+header.bytes);
            ULONG read=0;WicCheck(stream->Read(output.data()+sizeof(header),header.bytes,&read));
            if (read!=header.bytes) throw std::runtime_error("Incomplete PNG output");
        }
        memcpy(output.data(),&header,sizeof(header));
        return output;
    }
};
}
