#include "SpyKnife.h"
#include "../../Ticks/Ticks.h"
#ifndef TEXTMODE
#include <ImGui/imgui.h>
#include <cstdio>
#endif

void CSpyKnife::CacheVisual(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command)
{
#ifndef TEXTMODE
    Display d;
    if(!Vars::Aimbot::Melee::BackstabVisuals.Value||!local||!weapon||!local->IsAlive()||local->m_iClass()!=TF_CLASS_SPY
        ||weapon->GetWeaponID()!=TF_WEAPON_KNIFE||G::Unload) {m_display.Set(d);return;}
    auto* target=Active()?m_target.Get():m_visualTarget.Get();
    if(!target||!target->IsAlive()||target->IsDormant()||target->m_iTeamNum()==local->m_iTeamNum()
        ||target->m_vecOrigin().DistToSqr(local->m_vecOrigin())>300.f*300.f)
    {
        target=nullptr;float distance=240.f*240.f;
        for(auto* entity:H::Entities.GetGroup(EntityEnum::PlayerEnemy))
        {
            auto* candidate=entity->As<CTFPlayer>();
            if(!candidate->IsAlive()||candidate->IsDormant()||candidate->IsAGhost()) continue;
            const float score=candidate->m_vecOrigin().DistToSqr(local->m_vecOrigin());
            if(score<distance) {distance=score;target=candidate;}
        }
    }
    if(!target) {m_display.Set(d);return;}
    d.active=true;d.expires=GetTickCount64()+250;d.origin=target->m_vecOrigin();d.owner=local->m_vecOrigin();
    Math::AngleVectors(target->GetEyeAngles(),&d.forward);d.forward.Normalize2D();
    d.color=Vars::Menu::Theme::Accent.Value;d.route=Vars::Aimbot::Melee::BackstabPath.Value;
    d.safety=SpyKnife::Safety(Vars::Aimbot::Melee::BackstabSafety.Value);
    if(const auto* observation=Observe(target)) d.yawError=std::abs(observation->rate)*.03f;
    d.radius=std::clamp(weapon->GetSwingRange()+target->m_vecMaxs().To2D().Length()+14.f,32.f,150.f);
    CTraceFilterWorldAndPropsOnly filter(local);CGameTrace ground={};
    SDK::Trace(d.origin+Vec3(0,0,8),d.origin-Vec3(0,0,24),MASK_SOLID,&filter,&ground);
    if(ground.DidHit()&&!ground.startsolid&&ground.plane.normal.z>.7f) {d.origin.z=ground.endpos.z;d.normal=ground.plane.normal;}
    const auto direction=SpyKnife::Unit({d.origin.x-d.owner.x,d.origin.y-d.owner.y,0});
    d.margin=SpyKnife::Dot(direction,{d.forward.x,d.forward.y,0});
    if(m_rejectedCommand==command->command_number) d.status=Status::Uncertain;
    else if(m_active) d.status=Status::Warp;
    else if(m_settling||(Eligible(local,weapon)&&m_status==Status::Settling)) d.status=Status::Settling;
    else if(m_trick) d.status=m_status;
    else if(!G::CanPrimaryAttack) d.status=Status::Cooldown;
    else if(!ValidTarget(local,weapon,target)) d.status=Status::Shield;
    else
    {
        Vec3 aim,point;
        const auto old=m_status;
        Contact(local,weapon,target,target->m_vecOrigin(),command->viewangles,aim,point,false,nullptr,true);
        d.status=m_status;m_status=old;
    }
    if(m_active||m_settling)
    {
        d.count=(std::min)(m_count+1,int(d.path.size()));
        for(int i=0;i<d.count;++i) d.path[i]=m_positions[i];
    }
    else if(m_trick)
    {
        d.count=std::clamp(m_trickCount,0,int(d.path.size()));
        for(int i=0;i<d.count;++i) d.path[i]=m_trickPath[i];
    }
    m_display.Set(d);
#else
    (void)local;(void)weapon;(void)command;
#endif
}
void CSpyKnife::RenderWorld()
{
#ifndef TEXTMODE
    const auto d=m_display.Get();
    if(!d.active||GetTickCount64()>d.expires||SDK::CleanScreenshot()||I::EngineVGui->IsGameUIVisible()) return;
    Color_t muted{139,146,163,100},accent=d.color;accent.a=220;
    const float heading=std::atan2(d.forward.y,d.forward.x);
    constexpr float pi=3.14159265358979323846f;
    auto at=[&](float angle,float radius) {
        const float x=std::cos(angle)*radius,y=std::sin(angle)*radius;
        return d.origin+Vec3(x,y,2.f-(d.normal.x*x+d.normal.y*y)/(std::max)(d.normal.z,.7f));
    };
    // Outer sector is native rear geometry; highlighted samples retain the
    // same uncertainty margins as the automation, with finite reach shown.
    for(int i=0;i<48;++i)
    {
        const float a=heading+pi*.5f+pi*float(i)/48,b=heading+pi*.5f+pi*float(i+1)/48;
        const Vec3 owner=at((a+b)*.5f,d.radius);
        const SpyKnife::V separation{d.origin.x-owner.x,d.origin.y-owner.y,0};
        const bool safe=SpyKnife::SafeGeometry(separation,SpyKnife::Unit(separation),{d.forward.x,d.forward.y,0},d.safety,d.yawError);
        H::Draw.RenderLine(at(a,d.radius),at(b,d.radius),safe?accent:muted,true);
        if(safe) H::Draw.RenderLine(at(a,d.radius*.82f),at(b,d.radius*.82f),Color_t(accent.r,accent.g,accent.b,90),true);
    }
    H::Draw.RenderLine(d.origin+Vec3(0,0,2),at(heading+pi*.5f,d.radius),muted,true);
    H::Draw.RenderLine(d.origin+Vec3(0,0,2),at(heading+pi*1.5f,d.radius),muted,true);
    const Vec3 forward=d.origin+d.forward*28.f+Vec3(0,0,3);
    H::Draw.RenderLine(d.origin+Vec3(0,0,3),forward,accent,true);
    H::Draw.RenderLine(forward,forward-d.forward*7.f+Vec3(-d.forward.y,d.forward.x,0)*5.f,accent,true);
    H::Draw.RenderLine(forward,forward-d.forward*7.f-Vec3(-d.forward.y,d.forward.x,0)*5.f,accent,true);
    if(d.route) for(int i=1;i<d.count;++i) H::Draw.RenderLine(d.path[i-1]+Vec3(0,0,3),d.path[i]+Vec3(0,0,3),accent,true);
#endif
}
void CSpyKnife::DrawHUD()
{
#ifndef TEXTMODE
    const auto d=m_display.Get();
    if(!d.active||GetTickCount64()>d.expires||SDK::CleanScreenshot()||I::EngineVGui->IsGameUIVisible()) return;
    static constexpr const char* labels[]={"No target","Backstab ready","Need a deeper rear angle","Out of knife reach",
        "Contact obstructed","Pose / final aim uncertain","Protected / ignored target","Knife not ready","Warp approach",
        "Waiting for movement acknowledgement","Sidestab approach","Matador feint"};
    const int status=int(d.status);if(status<0||status>=int(std::size(labels))) return;
    auto* draw=ImGui::GetBackgroundDrawList();const auto screen=ImGui::GetIO().DisplaySize;
    if(screen.x<100||screen.y<100) return;
    char detail[96];std::snprintf(detail,sizeof(detail),"Rear margin %.0f%%   |   Safety %.0f%%",(std::max)(d.margin,0.f)*100,d.safety*100);
    const float measured=(std::max)(ImGui::CalcTextSize(labels[status]).x,ImGui::CalcTextSize(detail).x);
    const float fit=(std::min)(1.f,(screen.x-48.f)/(std::max)(measured,1.f));
    const float font=ImGui::GetFontSize()*fit,width=(std::max)(250.f*fit,measured*fit+24.f),height=font*2.7f;
    const ImVec2 pos{(screen.x-width)*.5f,screen.y*.72f};
    const ImU32 accent=IM_COL32(d.color.r,d.color.g,d.color.b,240);
    draw->AddRectFilled(pos,{pos.x+width,pos.y+height},IM_COL32(18,20,26,220),6.f);
    draw->AddText(ImGui::GetFont(),font,{pos.x+12,pos.y+6},d.status==Status::Ready?accent:IM_COL32(228,230,237,240),labels[status]);
    draw->AddText(ImGui::GetFont(),font,{pos.x+12,pos.y+font+10},IM_COL32(139,146,163,220),detail);
#endif
}
