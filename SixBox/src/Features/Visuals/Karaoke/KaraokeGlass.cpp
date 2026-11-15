#include "KaraokeGlass.h"
#ifndef TEXTMODE
#include "KaraokeGlassBytecode.h"
#include <algorithm>
#include <cstring>

namespace Karaoke
{
    namespace {template<class T>void Drop(T*& p){if(p){p->Release();p=nullptr;}}}
    void Glass::Release()
    {
        std::lock_guard lock(mutex);
        for(auto& surface:surfaces)Drop(surface);
        for(auto& texture:textures)Drop(texture);
        Drop(blurShader);Drop(glassShader);Drop(textShader);Drop(vertexShader);Drop(state);
        device=nullptr;width=height=0;ready=false;failed=false;
    }
    void Glass::Quad(unsigned w,unsigned h)
    {
        struct Vertex {float x,y,z;DWORD color;float u,v;};
        const Vertex vertices[]={{0,0,0,0xffffffff,0,0},{float(w),0,0,0xffffffff,1,0},
            {0,float(h),0,0xffffffff,0,1},{float(w),float(h),0,0xffffffff,1,1}};
        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
    }
    bool Glass::Capture(IDirect3DDevice9* current,const GlassStyle& style)
    {
        std::lock_guard lock(mutex);
        ready=false;passCount=0;
        if(style.mode==0||!current)return false;
        if(device!=current){Release();device=current;}
        if(failed)return false;
        IDirect3DSurface9* target=nullptr,*depth=nullptr;
        D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
        if(FAILED(device->GetRenderTarget(0,&target))||!target)return false;
        target->GetDesc(&desc);device->GetViewport(&viewport);
        if(width!=desc.Width||height!=desc.Height){Release();device=current;width=desc.Width;height=desc.Height;}
        if(!width||!height){Drop(target);return false;}
        auto initialize=[&]
        {
            if(!vertexShader&&FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(Shader::Vertex),&vertexShader)))return false;
            if(!blurShader&&FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(Shader::Blur),&blurShader)))return false;
            if(!glassShader&&FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(Shader::Glass),&glassShader)))return false;
            if(!textShader&&FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(Shader::Text),&textShader)))return false;
            if(!state&&FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)))return false;
            for(int i=0;i<3;++i)
            {
                const UINT w=i==0?width:(std::max)(1u,width/2),h=i==0?height:(std::max)(1u,height/2);
                if(!textures[i]&&FAILED(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&textures[i],nullptr)))return false;
                if(!surfaces[i]&&FAILED(textures[i]->GetSurfaceLevel(0,&surfaces[i])))return false;
            }
            return true;
        };
        if(!initialize()){failed=true;Drop(target);return false;}
        state->Capture();device->GetDepthStencilSurface(&depth);
        D3DCAPS9 caps{};device->GetDeviceCaps(&caps);
        IDirect3DSurface9* extra[3]{};
        const unsigned count=(std::min)(3u,unsigned(caps.NumSimultaneousRTs>0?caps.NumSimultaneousRTs-1:0));
        for(unsigned i=0;i<count;++i){device->GetRenderTarget(i+1,&extra[i]);device->SetRenderTarget(i+1,nullptr);}
        const HRESULT copied=device->StretchRect(target,nullptr,surfaces[0],nullptr,D3DTEXF_NONE);
        bool ok=SUCCEEDED(copied);
        // Present hooks usually run after EndScene. Own a scene only when the
        // caller has not already begun one; never end the game's active scene.
        const bool ownScene=SUCCEEDED(device->BeginScene());
        device->SetDepthStencilSurface(nullptr);device->SetVertexShader(vertexShader);
        device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);
        device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        const unsigned w=(std::max)(1u,width/2),h=(std::max)(1u,height/2);
        const D3DVIEWPORT9 halfViewport{0,0,w,h,0,1};device->SetViewport(&halfViewport);device->SetPixelShader(blurShader);
        const float screen[4]={2.f/w,-2.f/h,-1.f-1.f/w,1.f+1.f/h};device->SetVertexShaderConstantF(0,screen,1);
        for(int pass=0;pass<2&&ok;++pass)
        {
            device->SetTexture(0,nullptr);
            ok=SUCCEEDED(device->SetRenderTarget(0,surfaces[pass+1]));
            const float step[4]={pass==0?style.blur/float(width):0,pass==1?style.blur/float(height):0,0,0};
            device->SetPixelShaderConstantF(0,step,1);device->SetTexture(0,textures[pass]);Quad(w,h);
        }
        device->SetTexture(0,nullptr);
        if(ownScene)device->EndScene();
        device->SetRenderTarget(0,target);device->SetDepthStencilSurface(depth);
        device->SetViewport(&viewport);
        for(unsigned i=0;i<count;++i){device->SetRenderTarget(i+1,extra[i]);Drop(extra[i]);}
        state->Apply();Drop(target);Drop(depth);
        ready=ok;return ready;
    }
    void Glass::Begin(const ImDrawList*,const ImDrawCmd* command)
    {
        auto* pass=static_cast<Pass*>(command->UserCallbackData);auto* self=pass->owner;
        if(self->device&&self->ready)
        {
            self->device->SetVertexShader(self->vertexShader);
            const float screen[4]={2.f/self->width,-2.f/self->height,-1.f-1.f/self->width,1.f+1.f/self->height};
            self->device->SetVertexShaderConstantF(0,screen,1);
            self->device->SetPixelShader(pass->text?self->textShader:self->glassShader);
            self->device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            self->device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            if(pass->text)
            {
                self->device->SetTexture(1,self->textures[2]);
                self->device->SetSamplerState(1,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);self->device->SetSamplerState(1,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
                self->device->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);self->device->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
                self->device->SetSamplerState(1,D3DSAMP_MIPFILTER,D3DTEXF_NONE);self->device->SetSamplerState(1,D3DSAMP_SRGBTEXTURE,FALSE);
            }
            self->device->SetPixelShaderConstantF(0,&pass->uniforms[0][0],5);
        }
    }
    Glass::Pass* Glass::MakePass(const GlassStyle& style,bool text,bool mesh,ImVec2 pos,ImVec2 size)
    {
        if(!ready||passCount>=passes.size())return nullptr;
        auto& pass=passes[passCount++];pass.owner=this;pass.text=text;
        const float data[5][4]={{pos.x/width,pos.y/height,size.x/width,size.y/height},
            {size.x,size.y,style.radius,(std::min)(size.x,size.y)},
            {float(style.mode),style.refraction,style.zoom,style.chroma},
            {style.frost,style.edge,style.opacity,mesh?1.f:0.f},{style.tint.x,style.tint.y,style.tint.z,style.tint.w}};
        std::memcpy(pass.uniforms,data,sizeof(data));
        if(text){pass.uniforms[0][0]=1.f/width;pass.uniforms[0][1]=1.f/height;pass.uniforms[0][2]=float(width);pass.uniforms[0][3]=float(height);}
        return &pass;
    }
    void Glass::Draw(ImDrawList* draw,ImVec2 pos,ImVec2 size,const GlassStyle& style)
    {
        std::lock_guard lock(mutex);
        auto* pass=MakePass(style,false,false,pos,size);if(!pass)return;
        draw->AddCallback(Begin,pass);
        draw->AddImage(reinterpret_cast<ImTextureID>(textures[2]),pos,{pos.x+size.x,pos.y+size.y},{0,0},{1,1});
        draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    }
    bool Glass::BeginText(ImDrawList* draw,const GlassStyle& style,bool mesh)
    {
        std::lock_guard lock(mutex);auto* pass=MakePass(style,true,mesh);if(!pass)return false;
        draw->AddCallback(Begin,pass);return true;
    }
    void Glass::EndText(ImDrawList* draw){draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);}
}
#endif
