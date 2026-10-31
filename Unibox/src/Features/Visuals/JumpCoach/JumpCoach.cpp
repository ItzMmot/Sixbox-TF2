#include "JumpCoach.h"
#ifndef TEXTMODE
#include "../../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../../Aimbot/AutoTrolldier/AutoTrolldier.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../Ticks/Ticks.h"
#include <ImGui/imgui.h>
#include <cstdio>
#include <cstring>

namespace
{
    JumpCoach::Vector Snapshot(Vec3 v) {return {v.x,v.y,v.z};}
    Vec3 Native(JumpCoach::Vector v) {return {v.x,v.y,v.z};}
    bool Launcher(CTFWeaponBase* weapon)
    {
        if(!weapon) return false;
        const int id=weapon->GetWeaponID();
        return id==TF_WEAPON_ROCKETLAUNCHER || id==TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT || id==TF_WEAPON_PARTICLE_CANNON;
    }
    float CvarFloat(ConVar* var,float fallback) {return var?var->GetFloat():fallback;}
}
void CJumpCoach::Reset()
{
    m_display.Set({});m_owner.Term();m_command=-1;m_side=1;
    m_airborne=false;m_takeoffSpeed=m_peakSpeed=m_airStart=m_lastGroundSpeed=m_efficiency=0;
}
JumpCoach::Trace CJumpCoach::Trace(void* context,JumpCoach::Vector from,JumpCoach::Vector to,
    JumpCoach::Vector mins,JumpCoach::Vector maxs)
{
    auto* local=static_cast<CTFPlayer*>(context);
    CTraceFilterWorldAndPropsOnly filter(local);
    CGameTrace hit={};
    SDK::TraceHull(Native(from),Native(to),Native(mins),Native(maxs),local->SolidMask(),&filter,&hit);
    return {hit.fraction,Snapshot(hit.plane.normal),hit.startsolid||hit.allsolid,bool(hit.surface.flags&(SURF_SKY|SURF_SKY2D))};
}
void CJumpCoach::Update(CTFPlayer* local,CTFWeaponBase* weapon,const CUserCmd* cmd)
{
    using namespace Vars::Misc::Movement::JumpCoach;
    if(!Enabled.Value || !local || !cmd || G::Unload || !I::GlobalVars || !local->IsAlive() || local->IsAGhost()
        || I::EngineClient->IsPlayingDemo() || I::ClientState->IsPaused() || I::EngineVGui->IsGameUIVisible()
        || local->m_MoveType()!=MOVETYPE_WALK || local->IsSwimming() || local->IsTaunting()
        || local->InCond(TF_COND_SHIELD_CHARGE) || local->InCond(TF_COND_PARACHUTE_ACTIVE)
        || local->InCond(TF_COND_HALLOWEEN_KART) || local->InCond(TF_COND_STUNNED)
        || local->InCond(TF_COND_AIR_CURRENT) || local->InCond(TF_COND_ROCKETPACK)
        || local->m_hGrapplingHookTarget().Get() || local->m_flWaterJumpTime()>0
        || !local->m_vecBaseVelocity().IsZero() || F::Ticks.m_bShifting || F::Ticks.m_bWarp
        || F::Ticks.m_bRecharge || (SoldierOnly.Value && local->m_iClass()!=TF_CLASS_SOLDIER))
    {Reset();return;}
    if(m_owner.Get()!=local || cmd->command_number<m_command || int64_t(cmd->command_number)>int64_t(m_command)+8)
    {Reset();m_owner.Set(local);}
    if(cmd->command_number==m_command) return;
    m_command=cmd->command_number;
    static auto air=H::ConVars.FindVar("sv_airaccelerate"),maxVelocity=H::ConVars.FindVar("sv_maxvelocity");
    m_physics.interval=TICK_INTERVAL;
    const float gravityScale=local->m_flGravity();
    m_physics.gravity=SDK::GetGravity()*(gravityScale!=0?gravityScale:1.f);
    m_physics.airAcceleration=CvarFloat(air,10.f);m_physics.maxVelocity=CvarFloat(maxVelocity,3500.f);
    m_physics.wishSpeed=SDK::MaxSpeed(local,false);m_physics.friction=local->m_surfaceFriction();
    m_physics.airCap=30.f*SDK::AttribHookValue(1.f,"mod_air_control",local);
    if(local->InCond(TF_COND_BLASTJUMPING)) m_physics.airCap*=SDK::AttribHookValue(1.f,"mod_air_control_blast_jump",local);
    if(!m_physics.Valid() || !std::isfinite(cmd->viewangles.y) || !std::isfinite(cmd->forwardmove) || !std::isfinite(cmd->sidemove)
        || !std::isfinite(G::OriginalCmd.viewangles.y) || !std::isfinite(G::OriginalCmd.viewangles.x) || !std::isfinite(PredictionTime.Value)) {Reset();return;}
    JumpCoach::Body body{Snapshot(local->m_vecOrigin()),Snapshot(local->m_vecVelocity()),Snapshot(local->m_vecMins()+PLAYER_ORIGIN_COMPRESSION),Snapshot(local->m_vecMaxs()-PLAYER_ORIGIN_COMPRESSION)};
    if(!body.origin.Finite() || !body.velocity.Finite() || !std::isfinite(body.velocity.Horizontal())) {Reset();return;}
    const float yaw=cmd->viewangles.y*JumpCoach::Pi/180.f,c=std::cos(yaw),s=std::sin(yaw);
    JumpCoach::Vector wish{c*cmd->forwardmove+s*cmd->sidemove,s*cmd->forwardmove-c*cmd->sidemove,0};
    const float inputSpeed=(std::min)(m_physics.wishSpeed,wish.Horizontal());
    wish=JumpCoach::Unit(wish);
    if(G::OriginalCmd.sidemove>1) m_side=-1;
    else if(G::OriginalCmd.sidemove<-1) m_side=1;
    else if(G::OriginalCmd.mousedx) m_side=G::OriginalCmd.mousedx>0?-1:1;
    Display display;
    display.active=true;display.grounded=local->IsOnGround();display.origin=Native(body.origin);display.side=m_side;
    display.speed=body.velocity.Horizontal();display.vertical=body.velocity.z;
    display.showHUD=Vars::Misc::Movement::JumpCoach::DrawHUD.Value;display.showStrafe=StrafeHints.Value;display.landingMarker=DrawLanding.Value;
    display.depth=!ThroughWalls.Value;display.color=PathColor.Value;
    display.horizon=(std::min)(std::clamp(PredictionTime.Value,.25f,5.f),JumpCoach::MaxTicks*m_physics.interval);
    display.hudX=std::isfinite(HUDX.Value)?std::clamp(HUDX.Value,0.f,100.f):50.f;
    display.hudY=std::isfinite(HUDY.Value)?std::clamp(HUDY.Value,0.f,100.f):70.f;
    display.scale=std::isfinite(HUDScale.Value)?std::clamp(HUDScale.Value,.6f,1.6f):1.f;
    display.markerRadius=std::clamp((body.maxs.x-body.mins.x+body.maxs.y-body.mins.y)*.25f,8.f,80.f);
    display.automated=F::AutoTrolldier.IsActive(cmd) || Vars::Misc::Movement::AutoStrafe.Value!=0;
    const float now=TICKS_TO_TIME(local->m_nTickBase());
    if(display.grounded)
    {
        m_airborne=false;m_lastGroundSpeed=display.speed;m_efficiency=0;
        display.end=JumpCoach::End::Grounded;
    }
    else
    {
        if(!m_airborne) {m_airStart=now;m_takeoffSpeed=m_lastGroundSpeed>0?m_lastGroundSpeed:display.speed;m_peakSpeed=display.speed;m_airborne=true;}
        m_peakSpeed=(std::max)(m_peakSpeed,display.speed);
        display.launchGain=display.speed-m_takeoffSpeed;display.peakLoss=m_peakSpeed-display.speed;
        display.airtime=(std::max)(0.f,now-m_airStart);
        const auto guide=JumpCoach::Guide(body.velocity,wish,inputSpeed,G::OriginalCmd.viewangles.y,m_side,m_physics);
        const float blend=1.f-std::exp(-m_physics.interval/.12f);
        m_efficiency+=(guide.efficiency-m_efficiency)*blend;
        display.guide=guide.valid && display.speed>=MinimumSpeed.Value;
        display.efficiency=m_efficiency;display.turn=JumpCoach::Wrap(guide.cameraYaw-G::OriginalCmd.viewangles.y);
        display.wish=Native(guide.wish);
        const int ticks=std::clamp(int(std::ceil(display.horizon/m_physics.interval)),1,JumpCoach::MaxTicks);
        const int budget=std::clamp(TraceBudget.Value,256,8192);
        JumpCoach::Predict(body,wish,inputSpeed,m_physics,ticks,budget,Trace,local,false,m_side,m_plan);
        display.end=m_plan.end;
        if(DrawPath.Value)
        {
            display.count=m_plan.count;
            for(int i=0;i<display.count;++i) display.path[i]=Native(m_plan.points[i].origin);
        }
        if(m_plan.end==JumpCoach::End::Landed)
        {
            const auto& end=m_plan.points[m_plan.count-1];
            display.landing=Native(m_plan.landing);display.normal=Native(m_plan.normal);
            display.landTime=end.time;display.landSpeed=end.velocity.Horizontal();
            const auto delta=end.origin-body.origin;display.landDistance=delta.Horizontal();
        }
        if(DrawIdeal.Value && display.guide)
        {
            JumpCoach::Predict(body,{},0,m_physics,ticks,(std::max)(0,budget-m_plan.traces),Trace,local,true,m_side,m_ideal);
            display.idealCount=m_ideal.count;
            for(int i=0;i<display.idealCount;++i) display.ideal[i]=Native(m_ideal.points[i].origin);
        }
    }
    if(RocketTiming.Value) RocketAdvice(local,weapon,cmd,body,display);
    display.expires=GetTickCount64()+250;
    if(OnlyAir.Value && display.grounded) display.active=false;
    m_display.Set(display);
}
void CJumpCoach::RocketAdvice(CTFPlayer* local,CTFWeaponBase* weapon,const CUserCmd* cmd,
    const JumpCoach::Body& body,Display& display)
{
    if(local->m_iClass()!=TF_CLASS_SOLDIER) return;
    if(!Launcher(weapon))
    {
        if(Launcher(local->GetWeaponFromSlot(SLOT_PRIMARY))) display.rocket=RocketState::Equip;
        return;
    }
    display.rocket=RocketState::Unsupported;
    if(weapon->m_iItemDefinitionIndex()==Soldier_m_TheBeggarsBazooka || !local->CanAttack() || cmd->weaponselect
        || (weapon->GetWeaponID()==TF_WEAPON_PARTICLE_CANNON && weapon->As<CTFParticleCannon>()->m_flChargeBeginTime()>0)) return;
    if(F::AutoRocketJump.IsRunning()) {display.rocket=RocketState::Automatic;return;}
    if(!weapon->HasPrimaryAmmoForShot()) {display.rocket=RocketState::NoAmmo;return;}
    if(weapon->IsInReload()) {display.rocket=RocketState::Reload;return;}
    if(display.grounded) {display.rocket=RocketState::Grounded;return;}
    if(m_plan.end!=JumpCoach::End::Landed) {display.rocket=RocketState::NoLanding;return;}
    const float now=TICKS_TO_TIME(local->m_nTickBase());
    const float cooldown=(std::max)(0.f,(std::max)(weapon->m_flNextPrimaryAttack(),local->m_flNextAttack())-now);
    if(!std::isfinite(cooldown)) return;
    if(cooldown>display.landTime) {display.rocket=RocketState::Cooldown;display.shotDelay=cooldown;return;}
    display.rocket=RocketState::NoWindow;
    const float lead=std::isfinite(Vars::Misc::Movement::JumpCoach::ShotLead.Value)
        ? std::clamp(Vars::Misc::Movement::JumpCoach::ShotLead.Value,0.f,.15f):.03f;
    const float targetTime=display.landTime-lead;
    if(targetTime<=m_physics.interval) return;
    const auto mode=RocketJump::GetMode(Vars::Misc::Movement::AutoRocketJumpMode.Value);
    JumpCoach::Vector direction=body.velocity;direction.z=0;direction=JumpCoach::Unit(direction);
    if(direction.Squared()<.5f)
    {
        Vec3 facing;Math::AngleVectors({0,G::OriginalCmd.viewangles.y,0},&facing);direction=Snapshot(facing);
    }
    const float offset=mode==RocketJump::Mode::Vertical?0.f:mode==RocketJump::Mode::Horizontal?56.f:32.f;
    const Vec3 desired=display.landing-Native(direction)*offset;
    CTraceFilterWorldAndPropsOnly filter(local);
    CGameTrace ground={};
    SDK::Trace(desired+Vec3(0,0,48),desired-Vec3(0,0,128),MASK_SOLID,&filter,&ground);
    if(!ground.DidHit() || ground.startsolid || ground.allsolid || ground.plane.normal.z<.7f
        || (ground.surface.flags&(SURF_SKY|SURF_SKY2D))) return;
    const Vec3 aimPoint=ground.endpos;
    CTraceFilterCollideable rocketFilter(local);
    // Supported rockets are straight, drag-free projectiles. Do not call
    // ProjSim::SetupTrace: it requires an initialized shared physics object.
    const int rocketMask=MASK_SOLID;
    rocketFilter.m_bWeaponCached=true;
    rocketFilter.m_bWeapon=false;
    const int ready=int(std::ceil(cooldown/m_physics.interval));
    // A bounded neighbourhood around the expected floor-shot time. The final
    // candidate uses the launcher's actual speed, muzzle offset and hull.
    ProjectileInfo initial;
    if(!F::ProjSim.GetInfo(local,weapon,{89.f,cmd->viewangles.y,0},initial,ProjSimEnum::NoRandomAngles)
        || !std::isfinite(initial.m_flVelocity) || initial.m_flVelocity<1 || initial.m_flVelocity>10000 || initial.m_flGravity!=0) return;
    const float roughTravel=local->m_vecViewOffset().Length()/initial.m_flVelocity;
    const int guess=int(std::floor((targetTime-roughTravel)/m_physics.interval))-1;
    JumpCoach::Shot best;Vec3 bestView;
    for(int candidate=guess-7;candidate<=guess+7;++candidate)
    {
        if(candidate<ready || candidate<0) continue;
        JumpCoach::Sample launch;
        const float fireDelay=candidate*m_physics.interval;
        if(!JumpCoach::AtTime(m_plan,fireDelay+m_physics.interval,launch)) continue;
        const Vec3 shift=Native(launch.origin-body.origin);
        Vec3 angles=Math::CalcAngle(Native(launch.origin)+local->m_vecViewOffset(),aimPoint);
        ProjectileInfo info;
        Vec3 muzzle;
        bool valid=true;
        for(int refinement=0;refinement<3;++refinement)
        {
            Math::ClampAngles(angles);
            if(!F::ProjSim.GetInfo(local,weapon,angles,info,ProjSimEnum::NoRandomAngles)
                || info.m_flGravity!=0 || !std::isfinite(info.m_flVelocity) || info.m_flVelocity<1 || info.m_flVelocity>10000
                || !info.m_vVelocityAdd.IsZero()) {valid=false;break;}
            muzzle=info.m_vPos+shift;
            if(refinement<2) angles=Math::CalcAngle(muzzle,aimPoint);
        }
        if(!valid) continue;
        CGameTrace clearance={};
        SDK::TraceHull(local->GetShootPos()+shift,muzzle,-info.m_vHull,info.m_vHull,rocketMask,&rocketFilter,&clearance);
        if(clearance.startsolid || clearance.allsolid || clearance.DidHit()) continue;
        Vec3 forward;Math::AngleVectors(info.m_vAng,&forward);
        const float distance=aimPoint.DistTo(muzzle);
        if(!std::isfinite(distance) || distance<1 || distance>2000) continue;
        CGameTrace hit={};
        SDK::TraceHull(muzzle,muzzle+forward*(distance+64.f),-info.m_vHull,info.m_vHull,rocketMask,&rocketFilter,&hit);
        if(!hit.DidHit() || hit.startsolid || hit.allsolid || hit.plane.normal.z<.7f
            || (hit.m_pEnt && (hit.m_pEnt->IsPlayer() || hit.m_pEnt->IsBuilding()))
            || (hit.surface.flags&(SURF_SKY|SURF_SKY2D))) continue;
        const float travel=hit.endpos.DistTo(muzzle)/info.m_flVelocity;
        const float impact=fireDelay+m_physics.interval+travel;
        JumpCoach::Sample atImpact;
        if(!JumpCoach::AtTime(m_plan,impact,atImpact)) continue;
        const Vec3 blast=hit.endpos+hit.plane.normal;
        const float nearest=JumpCoach::BlastDistance(Snapshot(blast),atImpact.origin,body.mins,body.maxs);
        if(nearest>=TF_ROCKET_RADIUS_FOR_RJS-12.f) continue;
        const Vec3 force=Native(atImpact.origin+(body.mins+body.maxs)*.5f)+Vec3(0,0,10)-blast;
        const float score=RocketJump::BoostScore(mode,force.x,force.y,force.z,nearest,TF_ROCKET_RADIUS_FOR_RJS,direction.x,direction.y);
        if(score<=0 || !SDK::VisPosWorld(local,local,blast,Native(atImpact.origin)+local->m_vecViewOffset(),MASK_SHOT)) continue;
        const float previousError=best.error;
        JumpCoach::ConsiderShot(best,fireDelay,travel,targetTime,display.landTime,m_physics.interval,Snapshot(hit.endpos));
        if(best.error<previousError) bestView=angles;
    }
    if(best.found && best.error<=(std::max)(.025f,m_physics.interval*1.5f))
    {
        display.shotDelay=best.delay;display.shotPoint=Native(best.point);display.showShot=true;
        display.rocket=best.delay<m_physics.interval*.5f?RocketState::Now:RocketState::Wait;
        if(display.rocket==RocketState::Now)
        {
            Vec3 current,desired;Math::AngleVectors(G::OriginalCmd.viewangles,&current);Math::AngleVectors(bestView,&desired);
            if(!JumpCoach::Aligned(Snapshot(current),Snapshot(desired))) display.rocket=RocketState::Aim;
        }
    }
}
void CJumpCoach::RenderWorld()
{
    const auto data=m_display.Get();
    if(!data.active || GetTickCount64()>data.expires || SDK::CleanScreenshot() || I::EngineVGui->IsGameUIVisible()) return;
    auto path=[&](const auto& points,int count,Color_t color,bool dashed) {
        const int stride=(std::max)(1,(count+126)/128);
        int previous=0,segment=0;
        while(previous<count-1)
        {
            const int next=(std::min)(previous+stride,count-1);
            if(!dashed || !(segment&1)) H::Draw.RenderLine(points[previous],points[next],color,data.depth);
            previous=next;++segment;
        }
    };
    Color_t faint=data.color;faint.a=uint8_t(faint.a*.35f);
    path(data.ideal,data.idealCount,faint,true);path(data.path,data.count,data.color,false);
    if(data.end==JumpCoach::End::Landed && data.landingMarker)
    {
        const Vec3 normal=data.normal;
        Vec3 right=normal.Cross(std::abs(normal.z)>.99f?Vec3(0,1,0):Vec3(0,0,1));right.Normalize();
        const Vec3 up=normal.Cross(right),centre=data.landing+normal*1.5f;
        for(int i=0;i<32;++i)
        {
            const float a=i*JumpCoach::Pi/16.f,b=(i+1)*JumpCoach::Pi/16.f;
            H::Draw.RenderLine(centre+(right*std::cos(a)+up*std::sin(a))*data.markerRadius,
                centre+(right*std::cos(b)+up*std::sin(b))*data.markerRadius,data.color,data.depth);
        }
        H::Draw.RenderLine(centre-right*7,centre+right*7,data.color,data.depth);
        H::Draw.RenderLine(centre-up*7,centre+up*7,data.color,data.depth);
    }
    if(data.showShot)
    {
        const Vec3 p=data.shotPoint+Vec3(0,0,2);
        const Color_t color=data.rocket==RocketState::Now?data.color:Color_t(215,177,108,210);
        H::Draw.RenderLine(p-Vec3(8,0,0),p+Vec3(8,0,0),color,data.depth);
        H::Draw.RenderLine(p-Vec3(0,8,0),p+Vec3(0,8,0),color,data.depth);
        H::Draw.RenderLine(p,p+Vec3(0,0,12),color,data.depth);
    }
}
void CJumpCoach::DrawHUD()
{
    const auto data=m_display.Get();
    if(!data.active || !data.showHUD || GetTickCount64()>data.expires) return;
    ImDrawList* draw=ImGui::GetBackgroundDrawList();
    const auto screen=ImGui::GetIO().DisplaySize;
    if(screen.x<100 || screen.y<100) return;
    ImFont* font=ImGui::GetFont();
    const float scale=(std::min)(data.scale,screen.y/(ImGui::GetFontSize()*12+180)),fontSize=ImGui::GetFontSize()*scale;
    const float hintSize=fontSize*.88f,pad=12*scale,line=fontSize+7*scale;
    const float width=(std::min)((std::max)(320*scale,fontSize*21),screen.x-16);
    const float height=pad*2+line*(data.showStrafe?8.f:6.f)+(data.rocket!=RocketState::Hidden?line:0);
    const float x=std::clamp(screen.x*data.hudX*.01f-width*.5f,8.f,(std::max)(8.f,screen.x-width-8));
    const float y=std::clamp(screen.y*data.hudY*.01f,8.f,(std::max)(8.f,screen.y-height-8));
    const ImU32 text=IM_COL32(228,230,237,245),muted=IM_COL32(139,146,163,240),warning=IM_COL32(215,177,108,245);
    const ImU32 accent=IM_COL32(data.color.r,data.color.g,data.color.b,245);
    draw->AddRectFilled({x,y},{x+width,y+height},IM_COL32(18,20,26,225),8*scale);
    draw->AddRectFilled({x,y},{x+3*scale,y+height},accent,2*scale);
    auto label=[&](float px,float py,const char* value,ImU32 color,float size,float limit=FLT_MAX) {
        const float available=(std::min)(limit,x+width-pad-px);
        if(available<1) return;
        char clipped[144];
        if(font->CalcTextSizeA(size,FLT_MAX,0,value).x>available)
        {
            const float dots=font->CalcTextSizeA(size,FLT_MAX,0,"...").x;
            if(dots>available) return;
            const char* stop=value;
            font->CalcTextSizeA(size,(std::max)(0.f,available-dots),0,value,nullptr,&stop);
            const size_t length=(std::min)(size_t(stop-value),sizeof(clipped)-4);
            std::memcpy(clipped,value,length);std::memcpy(clipped+length,"...",4);value=clipped;
        }
        draw->AddText(font,size,{px,py},color,value);
    };
    auto right=[&](float py,const char* value,ImU32 color,float size) {
        const float budget=(width-pad*2)*.38f;
        const float length=(std::min)(font->CalcTextSizeA(size,FLT_MAX,0,value).x,budget);
        label(x+width-pad-length,py,value,color,size,budget);
    };
    char buffer[144];float row=y+pad;
    label(x+pad,row,"Jump coach",text,fontSize);right(row,data.grounded?"GROUND":"AIR",accent,hintSize);row+=line;
    std::snprintf(buffer,sizeof(buffer),"%.0f HU/s",data.speed);label(x+pad,row,buffer,text,fontSize*1.15f,(width-pad*2)*.6f);
    std::snprintf(buffer,sizeof(buffer),"Vz %+.0f",data.vertical);right(row,buffer,muted,hintSize);row+=line*1.2f;
    std::snprintf(buffer,sizeof(buffer),"Launch %+.0f  |  Peak loss %.0f HU/s",data.launchGain,data.peakLoss);
    label(x+pad,row,buffer,data.peakLoss>25?warning:muted,hintSize);row+=line;
    if(data.showStrafe)
    {
        if(data.grounded) std::snprintf(buffer,sizeof(buffer),"Jump to start strafe coaching");
        else if(!data.guide) std::snprintf(buffer,sizeof(buffer),"Build speed before strafing");
        else std::snprintf(buffer,sizeof(buffer),"Strafe %s  |  gain efficiency %.0f%%",data.side>0?"A":"D",data.efficiency*100.f);
        if(data.automated && !data.grounded && data.guide)
            std::snprintf(buffer,sizeof(buffer),"Auto movement  |  gain efficiency %.0f%%",data.efficiency*100.f);
        label(x+pad,row,buffer,data.guide?accent:muted,hintSize);row+=line;
        const float centre=x+width*.5f,barY=row+3*scale,half=(width-pad*2)*.5f;
        draw->AddLine({centre-half,barY},{centre+half,barY},IM_COL32(65,71,86,230),2*scale);
        draw->AddLine({centre,barY-4*scale},{centre,barY+4*scale},muted,scale);
        if(data.guide)
        {
            const float marker=centre-std::clamp(data.turn/45.f,-1.f,1.f)*half;
            draw->AddCircleFilled({marker,barY},4*scale,accent,12);
            std::snprintf(buffer,sizeof(buffer),std::abs(data.turn)<1.f?"Camera aligned":"Look %s %.1f deg",data.turn>0?"left":"right",std::abs(data.turn));
            label(x+pad,row+9*scale,buffer,muted,hintSize);
        }
        row+=line*1.5f;
    }
    if(data.end==JumpCoach::End::Landed)
    {
        std::snprintf(buffer,sizeof(buffer),"Landing %.2fs  |  %.0f HU",data.landTime,data.landDistance);
        label(x+pad,row,buffer,text,hintSize);row+=line;
        std::snprintf(buffer,sizeof(buffer),"Landing speed %.0f HU/s",data.landSpeed);
        label(x+pad,row,buffer,muted,hintSize);
    }
    else
    {
        const char* status=data.end==JumpCoach::End::Grounded?"Flight prediction starts in the air":
            data.end==JumpCoach::End::Budget?"Partial path: trace budget reached":
            data.end==JumpCoach::End::Blocked?"Partial path: blocked / unsupported contact":
            data.end==JumpCoach::End::Invalid?"Flight estimate unavailable":"No landing within the prediction horizon";
        label(x+pad,row,status,muted,hintSize);row+=line;
        std::snprintf(buffer,sizeof(buffer),"Prediction %.2fs",data.horizon);label(x+pad,row,buffer,muted,hintSize);
    }
    row+=line;
    if(data.rocket!=RocketState::Hidden)
    {
        switch(data.rocket)
        {
        case RocketState::Grounded: std::snprintf(buffer,sizeof(buffer),"Rocket: start your jump first");break;
        case RocketState::Equip: std::snprintf(buffer,sizeof(buffer),"Rocket: equip your launcher");break;
        case RocketState::Unsupported: std::snprintf(buffer,sizeof(buffer),"Rocket: unsupported firing mode");break;
        case RocketState::NoAmmo: std::snprintf(buffer,sizeof(buffer),"Rocket: load a rocket first");break;
        case RocketState::Reload: std::snprintf(buffer,sizeof(buffer),"Rocket: interrupt reload first");break;
        case RocketState::Cooldown: std::snprintf(buffer,sizeof(buffer),"Rocket cooldown %.2fs: too late",data.shotDelay);break;
        case RocketState::NoLanding: std::snprintf(buffer,sizeof(buffer),"Rocket: landing not predicted");break;
        case RocketState::NoWindow: std::snprintf(buffer,sizeof(buffer),"Rocket: no verified shot window");break;
        case RocketState::Wait: std::snprintf(buffer,sizeof(buffer),"Rocket in %.2fs - aim at marker",data.shotDelay);break;
        case RocketState::Aim: std::snprintf(buffer,sizeof(buffer),"NOW: aim at floor marker");break;
        case RocketState::Now: std::snprintf(buffer,sizeof(buffer),"SHOOT NOW - floor marker");break;
        case RocketState::Automatic: std::snprintf(buffer,sizeof(buffer),"Rocket: Auto RJ is launching");break;
        default: buffer[0]=0;break;
        }
        label(x+pad,row,buffer,data.rocket==RocketState::Now?accent:muted,hintSize);row+=line;
    }
    label(x+pad,row,"Estimate: hold current input and yaw",muted,hintSize);
}
#endif
