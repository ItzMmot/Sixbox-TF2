#pragma once
#include "../../../SDK/SDK.h"
#ifndef TEXTMODE
#include "AuraPolicy.h"
#include <d3d9.h>

class CAura
{
    struct LayerData
    {
        Aura_t style;
        Aura::Vector velocity;
        float bendX=0.f,bendY=0.f;
        int samples=0;
        bool viewmodel=false, used=false;
    };
    struct Frame
    {
        std::array<LayerData,Aura::Layers> layers{};
        uint64_t epoch=0,stamp=0;
        int frame=-1;
    };
    struct QueuedCall
    {
        virtual int AddRef() { return ++refs; }
        virtual int Release();
        virtual ~QueuedCall() = default;
        virtual void operator()();
        std::atomic<int> refs=0;
        CAura* owner=nullptr;
        int index=0,kind=0,layer=0;
        Frame snapshot;
    };
    static constexpr int CommandCount=128;
    Aura::CommandSlots<CommandCount> m_commandSlots;
    std::array<QueuedCall,CommandCount> m_commands{};
    std::atomic<uint64_t> m_epoch=1;
    std::array<ITexture*,Aura::Layers> m_targets{};
    IMaterial* m_visibleMaterial=nullptr;
    IMaterial* m_throughMaterial=nullptr;
    int m_width=0,m_height=0;
    Frame m_source;
    std::array<int,Aura::Layers> m_groups{};
    std::array<uint64_t,256> m_seen{};
    int m_seenCount=0;
    VMatrix m_world{},m_viewmodel{};
    Vec3 m_angles{},m_previousAngles{};
    bool m_hasAngles=false;
    uint64_t m_sourceTime=0;
    int m_filterFrame=-1;
    std::array<int,Aura::Layers> m_filterFrames{};
    float m_sourceDelta=.016f,m_turnX=0.f,m_turnY=0.f;
    std::array<Aura::MotionFilter,Aura::Layers> m_filters{};
    std::array<int,Aura::Layers> m_filterGroups{};
    Frame m_gpu;
    std::array<IDirect3DTexture9*,Aura::Layers> m_masks{};
    std::array<int,Aura::Layers> m_maskFrames{};
    IDirect3DDevice9* m_device=nullptr;
    std::array<IDirect3DPixelShader9*,Aura::Programs> m_shaders{};
    std::array<bool,Aura::Programs> m_failed{};
    IDirect3DStateBlock9* m_state=nullptr;
    std::array<IDirect3DTexture9*,2> m_blur{};
    std::array<IDirect3DSurface9*,2> m_blurSurfaces{};
    int m_blurWidth=0,m_blurHeight=0;
    void BeginFrame();
    bool Queue(IMatRenderContext* context,int kind,int layer,const Frame& frame);
    void Execute(const QueuedCall& call);
    bool Initialize(IDirect3DDevice9* device,int mode,int width,int height);
    void DrawCanvas(IDirect3DDevice9* device,int width,int height,int x=0,int y=0);
public:
    void Capture(IVModelRender* render,const DrawModelState_t& state,const ModelRenderInfo_t& info,matrix3x4* bones,DrawModelExecuteFn original);
    void InitializeMaterials();
    void ShutdownMaterials();
    void BeginViewmodels(const CViewSetup& view);
    void SetWorldView(const CViewSetup& view);
    void Publish();
    void ClearScene();
    void WaitIdle(int milliseconds);
    static int SetViewmodelKind(int kind);
    static bool Capturing();
    void Render(IDirect3DDevice9* device);
    void Hide();
    void Release();
};
ADD_FEATURE(CAura, Aura);
#endif
