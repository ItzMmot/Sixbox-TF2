#pragma once
#include "model_transport.hpp"
#include "model_policy.hpp"
#include "../../../../Hooks/Direct3DDevice9.h"
namespace LocalModel
{
inline ITexture* target = nullptr;
inline bool captureFrame = false;
inline double nextCapture = 0;
inline bool Enabled() { return bool(Vars::ESP::OverlayRender.Value & 4); }
inline bool Route(CBaseEntity* entity, int flags = 0)
{
    return EnemyPart<CBaseEntity>(entity,H::Entities.GetLocal(),Enabled(),flags);
}
inline void Initialize()
{
    if (target || !SDK::SupportsDX9Shaders()) return;
    target=I::MaterialSystem->CreateNamedRenderTargetTextureEx("LocalModelOutlineDepthV2",1,1,RT_SIZE_FULL_FRAME_BUFFER,
        IMAGE_FORMAT_RGB888,MATERIAL_RT_DEPTH_SEPARATE,TEXTUREFLAGS_CLAMPS|TEXTUREFLAGS_CLAMPT,CREATERENDERTARGETFLAGS_HDR);
    if (target) target->IncrementReferenceCount();
}
inline void Unload()
{
    captureFrame=false;nextCapture=0;
    if (target) { target->DecrementReferenceCount();target->DeleteIfUnreferenced();target=nullptr; }
}
inline void BeginFrame()
{
    captureFrame=false;
    if (Enabled() && !target) LocalExtension::ModelCaptureError(true);
    if (target && (target->GetActualWidth()<H::Draw.m_nScreenW || target->GetActualHeight()<H::Draw.m_nScreenH))
    { LocalExtension::ModelCaptureError(true);return; }
    if (!Enabled() || !target || !LocalExtension::ModelTransportReady() || SDK::CleanScreenshot()
        || !WndProc::hwWindow || GetForegroundWindow()!=WndProc::hwWindow || IsIconic(WndProc::hwWindow)) return;
    const double now=SDK::PlatFloatTime();
    const double period=1./std::clamp(Vars::ESP::OverlayStencilRate.Value,15,120);
    if (now+0.00001<nextCapture) return;
    nextCapture=nextCapture<now-period?now+period:nextCapture+period;
    captureFrame=true;
}
struct ColorWriteScope
{
    IMatRenderContext* context;
    explicit ColorWriteScope(IMatRenderContext* value):context(value)
    { context->OverrideColorWriteEnable(true,false);context->OverrideAlphaWriteEnable(true,false); }
    ~ColorWriteScope()
    { context->OverrideAlphaWriteEnable(false,true);context->OverrideColorWriteEnable(false,true); }
};
struct TargetScope
{
    IMatRenderContext* context;
    TargetScope(IMatRenderContext* ctx,int width,int height):context(ctx)
    {
        context->PushRenderTargetAndViewport(target);
        context->Viewport(0,0,width,height);
        context->ClearColor4ub(0,0,0,0);
        context->ClearBuffers(true,true,true);
    }
    ~TargetScope() { context->PopRenderTargetAndViewport(); }
};
inline void Publish(IMatRenderContext* context,unsigned width,unsigned height,bool hasModels)
{
    if (!width || !height || width>8192 || height>8192 || uint64_t(width)*height>33177600)
    { LocalExtension::ModelCaptureError(true);return; }
    FILETIME now{};GetSystemTimeAsFileTime(&now);
    const uint64_t timestamp=(uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime;
    auto pixels=LocalExtension::AcquireModelPixels(size_t(width)*height*3);
    // ReadPixels has no result code: clear reused storage first so a device
    // loss cannot republish the previous silhouette as a fresh image.
    std::fill(pixels.begin(),pixels.end(),0);
    if (hasModels) context->ReadPixels(0,0,width,height,pixels.data(),IMAGE_FORMAT_RGB888);
    LocalExtension::PublishModelPixels(std::move(pixels),width,height,timestamp);
    LocalExtension::ModelCaptureError(false);
}
}
