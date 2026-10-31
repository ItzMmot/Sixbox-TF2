#include "Aura.h"
#ifndef TEXTMODE
#include "AuraBytecode.h"
#include "AuraTexture.h"
#include "../Groups/Groups.h"
#include "../Materials/Materials.h"
#include <cstdio>

namespace
{
    thread_local int viewmodelKind=0;
    thread_local bool capturing=false;
    std::atomic_bool separateDepth=true;
    int depthFrame=-1;
    uint64_t depthEpoch=0;
    template<class T> void Drop(T*& value) {if(value) {value->Release();value=nullptr;}}
    struct ContextGuard {IMatRenderContext* value; ~ContextGuard() {if(value)value->Release();}};
    constexpr DWORD VertexFormat=D3DFVF_XYZRHW|D3DFVF_TEX1;
}
int CAura::SetViewmodelKind(int kind) {int old=viewmodelKind;viewmodelKind=kind;return old;}
bool CAura::Capturing() {return capturing;}

int CAura::QueuedCall::Release()
{
    const int left=--refs;
    if(!left) owner->m_commandSlots.Release(index);
    return left;
}
void CAura::QueuedCall::operator()()
{
    if(kind==2 || (!G::Unload && snapshot.epoch==owner->m_epoch.load(std::memory_order_acquire))) owner->Execute(*this);
}
bool CAura::Queue(IMatRenderContext* context,int kind,int layer,const Frame& frame)
{
    const int slot=m_commandSlots.Acquire();
    if(slot<0) return false;
    auto& call=m_commands[slot];
    call.owner=this;call.index=slot;call.kind=kind;call.layer=layer;call.snapshot=frame;
    call.AddRef();
    // The engine consumes one owned reference, as in CRenderSync::Queue.
    void* queue=U::Memory.CallVirtual<147,void*>(context);
    if(queue) U::Memory.CallVirtual<0,void>(queue,static_cast<void*>(&call));
    else {call();call.Release();}
    return true;
}
void CAura::Execute(const QueuedCall& call)
{
    if(call.kind==2) {Release();return;}
    if(call.kind==1) {m_gpu=call.snapshot;return;}
    auto* device=I::DirectXDevice;
    if(!device) return;
    if(m_device!=device) {Release();m_device=device;}
    if(call.kind==3)
    {
        IDirect3DSurface9 *color=nullptr,*depth=nullptr;
        D3DSURFACE_DESC c{},d{};
        const bool shared=SUCCEEDED(device->GetRenderTarget(0,&color)) && color
            && SUCCEEDED(device->GetDepthStencilSurface(&depth)) && depth
            && SUCCEEDED(color->GetDesc(&c)) && SUCCEEDED(depth->GetDesc(&d))
            && c.MultiSampleType==D3DMULTISAMPLE_NONE && d.MultiSampleType==D3DMULTISAMPLE_NONE;
        separateDepth.store(!shared,std::memory_order_release);
        Drop(color);Drop(depth);return;
    }
    if(!Aura::D3D::CaptureTarget(device,m_masks[call.layer])) return;
    m_maskFrames[call.layer]=call.snapshot.frame;
}
void CAura::WaitIdle(int milliseconds)
{
    for(int elapsed=0;m_commandSlots.Busy() && elapsed<milliseconds;elapsed+=5) Sleep(5);
}
void CAura::ClearScene()
{
    m_epoch.fetch_add(1,std::memory_order_acq_rel);
    separateDepth.store(true,std::memory_order_release);depthFrame=-1;
    m_source={};m_seenCount=0;m_hasAngles=false;m_sourceTime=0;m_filterFrame=-1;
    m_filterFrames.fill(-1);m_filterGroups.fill(-1);
    for(auto& filter:m_filters) filter.Reset();
}
void CAura::BeginFrame()
{
    const int frame=I::GlobalVars->framecount;
    if(m_source.frame==frame && m_source.epoch==m_epoch.load(std::memory_order_acquire)) return;
    m_source={};m_source.frame=frame;m_source.epoch=m_epoch.load(std::memory_order_acquire);
    m_groups.fill(-1);m_seenCount=0;
    if(!F::Groups.GroupsActive()) return;
    const int count=int((std::min)(F::Groups.m_vGroups.size(),size_t(MAX_GROUPS)));
    int used=0;
    for(int pass=0;pass<2;++pass)
        for(int group=count-1;group>=0 && used<Aura::Layers;--group)
        {
            const auto& data=F::Groups.m_vGroups[group];
            if(!(static_cast<uint32_t>(Vars::ESP::ActiveGroups.Value)&(1u<<group)) || !data.m_tAura.Enabled) continue;
            const bool firstperson=data.m_iTargets&(TargetsEnum::ViewmodelHands|TargetsEnum::ViewmodelWeapon);
            if(firstperson!=(pass==0)) continue;
            m_groups[used++]=group;
        }
}
void CAura::InitializeMaterials()
{
    ClearScene();
    if(!SDK::SupportsDX9Shaders()) return;
    I::MatSystemSurface->GetScreenSize(m_width,m_height);
    if(m_width<=0 || m_height<=0) return;
    if(!m_visibleMaterial)
        m_visibleMaterial=F::Materials.create_from_vmt("SixboxAuraVisibleV2",
            "\"UnlitGeneric\" { \"$basetexture\" \"vgui/white_additive\" \"$model\" \"1\" \"$translucent\" \"1\" \"$nofog\" \"1\" \"$nocull\" \"1\" \"$ignorez\" \"0\" }");
    if(!m_throughMaterial)
        m_throughMaterial=F::Materials.create_from_vmt("SixboxAuraThroughV2",
            "\"UnlitGeneric\" { \"$basetexture\" \"vgui/white_additive\" \"$model\" \"1\" \"$translucent\" \"1\" \"$nofog\" \"1\" \"$nocull\" \"1\" \"$ignorez\" \"1\" }");
    for(int layer=0;layer<Aura::Layers;++layer)
    {
        char name[48];std::snprintf(name,sizeof(name),"SixboxAuraMaskV2_%d",layer);
        if(!m_targets[layer])
        {
            m_targets[layer]=I::MaterialSystem->CreateNamedRenderTargetTextureEx(name,m_width,m_height,
                RT_SIZE_LITERAL,IMAGE_FORMAT_RGBA8888,MATERIAL_RT_DEPTH_SHARED,
                TEXTUREFLAGS_CLAMPS|TEXTUREFLAGS_CLAMPT,0);
            if(m_targets[layer]) m_targets[layer]->IncrementReferenceCount();
        }
    }
}
void CAura::ShutdownMaterials()
{
    ClearScene();
    ContextGuard context{I::MaterialSystem->GetRenderContext()};
    if(context.value) Queue(context.value,2,0,m_source);
    for(auto& target:m_targets)
        if(target) {target->DecrementReferenceCount();target->DeleteIfUnreferenced();target=nullptr;}
    for(auto** material:{&m_visibleMaterial,&m_throughMaterial})
        if(*material) {F::Materials.Remove(*material);*material=nullptr;}
    m_width=m_height=0;
}
void CAura::Capture(IVModelRender* render,const DrawModelState_t& state,const ModelRenderInfo_t& info,
    matrix3x4* bones,DrawModelExecuteFn original)
{
    if(capturing || G::Unload || SDK::CleanScreenshot() || !F::Groups.GroupsActive()) return;
    auto* base=info.entity_index>=0 ? I::ClientEntityList->GetClientEntity(info.entity_index) : nullptr;
    auto* entity=base ? base->As<CBaseEntity>() : nullptr;
    const int kind=viewmodelKind ? viewmodelKind : entity && entity->IsWearableVM() ? TargetsEnum::ViewmodelWeapon : 0;
    Group_t* group=nullptr;
    if(kind ? !F::Groups.GetGroup(kind,group) : !entity || !F::Groups.GetGroup(entity,group)) return;
    if(!group || !group->m_tAura.Enabled) return;
    auto* local=H::Entities.GetLocal();
    if(!local) return;
    Aura_t style=group->m_tAura;style.Sanitize();
    if(!kind && (local->m_vecOrigin()-info.origin).LengthSqr()>style.Distance*style.Distance) return;
    BeginFrame();
    const int groupIndex=int(group-F::Groups.m_vGroups.data());
    int slot=-1;
    for(int i=0;i<Aura::Layers;++i) if(m_groups[i]==groupIndex) {slot=i;break;}
    if(slot<0 || !m_targets[slot] || m_targets[slot]->IsError()) return;
    const bool fallbackDepth=separateDepth.load(std::memory_order_acquire);
    if(style.VisibleOnly && !kind && fallbackDepth
        && !SDK::VisPosWorld(local,entity,local->GetEyePosition(),entity->GetCenter(),MASK_SHOT)) return;
    auto* material=style.VisibleOnly && !kind && !fallbackDepth ? m_visibleMaterial : m_throughMaterial;
    if(!material || material->IsErrorMaterial()) return;
    uint64_t key=uint64_t(reinterpret_cast<uintptr_t>(info.pRenderable))
        ^ (uint64_t(reinterpret_cast<uintptr_t>(info.pModel))*0x9e3779b97f4a7c15ull) ^ (uint64_t(kind)<<40)
        ^ (uint64_t(uint32_t(info.entity_index+1))*0xbf58476d1ce4e5b9ull);
    if(!key) key=1;
    for(int i=0;i<m_seenCount;++i) if(m_seen[i]==key) return;
    if(m_seenCount<int(m_seen.size())) m_seen[m_seenCount++]=key;
    auto& data=m_source.layers[slot];
    const bool first=!data.used;
    if(first)
    {
        if(style.GroupColor) style.Core=style.Edge=group->m_tColor;
        else if(style.PresetColors)
        {
        static const Color_t palette[Aura::Modes][2]={
            {{255,178,79,255},{191,65,29,255}},{{126,226,239,255},{37,94,145,255}},
            {{255,205,112,255},{218,102,45,255}},{{163,125,246,255},{64,159,210,255}},
            {{192,211,246,255},{111,127,223,255}},{{114,217,184,255},{126,111,220,255}},
            {{173,219,239,255},{85,145,190,255}},{{239,203,114,255},{65,104,114,255}},
            {{130,179,244,255},{66,82,171,255}},{{184,153,235,255},{73,108,169,255}},
            {{156,227,147,255},{43,105,83,255}},{{194,238,177,255},{44,139,100,255}},
            {{220,231,239,255},{34,45,61,255}},{{232,190,222,255},{122,176,216,255}}
        };
            style.Core=palette[style.Mode][0];style.Edge=palette[style.Mode][1];
        }
        data.style=style;data.viewmodel=kind!=0;
    }
    CBaseEntity* motion=kind ? local : entity;
    if(!kind && entity && (entity->IsBaseCombatWeapon()||entity->IsWearable()))
        if(auto* owner=entity->m_hOwnerEntity().Get()) motion=owner;
    const auto velocity=motion->GetAbsVelocity();
    const Aura::Vector sample{velocity.x,velocity.y,velocity.z};
    if(sample.Finite()) {data.velocity=data.velocity+sample;++data.samples;}
    ContextGuard context{I::MaterialSystem->GetRenderContext()};
    if(!context.value) return;
    if(depthFrame!=m_source.frame || depthEpoch!=m_source.epoch)
    {
        Queue(context.value,3,0,m_source);depthFrame=m_source.frame;depthEpoch=m_source.epoch;
    }
    IMaterial* previous=nullptr;OverrideType_t overrideType=OVERRIDE_NORMAL;
    I::ModelRender->GetMaterialOverride(&previous,&overrideType);
    const auto color=I::RenderView->GetColorModulation();const float blend=I::RenderView->GetBlend();
    context.value->PushRenderTargetAndViewport();
    context.value->SetRenderTarget(m_targets[slot]);context.value->Viewport(0,0,m_width,m_height);
    if(first) {context.value->ClearColor4ub(0,0,0,0);context.value->ClearBuffers(true,false,false);}
    I::RenderView->SetBlend(1.f);I::RenderView->SetColorModulation(1.f,1.f,1.f);
    I::ModelRender->ForcedMaterialOverride(material);
    // The override is translucent to leave scene depth untouched. Remove the
    // original opaque/translucent pass filter so it cannot skip our material.
    DrawModelState_t maskState=state;
    maskState.m_drawFlags=(maskState.m_drawFlags&~STUDIORENDER_DRAW_GROUP_MASK)|STUDIORENDER_DRAW_NO_SHADOWS;
    capturing=true;
    original(render,maskState,info,bones);
    capturing=false;
    data.used=true;
    I::ModelRender->ForcedMaterialOverride(previous,overrideType);
    I::RenderView->SetColorModulation(color);I::RenderView->SetBlend(blend);
    context.value->PopRenderTargetAndViewport();
}
void CAura::SetWorldView(const CViewSetup& view)
{
    VMatrix a{},b{},d{};
    I::RenderView->GetMatricesForView(view,&a,&b,&m_world,&d);
    m_angles=view.angles;
}
void CAura::BeginViewmodels(const CViewSetup& view)
{
    SetWorldView(view);
    CViewSetup vm=view;vm.fov=view.fovViewmodel;vm.zNear=view.zNearViewmodel;vm.zFar=view.zFarViewmodel;
    vm.m_bViewToProjectionOverride=false;
    VMatrix a{},b{},d{};
    I::RenderView->GetMatricesForView(vm,&a,&b,&m_viewmodel,&d);
}
void CAura::Publish()
{
    if(G::Unload) return;
    BeginFrame();
    const uint64_t now=GetTickCount64();
    if(m_filterFrame!=m_source.frame)
    {
        m_sourceDelta=m_sourceTime && now>m_sourceTime ? float(now-m_sourceTime)*.001f : .016f;
        m_turnX=m_hasAngles ? Aura::Wrap(m_angles.y-m_previousAngles.y)/((std::max)(m_sourceDelta,.001f)*180.f) : 0.f;
        m_turnY=m_hasAngles ? Aura::Wrap(m_angles.x-m_previousAngles.x)/((std::max)(m_sourceDelta,.001f)*180.f) : 0.f;
        if(m_sourceDelta>.25f) m_turnX=m_turnY=0.f;
        m_previousAngles=m_angles;m_hasAngles=true;m_sourceTime=now;m_filterFrame=m_source.frame;
    }
    for(int i=0;i<Aura::Layers;++i)
    {
        auto& layer=m_source.layers[i];
        if(!layer.used) continue;
        if(m_filterFrames[i]!=m_source.frame)
        {
            if(m_filterGroups[i]!=m_groups[i]) {m_filters[i].Reset();m_filterGroups[i]=m_groups[i];}
            const auto& matrix=layer.viewmodel ? m_viewmodel : m_world;
            const auto velocity=layer.samples ? layer.velocity*(1.f/layer.samples) : Aura::Vector{};
            float screen[2]{};
            for(int row=0;row<2;++row)
            {
                const float x=matrix.m[row][0],y=matrix.m[row][1],z=matrix.m[row][2];
                const float norm=std::sqrt(x*x+y*y+z*z);
                if(norm>.001f) screen[row]=(velocity.x*x+velocity.y*y+velocity.z*z)/norm/400.f;
            }
            const auto& s=layer.style;
            m_filters[i].Update(-screen[0]*s.Motion+m_turnX*s.CameraSway,
                screen[1]*s.Motion+m_turnY*s.CameraSway,m_sourceDelta,s.Inertia);
            m_filterFrames[i]=m_source.frame;
        }
        layer.bendX=m_filters[i].x;layer.bendY=m_filters[i].y;
    }
    m_source.stamp=now;
    ContextGuard context{I::MaterialSystem->GetRenderContext()};
    if(context.value)
    {
        // Acquire/resolve after every model in the layer has been drawn. This
        // also handles non-texture MSAA RTs without copying only the first model.
        for(int i=0;i<Aura::Layers;++i)
            if(m_source.layers[i].used)
            {
                context.value->PushRenderTargetAndViewport();
                context.value->SetRenderTarget(m_targets[i]);
                Queue(context.value,0,i,m_source);
                context.value->PopRenderTargetAndViewport();
            }
        Queue(context.value,1,0,m_source);
    }
}
void CAura::Hide()
{
    m_gpu={};m_maskFrames.fill(-1);
    for(auto& mask:m_masks) Drop(mask);
    Drop(m_state);
}
void CAura::Release()
{
    Hide();
    for(auto& shader:m_shaders) Drop(shader);
    for(auto& surface:m_blurSurfaces) Drop(surface);
    for(auto& texture:m_blur) Drop(texture);
    m_device=nullptr;m_failed.fill(false);m_blurWidth=m_blurHeight=0;
}
bool CAura::Initialize(IDirect3DDevice9* device,int mode,int width,int height)
{
    if(m_device!=device) {Release();m_device=device;}
    D3DCAPS9 caps{};
    if(FAILED(device->GetDeviceCaps(&caps)) || caps.PixelShaderVersion<D3DPS_VERSION(3,0)) return false;
    for(int program:{mode,Aura::Modes,Aura::Modes+1})
    {
        if(m_failed[program]) return false;
        if(!m_shaders[program])
        {
            const auto& code=Aura::Shader::Programs[program];
            if(FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data),&m_shaders[program])))
            {m_failed[program]=true;return false;}
        }
    }
    if(!m_state && FAILED(device->CreateStateBlock(D3DSBT_ALL,&m_state))) return false;
    width=(std::max)(1,width/2);height=(std::max)(1,height/2);
    if(width!=m_blurWidth || height!=m_blurHeight)
    {
        for(auto& surface:m_blurSurfaces) Drop(surface);
        for(auto& texture:m_blur) Drop(texture);
        m_blurWidth=width;m_blurHeight=height;
    }
    for(int i=0;i<2;++i)
        if(!m_blur[i])
        {
            if(FAILED(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&m_blur[i],nullptr))) return false;
            if(FAILED(m_blur[i]->GetSurfaceLevel(0,&m_blurSurfaces[i]))) {Drop(m_blur[i]);return false;}
        }
    return true;
}
void CAura::DrawCanvas(IDirect3DDevice9* device,int width,int height,int x,int y)
{
    struct Vertex {float x,y,z,rhw,u,v;};
    const float left=x-.5f,top=y-.5f,right=x+width-.5f,bottom=y+height-.5f;
    const Vertex quad[]={{left,top,0,1,0,0},{right,top,0,1,1,0},{right,bottom,0,1,1,1},
        {left,top,0,1,0,0},{right,bottom,0,1,1,1},{left,bottom,0,1,0,1}};
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,quad,sizeof(Vertex));
}
void CAura::Render(IDirect3DDevice9* device)
{
    const uint64_t now=GetTickCount64();
    if(m_gpu.epoch!=m_epoch.load(std::memory_order_acquire) || now<m_gpu.stamp || now-m_gpu.stamp>200) {Hide();return;}
    D3DVIEWPORT9 viewport{};
    if(FAILED(device->GetViewport(&viewport)) || !viewport.Width || !viewport.Height) return;
    bool ready=false;
    for(int i=0;i<Aura::Layers;++i)
        if(m_gpu.layers[i].used && m_masks[i] && m_maskFrames[i]==m_gpu.frame)
            ready=Initialize(device,m_gpu.layers[i].style.Mode,viewport.Width,viewport.Height)||ready;
    if(!ready) {Hide();return;}
    if(FAILED(m_state->Capture())) return;
    std::array<IDirect3DSurface9*,4> targets{};
    IDirect3DSurface9* depth=nullptr;
    D3DCAPS9 caps{};device->GetDeviceCaps(&caps);
    const int targetCount=std::clamp(int(caps.NumSimultaneousRTs),1,4);
    for(int i=0;i<targetCount;++i) device->GetRenderTarget(i,&targets[i]);
    device->GetDepthStencilSurface(&depth);
    if(!targets[0]) {for(auto& target:targets)Drop(target);Drop(depth);return;}
    for(int i=1;i<targetCount;++i) device->SetRenderTarget(i,nullptr);
    device->SetDepthStencilSurface(nullptr);
    device->SetVertexShader(nullptr);device->SetFVF(VertexFormat);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    device->SetRenderState(D3DRS_WRAP0,0);device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
    device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    for(int sampler=0;sampler<2;++sampler)
    {
        device->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        device->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        device->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        device->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    const float time=float(now%1000000)*.001f;
    for(int i=0;i<Aura::Layers;++i)
    {
        const auto& layer=m_gpu.layers[i];const auto& s=layer.style;
        if(!layer.used || !m_masks[i] || m_maskFrames[i]!=m_gpu.frame || !m_shaders[s.Mode] || !m_blurSurfaces[0] || !m_blurSurfaces[1]) continue;
        const float radius=std::clamp(s.Thickness*s.Scale,1.f,160.f);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        D3DVIEWPORT9 blurViewport{0,0,DWORD(m_blurWidth),DWORD(m_blurHeight),0,1};
        for(int pass=0;pass<2;++pass)
        {
            device->SetTexture(0,nullptr);device->SetTexture(1,nullptr);
            device->SetRenderTarget(0,m_blurSurfaces[pass]);device->SetViewport(&blurViewport);
            device->SetPixelShader(m_shaders[Aura::Modes+pass]);
            const float blur[4]={pass==0 ? radius*s.Width/viewport.Width : 0.f,
                pass==1 ? radius*s.Height/viewport.Height : 0.f,0,0};
            device->SetPixelShaderConstantF(0,blur,1);
            device->SetTexture(0,pass==0 ? m_masks[i] : m_blur[0]);
            DrawCanvas(device,m_blurWidth,m_blurHeight);
        }
        device->SetTexture(0,nullptr);device->SetTexture(1,nullptr);
        device->SetRenderTarget(0,targets[0]);device->SetViewport(&viewport);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,0x7);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND,s.Additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
        const Color_t core=s.Core,edge=s.Edge;
        const float uniforms[][4]={
            {time,s.Animation,s.NoiseScale,float(s.Quality)},
            {core.r/255.f,core.g/255.f,core.b/255.f,core.a/255.f},
            {edge.r/255.f,edge.g/255.f,edge.b/255.f,edge.a/255.f},
            {s.Opacity,s.Intensity,s.Fill,s.Outline},
            {s.Wind,s.WindSpeed,s.WindDirection*.01745329252f,s.Velocity},
            {s.Turbulence,s.Softness,s.Flicker,s.Glow},
            {layer.bendX,layer.bendY,s.Pulse,s.Spin},
            {radius/viewport.Width,radius/viewport.Height,viewport.Width/float(viewport.Height),float(s.Coverage)}
        };
        device->SetPixelShader(m_shaders[s.Mode]);device->SetPixelShaderConstantF(0,&uniforms[0][0],8);
        device->SetTexture(0,m_masks[i]);device->SetTexture(1,m_blur[1]);
        DrawCanvas(device,viewport.Width,viewport.Height,viewport.X,viewport.Y);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
    }
    device->SetTexture(0,nullptr);device->SetTexture(1,nullptr);
    device->SetRenderTarget(0,targets[0]);
    for(int i=1;i<targetCount;++i) device->SetRenderTarget(i,targets[i]);
    device->SetDepthStencilSurface(depth);device->SetViewport(&viewport);
    m_state->Apply();
    for(auto& target:targets) Drop(target);
    Drop(depth);
}
#endif
