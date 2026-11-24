#include "Sandevistan.h"
#ifndef TEXTMODE
#include "../Materials/Materials.h"
#include "../../ImGui/Render.h"
#include "../../Configs/Configs.h"
#include <fstream>
#include <ImGui/imgui.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace V = Vars::Visuals::Sandevistan;
namespace
{
    thread_local bool drawing=false;
    struct DrawingScope {bool old=drawing;DrawingScope(){drawing=true;}~DrawingScope(){drawing=old;}};
    struct RenderScope
    {
        float color[3]{};
        float blend=I::RenderView->GetBlend();
        IMaterial* material=nullptr;OverrideType_t type=OVERRIDE_NORMAL;
        RenderScope(){I::RenderView->GetColorModulation(color);I::ModelRender->GetMaterialOverride(&material,&type);}
        ~RenderScope(){I::ModelRender->ForcedMaterialOverride(material,type);I::RenderView->SetColorModulation(color);I::RenderView->SetBlend(blend);}
    };
    void ApplyTint(Color_t color,float opacity)
    {
        I::RenderView->SetColorModulation(color);
        I::RenderView->SetBlend(std::clamp(color.a/255.f*opacity,0.f,1.f));
    }
    bool Allowed(){return V::Enabled.Value && (V::Triggers.Value&3) && !G::Unload;}
    Color_t Color(float rank)
    {
        return Sandevistan::Gradient(V::StartColor.Value,V::EndColor.Value,
            Sandevistan::Rank(int(rank*10000),10001,V::GradientBias.Value,V::Reverse.Value));
    }
    float TrailFade(double age,float rank)
    {return Sandevistan::Fade(age,V::Duration.Value,V::FadeIn.Value,V::FadePower.Value,rank,V::Stagger.Value);}
    void Transform(matrix3x4* dest,const matrix3x4* source,int count,const Vec3& origin,const Vec3& offset,float scale)
    {
        for(int bone=0;bone<count;++bone)for(int row=0;row<3;++row)
        {
            for(int col=0;col<3;++col)dest[bone][row][col]=source[bone][row][col]*scale;
            dest[bone][row][3]=origin[row]+(source[bone][row][3]-origin[row])*scale+offset[row];
        }
    }
}
bool CSandevistan::Rendering(){return drawing;}
void CSandevistan::ClearLocked()
{
    m_events={};m_commands={};m_count=m_nextGhost=m_nextEvent=0;m_current=0;
    m_owner.Term();m_class=m_modelIndex=0;
}
void CSandevistan::Reset(){std::lock_guard lock(m_mutex);ClearLocked();}
bool CSandevistan::ValidOwner(CTFPlayer* local)
{
    if(!local||!local->IsAlive()||local->IsAGhost()||!I::EngineClient->IsInGame()) {ClearLocked();return false;}
    const int playerClass=local->m_iClass(), modelIndex=local->m_nModelIndex();
    if(m_owner.Get()!=local||m_class!=playerClass||m_modelIndex!=modelIndex)
    {ClearLocked();m_owner=local;m_class=playerClass;m_modelIndex=modelIndex;}
    return true;
}
CSandevistan::Event* CSandevistan::FindEvent(uint64_t id)
{
    if(!id)return nullptr;
    for(auto& event:m_events)if(event.id==id)return &event;
    return nullptr;
}
void CSandevistan::MarkCommand(CTFPlayer* local,CUserCmd* command,int kind,int remaining)
{
    std::lock_guard lock(m_mutex);
    if(!Allowed()||!ValidOwner(local)){ClearLocked();return;}
    if(!command||command->command_number<1)return;
    kind&=V::Triggers.Value&3;
    if(!kind){m_current=0;return;}
    const double now=SDK::PlatFloatTime();
    const int logical=Sandevistan::InputSequence(G::OriginalCmd.command_number,command->command_number);
    auto* event=FindEvent(m_current);
    if(!event||event->Split(logical,kind,now))
    {
        if(remaining<1){m_current=0;return;}
        event=&m_events[m_nextEvent];m_nextEvent=(m_nextEvent+1)%int(m_events.size());*event={};
        event->Start(logical,kind);event->id=++m_serial;m_current=event->id;
    }
    const bool confirmed=event->confirmed;
    if(!event->Append(logical,remaining,now))return;
    if(!confirmed&&event->confirmed)++m_diagnostics.events;
    // Record every physical command. Sampling and gradient ranks are derived
    // from the poses actually captured once the burst reaches the renderer.
    m_commands[command->command_number%m_commands.size()]={command->command_number,logical,event->id,false};
    if(remaining<=0)m_current=0;
}
void CSandevistan::CapturePrediction(CTFPlayer* local,CUserCmd* command,float /*simulationTime*/)
{
    if(!command||command->command_number<1||!Allowed())return;
    Command metadata;
    {
        std::lock_guard lock(m_mutex);
        if(!ValidOwner(local))return;
        auto& stored=m_commands[command->command_number%m_commands.size()];
        if(stored.number!=command->command_number||stored.consumed||!FindEvent(stored.event))return;
        metadata=stored;
    }
    const auto* model=local->GetModel();if(!model)return;
    // Store every sampled command even if bones cannot be evaluated inside
    // prediction. Freeze the actual engine-rendered palette on the first draw.
    Ghost pose;pose.event=metadata.event;pose.command=metadata.logical;pose.model=model;
    pose.origin=local->m_vecOrigin();pose.angles=local->GetAbsAngles();
    pose.velocity=local->m_vecVelocity();pose.mins=local->m_vecMins();pose.maxs=local->m_vecMaxs();
    pose.skin=local->m_nSkin();pose.body=local->m_nBody();pose.hitbox=local->m_nHitboxSet();
    std::lock_guard lock(m_mutex);
    auto* event=FindEvent(metadata.event);
    if(!event||m_owner.Get()!=local||m_modelIndex!=local->m_nModelIndex())return;
    auto& stored=m_commands[command->command_number%m_commands.size()];
    if(stored.number!=metadata.number||stored.event!=metadata.event||stored.consumed)return;
    stored.consumed=true;++m_diagnostics.captured;
    m_ghosts[m_nextGhost]=pose;m_nextGhost=(m_nextGhost+1)%int(m_ghosts.size());
    m_count=std::min(m_count+1,int(m_ghosts.size()));
}
bool CSandevistan::Latest(double now,double& born)
{
    std::lock_guard lock(m_mutex);
    // Present may run on the queued material thread while the game replaces
    // player entities. Only read captured event data here.
    born=0;
    for(const auto& event:m_events)
        if(event.confirmed&&now>=event.born&&now-event.born<std::clamp(V::Duration.Value,.15f,3.f))born=std::max(born,event.born);
    return born>0;
}
void CSandevistan::InitializeMaterials()
{
    for(int style=0;style<3;++style)for(int depth=0;depth<2;++depth)
    {
        if(m_materials[style][depth])continue;
        const auto name=std::format("SixBoxSandevistan_{}_{}",style,depth);
        const auto text=std::format("\"UnlitGeneric\" {{ \"$basetexture\" \"vgui/white_additive\" \"$model\" \"1\" \"$translucent\" \"1\" \"$nofog\" \"1\" \"$nocull\" \"1\" \"$ignorez\" \"{}\" \"$additive\" \"{}\" \"$wireframe\" \"{}\" }}",depth,style==1?1:0,style==2?1:0);
        m_materials[style][depth]=F::Materials.create_from_vmt(name.c_str(),text);
    }
}
void CSandevistan::ShutdownMaterials()
{
    Reset();
    for(auto& variants:m_materials)for(auto*& material:variants)if(material){F::Materials.Remove(material);material=nullptr;}
}
void CSandevistan::RenderWorld(const CViewSetup* setup)
{
    if(!Allowed()||drawing||SDK::CleanScreenshot()||I::EngineVGui->IsGameUIVisible()||!F::Materials.m_bLoaded)return;
    ReportDiagnostics();
    auto* local=H::Entities.GetLocal();const double now=SDK::PlatFloatTime();int count=0;
    {
        std::lock_guard lock(m_mutex);
        if(!ValidOwner(local))return;
        std::array<int,Sandevistan::MaxGhosts> order{};int candidates=0;
        for(int i=0;i<m_count;++i)
        {
            auto& ghost=m_ghosts[i];auto* event=FindEvent(ghost.event);
            if(!event||!event->confirmed) continue;
            int index=0,total=0;
            for(int j=0;j<m_count;++j) if(m_ghosts[j].event==ghost.event)
            {++total;if(m_ghosts[j].command<ghost.command) ++index;}
            const int copies=std::min(std::clamp(V::Copies.Value,1,Sandevistan::MaxCopies),total);
            if(Sandevistan::Sample(index,total,copies)<0) continue;
            ghost.rank=Sandevistan::CapturedRank(index,total);ghost.copies=copies;
            if(TrailFade(now-event->born,ghost.rank)>.002f)order[candidates++]=i;
        }
        std::sort(order.begin(),order.begin()+candidates,[&](int a,int b)
        {return m_ghosts[a].event==m_ghosts[b].event?m_ghosts[a].command<m_ghosts[b].command:m_ghosts[a].event<m_ghosts[b].event;});
        const int budget=std::clamp(V::DrawBudget.Value,1,64);
        for(int i=std::max(0,candidates-budget);i<candidates;++i)
        {
            const auto& ghost=m_ghosts[order[i]];
            const Ghost* first=&ghost;const Ghost* last=&ghost;
            for(int j=0;j<m_count;++j)if(m_ghosts[j].event==ghost.event)
            {
                if(m_ghosts[j].command<first->command)first=&m_ghosts[j];
                if(m_ghosts[j].command>last->command)last=&m_ghosts[j];
            }
            Vec3 direction=last->origin-first->origin;direction.z=0;
            const float travel=direction.Length();
            if(travel<1.f){direction=first->velocity;direction.z=0;}
            if(direction.Length()<1.f){Math::AngleVectors(first->angles,&direction);direction.z=0;}
            const float length=direction.Length();direction=length>0?direction/length:Vec3{1,0,0};
            const float extension=Sandevistan::TrailExtension(ghost.copies,travel,ghost.maxs.x-ghost.mins.x,V::MinSpacing.Value,V::StationarySpread.Value);
            // Short ground bursts extend behind the same motion/facing axis.
            // A lateral fan turns every stationary ground event into a left-right sweep.
            m_renderOffset[count]=direction*Sandevistan::BehindOffset(ghost.rank,extension);
            m_renderGhosts[count]=ghost;m_renderBirth[count]=FindEvent(ghost.event)->born;++count;
        }
    }
    if(!count)return;
    auto* material=m_materials[std::clamp(V::Material.Value,0,2)][V::ThroughWalls.Value?1:0];
    if(!material||material->IsErrorMaterial())return;
    const auto hook=U::Hooks.m_mHooks.find("CBaseAnimating_InternalDrawModel");
    if(hook==U::Hooks.m_mHooks.end()||!hook->second->m_pOriginal)
    {std::lock_guard lock(m_mutex);++m_diagnostics.missingHook;return;}
    CViewSetup view{};if(setup)view=*setup;else if(!I::Client->GetPlayerView(view))return;
    auto* renderable=local->GetClientRenderable();if(!renderable)return;
    if(renderable->GetModelInstance()==MODEL_INSTANCE_INVALID)renderable->CreateModelInstance();
    m_renderCount=count;m_renderNow=now;m_renderCamera=view.origin;
    RenderScope restore;DrawingScope scope;
    struct PassScope {bool& active;PassScope(bool& value):active(value){active=true;}~PassScope(){active=false;}} pass(m_worldPass);
    I::ModelRender->ForcedMaterialOverride(material);
    {std::lock_guard lock(m_mutex);++m_diagnostics.templateCalls;}
    // Let the entity prepare its studio state, hardware data, LOD and lighting.
    // The draw hook consumes this seed draw and emits only historical bodies.
    hook->second->As<InternalDrawModelFn>()(local,STUDIO_RENDER|STUDIO_NOSHADOWS);
}
bool CSandevistan::RenderBody(IVModelRender* render,const DrawModelState_t& state,const ModelRenderInfo_t& info,
                             matrix3x4* bones,DrawModelExecuteFn original)
{
    if(!m_worldPass)return false;
    auto* local=H::Entities.GetLocal();
    if(!local||info.entity_index!=local->entindex()||info.pModel!=local->GetModel())return true;
    if(!bones||!state.m_pStudioHdr||!state.m_pStudioHWData||!original)return true;
    const int boneCount=state.m_pStudioHdr->numbones;
    if(boneCount<1||boneCount>MAXSTUDIOBONES)return true;
    {std::lock_guard lock(m_mutex);++m_diagnostics.templateHits;}
    auto* material=m_materials[std::clamp(V::Material.Value,0,2)][V::ThroughWalls.Value?1:0];
    I::ModelRender->ForcedMaterialOverride(material);
    matrix3x4 from{};
    if(info.pModelToWorld)memcpy(from,*info.pModelToWorld,sizeof(from));
    else {Math::AngleMatrix(info.angles,from);for(int r=0;r<3;++r)from[r][3]=info.origin[r];}
    const int renderCount=std::exchange(m_renderCount,0);
    for(int i=0;i<renderCount;++i)
    {
        auto& ghost=m_renderGhosts[i];
        if(ghost.model!=info.pModel)
        {std::lock_guard lock(m_mutex);++m_diagnostics.modelMismatch;continue;}
        if(!ghost.boneCount)
        {
            matrix3x4 historical{};Math::AngleMatrix(ghost.angles,historical);
            for(int r=0;r<3;++r)historical[r][3]=ghost.origin[r];
            Sandevistan::RebaseBones(ghost.bones.data(),bones,boneCount,from,historical);
            ghost.boneCount=boneCount;
            // Persist the frozen pose. Later frames must not animate old copies.
            std::lock_guard lock(m_mutex);
            for(int j=0;j<m_count;++j)if(m_ghosts[j].event==ghost.event&&m_ghosts[j].command==ghost.command)
            {m_ghosts[j].bones=ghost.bones;m_ghosts[j].boneCount=boneCount;break;}
        }
        const double age=m_renderNow-m_renderBirth[i];
        const float scale=std::clamp(V::Scale.Value,.75f,1.3f);
        const Vec3 offset=m_renderOffset[i]+Vec3{0,0,float(age)*V::Drift.Value};
        const Vec3 origin=ghost.origin+offset,mins=origin+ghost.mins*scale,maxs=origin+ghost.maxs*scale;
        if(Sandevistan::CameraIntersects(m_renderCamera.x,m_renderCamera.y,m_renderCamera.z,
            mins.x,mins.y,mins.z,maxs.x,maxs.y,maxs.z,std::max(4.f,V::MinCameraDistance.Value)))
        {std::lock_guard lock(m_mutex);++m_diagnostics.nearCamera;continue;}
        // Each queued draw owns a different palette and transform. Reusing one
        // scratch palette collapses deferred copies onto the last pose.
        Transform(ghost.bones.data(),ghost.bones.data(),ghost.boneCount,ghost.origin,offset,scale);
        auto& packet=m_worldDraws[i];packet.info=info;packet.state=state;
        auto& draw=packet.info;auto& setup=packet.state;auto& transform=packet.transform;
        Math::AngleMatrix(ghost.angles,transform);
        for(int r=0;r<3;++r)transform[r][3]=origin[r];
        draw.origin=origin;draw.angles=ghost.angles;draw.skin=ghost.skin;draw.body=ghost.body;draw.hitboxset=ghost.hitbox;
        draw.pModelToWorld=&transform;draw.pLightingOrigin=&draw.origin;setup.m_pModelToWorld=&transform;
        const auto color=Color(ghost.rank);
        ApplyTint(color,V::Opacity.Value*TrailFade(age,ghost.rank));
        original(render,setup,draw,ghost.bones.data());
        {std::lock_guard lock(m_mutex);++m_diagnostics.drawn;}
    }
    return true; // Never draw the live seed body in first person.
}
void CSandevistan::ReportDiagnostics()
{
    const double now=SDK::PlatFloatTime();if(now<m_nextReport)return;
    m_nextReport=now+1;
    Diagnostics stats;int poses=0;
    {
        std::lock_guard lock(m_mutex);stats=m_diagnostics;poses=m_count;
    }
    if(stats.events==m_reportedEvents&&stats.drawn==m_reportedDraws&&stats.templateCalls==m_reportedTemplates)return;
    m_reportedEvents=stats.events;m_reportedDraws=stats.drawn;m_reportedTemplates=stats.templateCalls;
    const std::filesystem::path path=std::filesystem::path(F::Configs.m_sCorePath)/"sandevistan.log";
    std::error_code error;const auto bytes=std::filesystem::file_size(path,error);
    const auto mode=!error&&bytes>1024*1024?std::ios::trunc:std::ios::app;
    std::ofstream log(path,std::ios::out|mode);
    if(!log)
    {
        SDK::Output("Sandevistan","Cannot write Core/sandevistan.log",WARNING_COLOR);
        return;
    }
    log<<std::format("time={:.3f} events={} captured={} stored={} templates={} template_hits={} drawn={} camera_culled={} model_mismatch={} missing_hook={}\n",
        now,stats.events,stats.captured,poses,stats.templateCalls,stats.templateHits,stats.drawn,stats.nearCamera,stats.modelMismatch,stats.missingHook);
}
void CSandevistan::RenderViewmodel(IVModelRender* render,const DrawModelState_t& state,const ModelRenderInfo_t& info,
                                  matrix3x4* bones,DrawModelExecuteFn original)
{
    if(!Allowed()||!V::Viewmodel.Value||drawing||!bones||!original||SDK::CleanScreenshot()||!F::Materials.m_bLoaded)return;
    const double now=SDK::PlatFloatTime();
    const Vec3 angles=I::EngineClient->GetViewAngles();
    if(m_echoFrame!=I::GlobalVars->framecount)
    {
        m_echoFrame=I::GlobalVars->framecount;m_nextViewDraw=0;m_cameraMotion.Update(now,angles.y,angles.x);
        Vec3 right,up;Math::AngleVectors(angles,nullptr,&right,&up);
        auto* local=H::Entities.GetLocal();const Vec3 velocity=local?local->m_vecVelocity():Vec3{};
        m_echoMotion={std::clamp(m_cameraMotion.x-velocity.Dot(right)*.004f,-4.f,4.f),
                      std::clamp(m_cameraMotion.y-velocity.Dot(up)*.004f,-4.f,4.f)};
    }
    double born;if(!Latest(now,born))return;
    auto* header=info.pModel?I::ModelInfoClient->GetStudiomodel(info.pModel):nullptr;
    if(!header||header->numbones<1||header->numbones>MAXSTUDIOBONES)return;
    auto* material=m_materials[std::clamp(V::Material.Value,0,2)][0];if(!material||material->IsErrorMaterial())return;
    RenderScope restore;DrawingScope scope;I::ModelRender->ForcedMaterialOverride(material);
    Vec3 right,up;Math::AngleVectors(I::EngineClient->GetViewAngles(),nullptr,&right,&up);
    const int copies=std::clamp(V::ViewCopies.Value,1,8);
    for(int i=0;i<copies;++i)
    {
        const float rank=copies>1?float(i)/(copies-1):0.f;
        const Vec3 offset=(right*m_echoMotion.x+up*m_echoMotion.y)*(float(copies-i)*std::clamp(V::ViewSpread.Value,0.f,4.f));
        if(m_nextViewDraw>=m_viewDraws.size())break;
        auto& packet=m_viewDraws[m_nextViewDraw++];packet.info=info;packet.state=state;
        Transform(packet.bones.data(),bones,header->numbones,info.origin,offset,1.f);
        auto& draw=packet.info;auto& setup=packet.state;auto& transform=packet.transform;
        if(info.pModelToWorld)memcpy(transform,*info.pModelToWorld,sizeof(transform));else Math::AngleMatrix(info.angles,transform);
        for(int row=0;row<3;++row)transform[row][3]=info.origin[row]+offset[row];
        draw.origin+=offset;draw.pModelToWorld=&transform;setup.m_pModelToWorld=&transform;
        const auto color=Color(rank);
        ApplyTint(color,V::ViewOpacity.Value*TrailFade(now-born,rank));
        original(render,setup,draw,packet.bones.data());
    }
}
void CSandevistan::DrawHUD()
{
    if(!Allowed()||!V::SpeedLines.Value||SDK::CleanScreenshot()||I::EngineVGui->IsGameUIVisible())return;
    double born;const double now=SDK::PlatFloatTime();if(!Latest(now,born))return;
    const float amount=TrailFade(now-born,1.f)*std::clamp(V::ScreenStrength.Value,0.f,1.f);
    if(amount<.003f)return;
    auto* draw=ImGui::GetBackgroundDrawList();const auto size=ImGui::GetIO().DisplaySize;const ImVec2 center(size.x*.5f,size.y*.5f);
    const float progress=std::clamp(float((now-born)/std::max(V::Duration.Value,.15f)),0.f,1.f);
    const int lines=std::clamp(V::LineCount.Value,4,40);
    for(int i=0;i<lines;++i)
    {
        const float rank=float(i)/std::max(1,lines-1),angle=float(i)/lines*6.2831853f+.13f;
        const float x=std::cos(angle)*size.x*.65f,y=std::sin(angle)*size.y*.65f;
        const float start=.78f+progress*.16f;
        auto color=Color(rank);color.a=byte(color.a*amount);
        draw->AddLine({center.x+x*start,center.y+y*start},{center.x+x,center.y+y},IM_COL32(color.r,color.g,color.b,color.a),1.5f);
    }
}
#endif
