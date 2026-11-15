#pragma once
#ifndef TEXTMODE
#include <d3d9.h>
#include <ImGui/imgui.h>
#include <mutex>
#include <array>

namespace Karaoke
{
    struct GlassStyle {int mode=0;float blur=12,refraction=.12f,zoom=1.02f,chroma=1.3f,frost=.035f,edge=.16f,radius=14,opacity=.92f;ImVec4 tint{.08f,.09f,.12f,.43f};};
    class Glass
    {
        std::recursive_mutex mutex;
        IDirect3DDevice9* device=nullptr;
        IDirect3DTexture9* textures[3]{};
        IDirect3DSurface9* surfaces[3]{};
        IDirect3DVertexShader9* vertexShader=nullptr;
        IDirect3DPixelShader9* blurShader=nullptr,*glassShader=nullptr,*textShader=nullptr;
        IDirect3DStateBlock9* state=nullptr;
        unsigned width=0,height=0;
        bool ready=false,failed=false;
        struct Pass {Glass* owner=nullptr;bool text=false;float uniforms[5][4]{};};
        std::array<Pass,256> passes{};
        unsigned passCount=0;
        Pass* MakePass(const GlassStyle& style,bool text,bool mesh,ImVec2 pos={},ImVec2 size={});
        static void Begin(const ImDrawList*,const ImDrawCmd* command);
        void Quad(unsigned w,unsigned h);
    public:
        ~Glass(){Release();}
        auto LockFrame(){return std::unique_lock<std::recursive_mutex>(mutex); }
        bool Capture(IDirect3DDevice9*,const GlassStyle&);
        void Draw(ImDrawList*,ImVec2 position,ImVec2 size,const GlassStyle&);
        bool BeginText(ImDrawList* draw,const GlassStyle& style,bool mesh);
        void EndText(ImDrawList* draw);
        void Release();
        bool Ready() const {return ready;}
    };
}
#endif
