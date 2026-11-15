#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <stdexcept>
#include <iostream>
using Microsoft::WRL::ComPtr;
void ok(HRESULT result){if(FAILED(result))throw std::runtime_error("D3D9 stencil test operation failed");}
struct Vertex{float x,y,z,rhw;D3DCOLOR color;};
void rectangle(IDirect3DDevice9* device,float left,float top,float right,float bottom,D3DCOLOR color)
{
    Vertex vertices[]={{left-.5f,top-.5f,0,1,color},{right-.5f,top-.5f,0,1,color},
                       {left-.5f,bottom-.5f,0,1,color},{right-.5f,bottom-.5f,0,1,color}};
    ok(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
}
int main()
{
    try
    {
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LocalStencilTest";
        RegisterClassW(&cls);
        HWND window=CreateWindowW(cls.lpszClassName,L"Private stencil GPU test",WS_OVERLAPPEDWINDOW,0,0,96,96,nullptr,nullptr,cls.hInstance,nullptr);
        if(!window)throw std::runtime_error("Test window creation failed");
        ComPtr<IDirect3D9> api;api.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if(!api)throw std::runtime_error("D3D9 unavailable");
        D3DPRESENT_PARAMETERS present{};present.Windowed=TRUE;present.SwapEffect=D3DSWAPEFFECT_DISCARD;
        present.hDeviceWindow=window;present.BackBufferWidth=64;present.BackBufferHeight=64;present.BackBufferFormat=D3DFMT_UNKNOWN;
        ComPtr<IDirect3DDevice9> device;
        ok(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&present,&device));
        ComPtr<IDirect3DSurface9> color,gameDepth,privateDepth,read;
        ok(device->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&color,nullptr));
        ok(device->CreateDepthStencilSurface(64,64,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&gameDepth,nullptr));
        ok(device->CreateDepthStencilSurface(64,64,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&privateDepth,nullptr));
        ok(device->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
        ok(device->SetRenderTarget(0,color.Get()));
        device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_LIGHTING,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        const auto mask=[&]
        {
            device->SetRenderState(D3DRS_STENCILENABLE,TRUE);device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS);
            device->SetRenderState(D3DRS_STENCILREF,1);device->SetRenderState(D3DRS_STENCILMASK,0xff);
            device->SetRenderState(D3DRS_STENCILWRITEMASK,0xff);device->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE);
            device->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_REPLACE);device->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP);
            device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
            device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
            device->SetRenderState(D3DRS_COLORWRITEENABLE,0);
            rectangle(device.Get(),16,16,48,48,0xffffffff);
            device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
        };
        for(bool rebuild:{false,true})
        {
            ok(device->SetDepthStencilSurface(gameDepth.Get()));ok(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));
            ok(device->BeginScene());mask();ok(device->EndScene());
            ok(device->SetDepthStencilSurface(privateDepth.Get()));ok(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));
            ok(device->BeginScene());if(rebuild)mask();
            device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL);
            device->SetRenderState(D3DRS_STENCILREF,0);device->SetRenderState(D3DRS_STENCILWRITEMASK,0);
            device->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP);
            rectangle(device.Get(),14,14,50,50,0xffff8000);ok(device->EndScene());
            ok(device->GetRenderTargetData(color.Get(),read.Get()));D3DLOCKED_RECT lock{};ok(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            const auto at=[&](int x,int y){return reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch)[x]&0xffffff;};
            const auto center=at(32,32),edge=at(15,32),outside=at(8,32);read->UnlockRect();
            if(!edge||outside||(rebuild?center!=0:center==0))throw std::runtime_error("Stencil interior/edge regression");
            std::cout<<(rebuild?"Own stencil surface: transparent interior, visible outline\n":"Wrong surface: reproduced filled model\n");
        }
        device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,nullptr);DestroyWindow(window);
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
