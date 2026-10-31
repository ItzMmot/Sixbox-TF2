// tests/aura_shader_test.cpp - offscreen D3D9 checks for the actual embedded shaders.
// Requires a Windows/Wine graphical session; the validation window stays hidden.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "../Unibox/src/Features/Visuals/Aura/AuraBytecode.h"
#include "../Unibox/src/Features/Visuals/Aura/AuraTexture.h"
#include "../Unibox/src/Features/Visuals/Aura/AuraPolicy.h"
constexpr int W=640,H=400;
IDirect3DDevice9* dev=nullptr;
IDirect3DTexture9 *mask=nullptr,*base=nullptr,*result=nullptr,*blur[2]{};
IDirect3DSurface9 *resultSurface=nullptr,*blurSurface[2]{},*readback=nullptr;
IDirect3DPixelShader9* programs[16]{};
std::array<bool,W*H> coverage{};
int failures=0;
void Check(bool good,const char* message) {if(!good){++failures;std::printf("FAIL: %s\n",message);}}
void HR(HRESULT value,const char* operation) {if(FAILED(value)){std::printf("HRESULT %lx: %s\n",value,operation);std::exit(2);}}
float Segment(float x,float y,float ax,float ay,float bx,float by) {
 float dx=bx-ax,dy=by-ay;float t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
 return std::hypot(x-ax-t*dx,y-ay-t*dy);
}
void FillTextures(bool empty=false) {
 D3DLOCKED_RECT mr{},br{};HR(mask->LockRect(0,&mr,nullptr,0),"lock mask");HR(base->LockRect(0,&br,nullptr,0),"lock base");
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){
  bool shape=Segment(x,y,220,270,490,160)<24 || Segment(x,y,300,246,346,325)<19
   || Segment(x,y,177,312,243,277)<33 || Segment(x,y,353,330,395,289)<27
   || (x>96&&x<157&&y>98&&y<158);
  if(std::hypot(x-319.f,y-278.f)<12)shape=false;
  if(empty)shape=false;
  coverage[y*W+x]=shape;
  reinterpret_cast<DWORD*>(static_cast<unsigned char*>(mr.pBits)+y*mr.Pitch)[x]=shape?0xffffffff:0;
  int shade=shape?100+(x*2+y)%36:22+(x/32+y/32)%2*3;
  reinterpret_cast<DWORD*>(static_cast<unsigned char*>(br.pBits)+y*br.Pitch)[x]=D3DCOLOR_ARGB(255,shade,shade+3,shade+7);
 }
 mask->UnlockRect(0);base->UnlockRect(0);
}
void Quad(int w,int h) {
 struct V {float x,y,z,w,u,v;};
 const V v[]={{-.5f,-.5f,0,1,0,0},{w-.5f,-.5f,0,1,1,0},{w-.5f,h-.5f,0,1,1,1},
  {-.5f,-.5f,0,1,0,0},{w-.5f,h-.5f,0,1,1,1},{-.5f,h-.5f,0,1,0,1}};
 HR(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(V)),"draw");
}
void Viewport(int w,int h) {D3DVIEWPORT9 vp{0,0,DWORD(w),DWORD(h),0,1};HR(dev->SetViewport(&vp),"viewport");}
struct Color {unsigned char r,g,b,a;};
        static const Color palette[14][2]={
            {{255,178,79,255},{191,65,29,255}},{{126,226,239,255},{37,94,145,255}},
            {{255,205,112,255},{218,102,45,255}},{{163,125,246,255},{64,159,210,255}},
            {{192,211,246,255},{111,127,223,255}},{{114,217,184,255},{126,111,220,255}},
            {{173,219,239,255},{85,145,190,255}},{{239,203,114,255},{65,104,114,255}},
            {{130,179,244,255},{66,82,171,255}},{{184,153,235,255},{73,108,169,255}},
            {{156,227,147,255},{43,105,83,255}},{{194,238,177,255},{44,139,100,255}},
            {{220,231,239,255},{34,45,61,255}},{{232,190,222,255},{122,176,216,255}}
        };
std::vector<DWORD> Render(int mode,int type=0,bool background=false,float time=1.25f,float opacity=.65f,float bend=0.f,float animation=1.f,bool monochrome=false) {
 HR(dev->BeginScene(),"begin");
 dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
 for(int pass=0;pass<2;++pass){
  dev->SetTexture(0,nullptr);dev->SetTexture(1,nullptr);
  HR(dev->SetRenderTarget(0,blurSurface[pass]),"blur RT");Viewport(W/2,H/2);
  dev->SetPixelShader(programs[14+pass]);
  const float offset[4]={pass==0?16.f/W:0.f,pass==1?25.6f/H:0.f,0,0};dev->SetPixelShaderConstantF(0,offset,1);
  dev->SetTexture(0,pass==0?mask:blur[0]);Quad(W/2,H/2);
 }
 dev->SetTexture(0,nullptr);dev->SetTexture(1,nullptr);HR(dev->SetRenderTarget(0,resultSurface),"result RT");Viewport(W,H);
 dev->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0);
 if(background){
  dev->SetPixelShader(nullptr);dev->SetTexture(0,base);
  dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
  dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
  Quad(W,H);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);dev->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
  dev->SetRenderState(D3DRS_DESTBLEND,(!monochrome && Aura::DefaultAdditive(mode))?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA);
 }
 const auto a=monochrome ? Color{220,224,233,255}:palette[mode][0];
 const auto b=monochrome ? Color{36,42,55,255}:palette[mode][1];
 const float c[][4]={{time,animation,3,1},{a.r/255.f,a.g/255.f,a.b/255.f,1},{b.r/255.f,b.g/255.f,b.b/255.f,1},
  {opacity,1.4f,monochrome?1.f:.3f,1},{10,1.5f,0,24},{1,.65f,.25f,.35f},{bend,0,0,.4f},{16.f/W,16.f/H,W/float(H),float(type)}};
 dev->SetPixelShader(programs[mode]);dev->SetPixelShaderConstantF(0,&c[0][0],8);
 dev->SetTexture(0,mask);dev->SetTexture(1,blur[1]);Quad(W,H);
 HR(dev->EndScene(),"end");
 HR(dev->GetRenderTargetData(resultSurface,readback),"readback");
 D3DLOCKED_RECT pixels{};HR(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),"read lock");
 std::vector<DWORD> output(W*H);
 for(int y=0;y<H;++y)std::copy_n(reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(pixels.pBits)+y*pixels.Pitch),W,output.data()+y*W);
 readback->UnlockRect();return output;
}
void Save(const char* dir,int mode,const std::vector<DWORD>& image,bool monochrome=false) {
 char name[1024];std::snprintf(name,sizeof(name),"%s/%smode-%02d.tga",dir,monochrome?"mono-":"",mode);
 auto* f=std::fopen(name,"wb");if(!f)std::exit(3);
 const unsigned char h[18]={0,0,2,0,0,0,0,0,0,0,0,0,W&255,W>>8,H&255,H>>8,32,0x28};
 std::fwrite(h,1,18,f);std::fwrite(image.data(),4,image.size(),f);std::fclose(f);
}
uint64_t Hash(const std::vector<DWORD>& image){uint64_t hash=1469598103934665603ull;for(auto p:image)hash=(hash^p)*1099511628211ull;return hash;}
int main(int argc,char** argv) {
 const bool captureOnly=argc>1 && std::strcmp(argv[1],"--capture-only")==0;
 HWND window=CreateWindowExW(0,L"STATIC",L"Aura shader validation",WS_OVERLAPPEDWINDOW,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 if(!window){std::printf("No hidden window: %lu\n",GetLastError());return 2;}
 auto* d3d=Direct3DCreate9(D3D_SDK_VERSION);if(!d3d)return 2;
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
 pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
 HR(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&dev),"device");
 D3DADAPTER_IDENTIFIER9 adapter{};d3d->GetAdapterIdentifier(0,0,&adapter);std::printf("D3D9 adapter: %s\n",adapter.Description);
 for(int i=0;i<16;++i)HR(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(Aura::Shader::Programs[i].data),&programs[i]),"pixel shader");
 HR(dev->CreateTexture(W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&mask,nullptr),"mask texture");
 HR(dev->CreateTexture(W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&base,nullptr),"base texture");
 HR(dev->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&result,nullptr),"result texture");
 HR(result->GetSurfaceLevel(0,&resultSurface),"result surface");
 HR(dev->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr),"read surface");
 for(int i=0;i<2;++i){HR(dev->CreateTexture(W/2,H/2,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&blur[i],nullptr),"blur texture");HR(blur[i]->GetSurfaceLevel(0,&blurSurface[i]),"blur surface");}
 dev->SetDepthStencilSurface(nullptr);dev->SetVertexShader(nullptr);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
 dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);dev->SetRenderState(D3DRS_LIGHTING,FALSE);
 dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
 for(int i=0;i<2;++i){dev->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
  dev->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);}
 FillTextures();
 if(!captureOnly) for(int mode=0;mode<14;++mode){
  auto full=Render(mode),outline=Render(mode,1),fill=Render(mode,2),invisible=Render(mode,0,false,1.25f,0);
  int interior=0,exterior=0,leaks=0,filledOutline=0,opaqueInvisible=0;
  for(int i=0;i<W*H;++i){
   const int alpha=full[i]>>24;
   if(coverage[i]){if(alpha>0)++interior;if((outline[i]>>24)>2)++filledOutline;}
   else{if(alpha>0)++exterior;if((fill[i]>>24)>2)++leaks;}
   if(invisible[i]>>24)++opaqueInvisible;
  }
  Check(interior>0 && exterior>0,"full has both surface and surrounding coating");
  Check(!leaks,"fill does not escape the actual model silhouette");Check(!filledOutline,"outline leaves interior clear");Check(!opaqueInvisible,"opacity zero");
  Check(Hash(full)!=Hash(Render(mode,0,false,1.25f,.65f,.7f)),"motion bends coating");
  Check(Hash(Render(mode,0,false,0,.65f,0,0))==Hash(Render(mode,0,false,10,.65f,0,0)),"animation zero freezes pattern");
  Check(Hash(full)!=Hash(Render(mode,0,false,2.75f)),"animation evolves over time");
  if(argc>1){Save(argv[1],mode,Render(mode,0,true));Save(argv[1],mode,Render(mode,0,true,1.25f,.65f,0,1,true),true);}
  std::printf("Mode %d (%s): surface %d, outer %d, fill leaks %d, interior outline %d\n",mode,Aura::ModeNames[mode],interior,exterior,leaks,filledOutline);
 }
 // Exercise the production capture helper, including non-texture surfaces.
 HR(dev->SetRenderTarget(0,resultSurface),"bind direct capture RT");
 IDirect3DTexture9* captured=nullptr;
 Check(Aura::D3D::CaptureTarget(dev,captured) && captured==result,"direct RT texture capture");
 if(captured){captured->Release();captured=nullptr;}
 auto testSurface=[&](D3DMULTISAMPLE_TYPE samples) {
  IDirect3DSurface9* surface=nullptr;
  HR(dev->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,samples,0,FALSE,&surface,nullptr),"standalone target");
  dev->SetTexture(0,nullptr);dev->SetTexture(1,nullptr);HR(dev->SetRenderTarget(0,surface),"standalone bind");
  dev->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,37,81,143),1,0);
  Check(Aura::D3D::CaptureTarget(dev,captured),"standalone/MSAA capture resolves");
  if(captured){
   auto* first=captured;
   Check(Aura::D3D::CaptureTarget(dev,captured) && captured==first,"resolve reuses existing storage");
   IDirect3DSurface9* copy=nullptr;captured->GetSurfaceLevel(0,&copy);
   HR(dev->GetRenderTargetData(copy,readback),"resolved readback");copy->Release();
   D3DLOCKED_RECT pixel{};HR(readback->LockRect(&pixel,nullptr,D3DLOCK_READONLY),"resolved lock");
   Check(*static_cast<const DWORD*>(pixel.pBits)==D3DCOLOR_ARGB(255,37,81,143),"resolved pixel matches source");
   readback->UnlockRect();captured->Release();captured=nullptr;
  }
  HR(dev->SetRenderTarget(0,resultSurface),"restore RT");surface->Release();
  std::printf("Surface capture + storage reuse: sample type %d passed\n",int(samples));std::fflush(stdout);
 };
 testSurface(D3DMULTISAMPLE_NONE);
 if(SUCCEEDED(d3d->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_4_SAMPLES,nullptr))) testSurface(D3DMULTISAMPLE_4_SAMPLES);
 else std::printf("4x MSAA unavailable on this adapter; non-MSAA resolve checked.\n");
 if(!captureOnly){FillTextures(true);auto empty=Render(0);Check(std::all_of(empty.begin(),empty.end(),[](DWORD p){return (p>>24)==0;}),"empty mask draws nothing");}
 std::printf("GPU shader checks: %d failures\n",failures);std::fflush(stdout);
 dev->SetTexture(0,nullptr);dev->SetTexture(1,nullptr);dev->SetPixelShader(nullptr);
 IDirect3DSurface9* backbuffer=nullptr;dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer);
 if(backbuffer){dev->SetRenderTarget(0,backbuffer);backbuffer->Release();}
 for(auto* p:programs)p->Release();for(int i=0;i<2;++i){blurSurface[i]->Release();blur[i]->Release();}
 resultSurface->Release();readback->Release();result->Release();mask->Release();base->Release();
 auto left=dev->Release();std::printf("D3D9 device remaining references: %lu\n",left);
 d3d->Release();DestroyWindow(window);
 std::printf("D3D9 teardown complete\n");std::fflush(stdout);
 return failures?1:0;
}
