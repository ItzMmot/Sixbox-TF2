// tests/menu_background_wic_test.cpp — actual Windows WIC decoder / file paths.
#include "../Unibox/src/Features/ImGui/Background/BackgroundImageIO.h"
#include <cstdio>
int wmain(int argc,wchar_t** argv)
{
    if(argc!=2) return 1;
    const std::filesystem::path folder(argv[1]);MenuBackground::Image image;std::string error;
    if(!MenuBackground::Decode(folder/L"тест.png",image,error)||image.width!=2||image.height!=2
        ||image.bgra[2]!=255||image.bgra[3]!=255||image.bgra[7]!=128)
        {std::fprintf(stderr,"PNG/Unicode decode failed: %s\n",error.c_str());return 2;}
    if(!MenuBackground::Decode(folder/L"fixture.bmp",image,error)||image.width!=2||image.height!=2)
        {std::fprintf(stderr,"BMP decode failed: %s\n",error.c_str());return 3;}
    if(!MenuBackground::Decode(folder/L"transparent.gif",image,error)||image.width!=1||image.height!=1||image.bgra[3]!=0)
        {std::fprintf(stderr,"GIF decode failed: %s\n",error.c_str());return 7;}
    {
        MenuBackground::ComScope com;
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Microsoft::WRL::ComPtr<IWICStream> stream;
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        if(!com.Valid()||FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))
            ||FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromFilename((folder/L"fixture.jpg").c_str(),GENERIC_WRITE))
            ||FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))
            ||FAILED(encoder->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(2,2))) return 8;
        WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
        BYTE pixels[]={0,0,255,0,255,0,255,0,0,0,255,255};
        if(FAILED(frame->SetPixelFormat(&format))||format!=GUID_WICPixelFormat24bppBGR
            ||FAILED(frame->WritePixels(2,6,sizeof(pixels),pixels))||FAILED(frame->Commit())||FAILED(encoder->Commit())) return 9;
    }
    if(!MenuBackground::Decode(folder/L"fixture.jpg",image,error)||image.width!=2||image.height!=2||image.bgra[3]!=255)
        {std::fprintf(stderr,"JPEG decode failed: %s\n",error.c_str());return 10;}
    if(!MenuBackground::Decode(folder/L"wide.png",image,error)||image.width!=2048||image.height!=2||image.naturalWidth!=5000||image.naturalHeight!=4)
        {std::fprintf(stderr,"Bounded resize failed: %s\n",error.c_str());return 4;}
    if(MenuBackground::Decode(folder/L"missing.png",image,error)||!image.bgra.empty()||error.empty()) return 5;
    if(MenuBackground::Decode(folder/L"invalid.png",image,error)||!image.bgra.empty()||error.empty()) return 6;
    std::puts("PASS: WIC PNG alpha, JPEG, GIF, Unicode paths, BMP, bounded resize/natural dimensions, missing/corrupt files.");
    return 0;
}
