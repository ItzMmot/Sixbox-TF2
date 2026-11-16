#include "AutoStickyJump.h"
#include "../../ImGui/Menu/Menu.h"
#include "../AutoRocketJump/JumpPolicy.h"
#include "../AutoTrolldier/Rotation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../Ticks/Ticks.h"
#include "../../Configs/Configs.h"
#include <fstream>

namespace
{
    using namespace Vars::Misc::Movement::AutoStickyJump;
    using Phase=StickyJump::Phase;
    bool Floor(const CGameTrace& t)
    {
        return t.DidHit()&&!t.startsolid&&!t.allsolid&&t.plane.normal.z>=.7f
            &&!(t.surface.flags&(SURF_SKY|SURF_SKY2D));
    }
    float Time(CTFPlayer* local) {return TICKS_TO_TIME(local->m_nTickBase());}
}
void CAutoStickyJump::Reset()
{
    F::Ticks.CancelStickyJumpBurst();
    m_display.Set({});m_baseline.clear();m_plan={};m_fire={};m_flush={};m_flushDone=m_flushHeading=false;m_flushWish={};m_firstSeen.clear();m_reason=nullptr;m_diagnostics={};m_loggedStatus=nullptr;m_nextDiagnostics=0;m_owner.Term();m_weapon.Term();m_phase=Phase::Plan;
    m_mode=m_rotationMode=m_lastCommand=m_controlCommand=m_chargeCommand=m_jumpCommand=m_shotCommand=-1;m_released=0;m_spawn=-1;
    m_started=m_phaseTime=m_lastRelease=0;m_lastSearch=m_lastShotCheck=-100;m_pitchSpeed=m_yawSpeed=0;
    m_rotationValid=m_airborne=m_burstTried=false;m_worldMove={};
}
void CAutoStickyJump::Stage(Phase phase,float now) {m_phase=phase;m_phaseTime=now;m_reason=nullptr;}
void CAutoStickyJump::Publish(float remaining,const char* status)
{
    m_plan.active=true;m_plan.phase=m_phase;m_plan.wanted=std::clamp(Stickies.Value,1,2);
    m_plan.armRemaining=(std::max)(0.f,remaining);if(status) m_reason=status;
    m_plan.status=m_reason?m_reason:StickyJump::Label(m_phase);
    m_plan.expires=GetTickCount64()+350;m_display.Set(m_plan);
    const auto stamp=GetTickCount64();
    const bool nearWindow=m_phase==Phase::Approach&&m_diagnostics.best>0&&m_diagnostics.ahead<=.12f;
    if(m_loggedPhase!=m_phase||m_loggedStatus!=m_plan.status||(nearWindow&&stamp>=m_nextDiagnostics))
    {
        m_nextDiagnostics=stamp+100;m_loggedPhase=m_phase;m_loggedStatus=m_plan.status;
        if(!F::Configs.m_sCorePath.empty())
        {
            const auto path=std::filesystem::path(F::Configs.m_sCorePath)/"sticky-jump.log";
            std::error_code error;const auto bytes=std::filesystem::file_size(path,error);
            std::ofstream log(path,(!error&&bytes>1024*1024)?std::ios::trunc:std::ios::app);
            auto* local=m_owner.Get();auto* weapon=m_weapon.Get();
            if(log&&local&&weapon)
                log<<"v4 cmd="<<G::OriginalCmd.command_number<<" time="<<Time(local)<<" phase="<<int(m_phase)
                   <<" status="<<m_plan.status<<" count="<<m_plan.count<<'/'<<m_plan.wanted<<" sent="<<m_released
                   <<" attached="<<m_diagnostics.attached<<" armed="<<m_diagnostics.armed<<" clear="<<m_diagnostics.clear
                   <<" ground="<<local->IsOnGround()<<" secondary="<<weapon->CanSecondaryAttack()
                   <<" next_secondary="<<weapon->m_flNextSecondaryAttack()<<" owner_next_attack="<<local->m_flNextAttack()
                   <<" speed="<<local->m_vecVelocity().Length2D()<<" raw_speed="<<m_diagnostics.rawSpeed
                   <<" raw_created="<<m_diagnostics.rawCreation<<" arm_remaining="<<m_plan.armRemaining
                   <<" score="<<m_diagnostics.current<<" best="<<m_diagnostics.best<<" ahead="<<m_diagnostics.ahead<<'\n';
            if(!log)
            {
                static bool reported=false;
                if(!reported) {reported=true;SDK::Output("Sticky jump","Cannot write Core/sticky-jump.log",WARNING_COLOR);}
            }
        }
    }
}
bool CAutoStickyJump::FlightPreview() const
{
    return Enabled.Value&&CoachIntegration.Value&&m_plan.valid&&m_phase!=Phase::Done&&m_phase!=Phase::Failed;
}
float CAutoStickyJump::ArmTime(CTFPlayer* local,CTFWeaponBase* weapon) const
{
    static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime");
    const float base=live?live->GetFloat():.8f;
    const int rune=local->InCond(TF_COND_RUNE_HASTE)?1:
        (local->InCond(TF_COND_RUNE_KING)||local->InCond(TF_COND_KING_BUFFED))?2:0;
    // Attribute is evaluated on the real weapon, including server attributes.
    return StickyJump::ArmTime(SDK::AttribHookValue(base,"sticky_arm_time",weapon),1.f,rune);
}
float CAutoStickyJump::CreationTime(CTFGrenadePipebombProjectile* sticky,float now)
{
    const int id=sticky->GetRefEHandle().ToInt();
    const auto [seen,_]=m_firstSeen.try_emplace(id,now);
    const float created=sticky->m_flCreationTime();
    // Some client snapshots have no valid creation timestamp. First observation
    // is a conservative lower bound on age, never a reason to call it armed.
    return StickyJump::CreationTime(created,now,m_started,seen->second,m_phase==Phase::Flush);
}
bool CAutoStickyJump::Flush(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    const auto& projectiles=H::Entities.GetGroup(EntityEnum::LocalStickies);
    if(m_flush.Done(int(projectiles.size()),weapon->As<CTFPipebombLauncher>()->m_iPipebombCount(),G::OriginalCmd.command_number))
    {
        m_flushDone=true;m_firstSeen.clear();m_baseline.clear();m_lastSearch=-100;
        if(m_phase==Phase::Flush) {Stage(Phase::Plan,Time(local));m_rotationValid=false;}
        return true;
    }
    const float now=Time(local);
    if(m_phase!=Phase::Flush)
    {
        m_flushWish=m_worldMove.Normalized2D();
        if(m_flushWish.IsZero()) Math::AngleVectors({0,G::OriginalCmd.viewangles.y,0},&m_flushWish);
        m_flushHeading=true;Stage(Phase::Flush,now);
    }
    if(AutoWalk.Value&&local->m_vecVelocity().Length2D()>30.f)
        m_worldMove=local->m_vecVelocity().Normalized2D()*450.f;
    if(now-m_phaseTime>5.f) {Stage(Phase::Failed,now);Publish(0,"Could not flush stickies - toggle to retry");return false;}
    CTFGrenadePipebombProjectile* selected=nullptr;float soonest=FLT_MAX;
    for(auto* entity:projectiles)
    {
        auto* sticky=entity->As<CTFGrenadePipebombProjectile>();
        if(sticky->m_hOriginalLauncher().Get()!=weapon) continue;
        const float remaining=CreationTime(sticky,now)+ArmTime(local,weapon)+TICK_INTERVAL-now;
        if(remaining<soonest) {soonest=remaining;selected=sticky;}
    }
    const bool directional=weapon->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT;
    bool aim=!directional;
    if(directional&&selected) aim=Rotate(cmd,Math::CalcAngle(local->GetShootPos(),selected->m_vecOrigin()));
    if(aim&&(!selected||soonest<=0)) m_setButtons|=IN_ATTACK2;
    m_plan.count=int(projectiles.size());Publish(selected?(std::max)(0.f,soonest):0,"Flushing old stickies - waiting for removal");
    return false;
}
bool CAutoStickyJump::Route(CTFPlayer* local,const Vec3& to,Display* display)
{
    CTraceFilterWorldAndPropsOnly filter(local);
    const Vec3 start=local->m_vecOrigin(),delta=to-start;
    const int steps=std::clamp(int(std::ceil(delta.Length2D()/16.f)),1,90);
    Vec3 previous=start;
    if(display) {display->approachCount=1;display->approach[0]=start+Vec3(0,0,2);}
    for(int n=1;n<=steps;++n)
    {
        Vec3 next=start+delta*(float(n)/steps);next.z=previous.z;
        CGameTrace sweep{},floor{};
        SDK::TraceHull(previous+Vec3(0,0,18),next+Vec3(0,0,18),local->m_vecMins(),local->m_vecMaxs(),local->SolidMask(),&filter,&sweep);
        if(sweep.startsolid||sweep.allsolid||sweep.fraction<.999f) return false;
        SDK::TraceHull(next+Vec3(0,0,18),next-Vec3(0,0,64),local->m_vecMins(),local->m_vecMaxs(),local->SolidMask(),&filter,&floor);
        if(!Floor(floor)||std::abs(floor.endpos.z-previous.z)>18.1f) return false;
        previous=floor.endpos;
        if(display&&display->approachCount<int(display->approach.size())) display->approach[display->approachCount++]=previous+Vec3(0,0,2);
    }
    if(display) display->takeoff=previous;
    return true;
}
bool CAutoStickyJump::Shot(CTFPlayer* local,CTFWeaponBase* weapon,const Vec3& point,Vec3& angle,Display* display,bool validate)
{
    // Prepare runs before EnginePrediction::Start; charge duration still needs
    // the weapon owner's predicted clock, not the last network snapshot time.
    struct ClockScope
    {
        float old=I::GlobalVars->curtime;
        explicit ClockScope(float now) {I::GlobalVars->curtime=now;}
        ~ClockScope() {I::GlobalVars->curtime=old;}
    } clock(Time(local));
    angle=Math::CalcAngle(local->GetShootPos(),point);
    ProjectileInfo info{};
    // Solve the low ballistic arc, including the grenade's upward launch impulse
    // and actual muzzle offset. Verify it with the existing VPhysics simulator.
    for(int refine=0;refine<4;++refine)
    {
        if(!F::ProjSim.GetInfo(local,weapon,angle,info,ProjSimEnum::InitCheck|ProjSimEnum::NoRandomAngles)) return false;
        const Vec3 delta=point-info.m_vPos;
        const float distance=delta.Length2D(),speed=std::hypot(info.m_flVelocity,200.f),g=info.m_flGravity;
        if(distance<1.f||speed<=0||g<=0) return false;
        const float squared=speed*speed,disc=squared*squared-g*(g*distance*distance+2.f*delta.z*squared);
        if(disc<0||!std::isfinite(disc)) return false;
        angle=Math::CalcAngle(info.m_vPos,point);
        angle.x=(-std::atan((squared-std::sqrt(disc))/(g*distance))+std::atan2(200.f,info.m_flVelocity))*180.f/3.141592654f;
        Math::ClampAngles(angle);
    }
    if(!validate) return true;
    if(!F::ProjSim.GetInfo(local,weapon,angle,info,ProjSimEnum::InitCheck|ProjSimEnum::NoRandomAngles)
        ||!F::ProjSim.Initialize(info)) return false;
    CTraceFilterWorldAndPropsOnly filter(local);
    Vec3 previous=F::ProjSim.GetOrigin();
    if(display) {display->trajectoryCount=1;display->trajectory[0]=previous;}
    for(int n=0;n<80;++n)
    {
        F::ProjSim.RunTick(info,false);
        Vec3 next=F::ProjSim.GetOrigin();CGameTrace hit{};
        SDK::TraceHull(previous,next,-info.m_vHull,info.m_vHull,MASK_SOLID,&filter,&hit);
        if(hit.DidHit()) next=hit.endpos;
        if(display&&display->trajectoryCount<int(display->trajectory.size())) display->trajectory[display->trajectoryCount++]=next;
        if(hit.DidHit()) return Floor(hit)&&next.DistTo(point)<32.f;
        previous=next;
    }
    return false;
}
bool CAutoStickyJump::Plan(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    Vec3 wish;Math::AngleVectors({0,G::OriginalCmd.viewangles.y,0},&wish);
    const auto& original=G::OriginalCmd;
    if(std::abs(original.forwardmove)+std::abs(original.sidemove)>5.f)
    {
        Vec3 right;Math::AngleVectors({0,original.viewangles.y,0},nullptr,&right);
        wish=wish*original.forwardmove+right*original.sidemove;wish.Normalize();
    }
    if(m_flushHeading) wish=m_flushWish;
    const float speed=local->m_vecVelocity().Length2D();
    // Pick up the existing run instead of steering a fast player toward camera yaw.
    if(AutoWalk.Value&&speed>30.f) wish=local->m_vecVelocity().Normalized2D();
    const int wanted=std::clamp(Stickies.Value,1,2);
    const float runSpeed=(std::max)(SDK::MaxSpeed(local,false),1.f);
    float rotationLead=.08f;
    if(Rotation.Value>=2)
    {
        const float rotationScale=std::clamp(RotationSpeed.Value/25.f,.45f,2.2f);
        const float yaw=std::abs(Trolldier::WrapAngle(Math::VectorAngles(wish).y-G::OriginalCmd.viewangles.y));
        rotationLead=(std::max)(yaw/(180.f*rotationScale),std::abs(35.f-G::OriginalCmd.viewangles.x)/(125.f*rotationScale))+.25f;
    }
    auto* net=I::EngineClient->GetNetChannelInfo();
    const float confirmation=net?std::clamp(net->GetLatency(FLOW_INCOMING),0.f,.5f):0.f;
    const float delay=confirmation+ArmTime(local,weapon)+(wanted-1)*(weapon->GetFireRate()+3.f*TICK_INTERVAL)+rotationLead+.25f
        +(std::max)(0.f,weapon->m_flNextPrimaryAttack()-Time(local));
    static auto accelerate=H::ConVars.FindVar("sv_accelerate");
    const float acceleration=(accelerate?accelerate->GetFloat():10.f)*runSpeed*(std::max)(.1f,local->m_surfaceFriction());
    const float distance=StickyJump::LeadDistance(speed,runSpeed,acceleration,delay,std::clamp(ApproachDistance.Value,32.f,512.f));
    if(distance<0||distance>1400.f) return false;
    Display plan;plan.wish=wish;
    if(!Route(local,local->m_vecOrigin()+wish*distance,&plan)) return false;
    const bool vertical=Mode.Value==ModeEnum::Vertical;
    const float offsets[]={0,8,16,56,72,88,104};
    CTraceFilterWorldAndPropsOnly filter(local);
    float best=-1;Display selected;
    for(int n=vertical?0:3;n<(vertical?3:7);++n)
    {
        const Vec3 point=plan.takeoff-wish*offsets[n];CGameTrace floor{};
        SDK::Trace(point+Vec3(0,0,24),point-Vec3(0,0,64),MASK_SOLID,&filter,&floor);
        if(!Floor(floor)) continue;
        const Vec3 blast=floor.endpos+floor.plane.normal*2.f;
        if(!SDK::VisPosWorld(local,local,blast,plan.takeoff+Vec3(0,0,28),MASK_SHOT)) continue;
        const Vec3 force=plan.takeoff+Vec3(0,0,38)-blast;
        const float radius=SDK::AttribHookValue(146.f,"mult_explosion_radius",weapon);
        const float score=RocketJump::BoostScore(vertical?RocketJump::Mode::Vertical:RocketJump::Mode::Horizontal,
            force.x,force.y,force.z,(std::max)(0.f,force.Length()-24.f),radius,wish.x,wish.y);
        if(score<=best) continue;
        Display candidate=plan;Vec3 angles;
        if(!Shot(local,weapon,blast,angles,&candidate)) continue;
        best=score;selected=candidate;selected.blast=blast;m_angles=angles;
    }
    if(best<0) return false;
    m_baseline.clear();m_firstSeen.clear();
    for(auto* entity:H::Entities.GetGroup(EntityEnum::LocalStickies)) m_baseline.insert(entity->GetRefEHandle().ToInt());
    m_mode=Mode.Value;m_plan=selected;m_plan.wanted=std::clamp(Stickies.Value,1,2);m_plan.valid=true;m_started=Time(local);m_lastRelease=m_started;
    m_lastShotCheck=m_started;m_aimOrigin=local->GetShootPos();
    Stage(Phase::Shoot,m_started);return true;
}
bool CAutoStickyJump::Rotate(CUserCmd* cmd,const Vec3& target)
{
    const bool silent=Rotation.Value==RotationEnum::Silent||Rotation.Value==RotationEnum::SilentLegit;
    const bool legit=Rotation.Value==RotationEnum::Legit||Rotation.Value==RotationEnum::SilentLegit;
    if(!m_rotationValid||m_rotationMode!=Rotation.Value) {m_rotationMode=Rotation.Value;m_rotation=cmd->viewangles;m_pitchSpeed=m_yawSpeed=0;m_rotationValid=true;}
    Vec3 angles=target;
    if(legit)
    {
        const Vec3 from=silent?m_rotation:cmd->viewangles;
        angles.x=Trolldier::SmoothRotation(from.x,target.x,TICK_INTERVAL,RotationSpeed.Value,true,m_pitchSpeed);
        angles.y=Trolldier::SmoothRotation(from.y,target.y,TICK_INTERVAL,RotationSpeed.Value,false,m_yawSpeed);
    }
    Math::ClampAngles(angles);cmd->viewangles=m_rotation=angles;
    if(silent) G::SilentAngles=true;else I::EngineClient->SetViewAngles(angles);
    return std::abs(angles.x-target.x)<.6f&&std::abs(Trolldier::WrapAngle(angles.y-target.y))<.6f;
}
void CAutoStickyJump::Prepare(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd)
{
    const bool previousControl=Controls(cmd);
    m_controlCommand=-1;
    if(!Enabled.Value||!local||!weapon||!cmd||!I::GlobalVars||!local->IsAlive()||local->IsAGhost()
        ||local->m_iClass()!=TF_CLASS_DEMOMAN||weapon->GetWeaponID()!=TF_WEAPON_PIPEBOMBLAUNCHER
        ||!local->CanAttack()||local->IsSwimming()||local->IsTaunting()||local->m_MoveType()!=MOVETYPE_WALK
        ||F::Menu.m_bIsOpen||I::EngineVGui->IsGameUIVisible()||I::EngineClient->IsPlayingDemo()||I::ClientState->IsPaused()||cmd->weaponselect
        ||(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2))) {Reset();return;}
    if(m_owner.Get()!=local||m_weapon.Get()!=weapon||m_spawn!=local->m_iSpawnCounter())
    {Reset();m_owner.Set(local);m_weapon.Set(weapon);m_spawn=local->m_iSpawnCounter();}
    if(m_lastCommand==cmd->command_number)
    {if(previousControl) m_controlCommand=cmd->command_number;Finalize(local,cmd);return;}
    if(cmd->command_number<m_lastCommand) {Reset();return;}
    m_lastCommand=cmd->command_number;
    const float now=Time(local);const int wanted=std::clamp(Stickies.Value,1,2);
    if(m_plan.valid&&(m_mode!=Mode.Value||wanted!=m_plan.wanted))
    {Stage(Phase::Failed,now);Publish(0,"Settings changed - toggle to replan");return;}
    if(m_phase==Phase::Done||m_phase==Phase::Failed) {Publish();return;}
    if(m_phase==Phase::Air)
    {
        if(!local->IsOnGround()) m_airborne=true;
        if(m_airborne&&local->IsOnGround())
        {
            if(Repeat.Value) {Reset();return;}Stage(Phase::Done,now);
        }
        if(now-m_phaseTime>12.f) Stage(Phase::Done,now);
        Publish();return;
    }
    m_controlCommand=cmd->command_number;m_setButtons=0;m_clearButtons=IN_ATTACK|IN_ATTACK2|IN_RELOAD|IN_JUMP;
    Vec3 inputForward,inputRight;Math::AngleVectors({0,G::OriginalCmd.viewangles.y,0},&inputForward,&inputRight);
    m_worldMove=inputForward*G::OriginalCmd.forwardmove+inputRight*G::OriginalCmd.sidemove;
    if(AutoWalk.Value&&m_plan.valid) m_worldMove=m_plan.wish*450.f;
    const auto& projectiles=H::Entities.GetGroup(EntityEnum::LocalStickies);
    if(!m_flushDone)
    {
        if(!FlushFirst.Value) m_flushDone=true;
        else if(!Flush(local,weapon,cmd)) {Finalize(local,cmd);return;}
    }
    if(m_phase==Phase::Plan)
    {
        if(!local->IsOnGround()||local->IsDucking()) {m_controlCommand=-1;Publish(0,"Stand on the ground to plan");return;}
        if(weapon->m_iClip1()>=0&&weapon->m_iClip1()<wanted)
        {
            if(local->GetAmmoCount(weapon->m_iPrimaryAmmoType())<weapon->GetAmmoPerShot())
            {m_controlCommand=-1;Publish(0,"Not enough sticky ammunition");return;}
            m_setButtons|=IN_RELOAD;Publish(0,"Loading the required sticky count");Finalize(local,cmd);return;
        }
        if(PreserveTraps.Value&&!projectiles.empty()) {m_controlCommand=-1;Publish(0,"Existing stickies - clear traps first");return;}
        if(now-m_lastSearch>=.15f) {m_lastSearch=now;Plan(local,weapon,cmd);}
        if(m_phase==Phase::Plan) {Publish(0,"No clear launch route / shot");Finalize(local,cmd);return;}
    }
    if(now<m_started||now-m_started>10.f) {Stage(Phase::Failed,now);Publish(0,"Timed out - toggle to retry");Finalize(local,cmd);return;}
    int seen=0;bool settled=true,armed=true,clear=true;float remaining=0,minRadius=FLT_MAX;Vec3 centre;
    const float duration=ArmTime(local,weapon);
    m_diagnostics.rawSpeed=0;
    for(auto* entity:projectiles)
    {
        auto* sticky=entity->As<CTFGrenadePipebombProjectile>();
        if(sticky->m_hOriginalLauncher().Get()!=weapon||m_baseline.contains(entity->GetRefEHandle().ToInt())) continue;
        if(sticky->m_iType()!=TF_GL_MODE_REMOTE_DETONATE) continue;
        ++seen;centre+=sticky->m_vecOrigin();
        // The authoritative attachment flag freezes VPhysics. Network velocity
        // can retain the old launch value and must not block a stuck bomb.
        settled&=sticky->m_bTouched();
        m_diagnostics.rawSpeed=(std::max)(m_diagnostics.rawSpeed,sticky->GetAbsVelocity().Length());
        m_diagnostics.rawCreation=sticky->m_flCreationTime();
        const float created=CreationTime(sticky,now);
        armed&=StickyJump::Armed(now,created,duration,TICK_INTERVAL);
        remaining=(std::max)(remaining,created+duration+TICK_INTERVAL-now);
        minRadius=(std::min)(minRadius,sticky->GetDamageRadius());
        const float radius=sticky->GetDamageRadius()-8.f;
        Vec3 nearest;
        reinterpret_cast<CCollisionProperty*>(local->GetCollideable())->CalcNearestPoint(sticky->m_vecOrigin(),&nearest);
        // Match AutoDetonate's proven collision trace and hull-distance check.
        Vec3 target=local->GetShootPos();
        if(sticky->m_vecOrigin().DistTo(target)>radius) target=nearest;
        clear&=sticky->m_vecOrigin().DistTo(nearest)<radius
            &&SDK::VisPosCollideable(sticky,local,sticky->m_vecOrigin(),target,MASK_SOLID&~CONTENTS_GRATE);
    }
    m_plan.count=seen;m_diagnostics.attached=settled;m_diagnostics.armed=armed;m_diagnostics.clear=clear;
    if(seen) centre/=float(seen);
    if(m_phase==Phase::Wait&&seen==wanted&&(local->m_vecOrigin()-centre).Dot(m_plan.wish)>minRadius+32.f)
    {Stage(Phase::Failed,now);Publish(remaining,"No confirmed blast corridor ahead");Finalize(local,cmd);return;}
    if(m_phase==Phase::Shoot)
    {
        if(m_released>=wanted) Stage(Phase::Wait,now);
        else
        {
            Vec3 aim=m_angles;
            const bool releasing=m_chargeCommand>=0&&cmd->command_number>m_chargeCommand
                &&weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime()>0;
            const bool validate=releasing||now-m_lastShotCheck>=.1f;
            if(validate||m_aimOrigin.DistToSqr(local->GetShootPos())>1.f)
            {
                if(!Shot(local,weapon,m_plan.blast,aim,validate?&m_plan:nullptr,validate)) {Stage(Phase::Failed,now);Publish(0,"Sticky shot became obstructed");Finalize(local,cmd);return;}
                if(validate) m_lastShotCheck=now;m_aimOrigin=local->GetShootPos();m_angles=aim;
            }
            const bool aligned=Rotate(cmd,aim);
            if(m_chargeCommand>=0)
            {
                // One command presses attack; a subsequent command releases it.
                if(StickyJump::ReleaseCharge(cmd->command_number,m_chargeCommand,weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(),aligned))
                {
                    ++m_released;m_chargeCommand=-1;m_lastRelease=now;m_shotCommand=cmd->command_number;G::Attacking=1;
                    m_fire.Release(now,weapon->GetFireRate(),weapon->m_iClip1());
                    if(wanted==2&&Doubletap.Value&&!m_burstTried)
                    {
                        m_burstTried=true;
                        const int ticks=StickyJump::BurstTicks(weapon->GetFireRate(),TICK_INTERVAL,F::Ticks.AvailableStickyJumpTicks());
                        if(ticks&&F::Ticks.BeginStickyJumpBurst(ticks)) m_plan.burst=ticks;
                    }
                    if(m_released>=wanted) Stage(Phase::Wait,now);
                }
                else if(now-m_lastRelease>2.f) Stage(Phase::Failed,now);
                else m_setButtons|=IN_ATTACK;
            }
            else if(aligned&&m_fire.Cleared(now,weapon->m_flNextPrimaryAttack(),weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(),weapon->m_iClip1(),seen,m_released,TICK_INTERVAL)
                &&m_fire.Ready(now,weapon->m_flNextPrimaryAttack(),weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(),TICK_INTERVAL)
                &&G::CanPrimaryAttack&&weapon->HasPrimaryAmmoForShot())
            {m_setButtons|=IN_ATTACK;m_chargeCommand=cmd->command_number;m_lastRelease=now;}
            else if(!weapon->HasPrimaryAmmoForShot())
            {
                if(local->GetAmmoCount(weapon->m_iPrimaryAmmoType())>=weapon->GetAmmoPerShot()) m_setButtons|=IN_RELOAD;
                else Stage(Phase::Failed,now);
            }
        }
    }
    if(m_phase==Phase::Wait)
    {
        if(seen==wanted&&settled&&!F::Ticks.m_bShifting)
        {
            // Lock to observed sticky positions; do not jump at a purely simulated blast.
            const Vec3 adjustment=centre-m_plan.blast;
            if(adjustment.Length2D()>48.f||std::abs(adjustment.z)>24.f) Stage(Phase::Failed,now);
            else {m_plan.takeoff+=Vec3(adjustment.x,adjustment.y,0);m_plan.blast=centre;Stage(Phase::Approach,now);}
        }
        else if(now-m_lastRelease>3.f) Stage(Phase::Failed,now);
    }
    if(AutoWalk.Value&&m_plan.valid) m_worldMove=m_plan.wish*450.f;
    if(m_phase==Phase::Approach)
    {
        if(seen!=wanted||!settled) Stage(Phase::Failed,now);
        else
        {
            const Vec3 velocity=local->m_vecVelocity();
            const Vec3 travel{velocity.x,velocity.y,0};
            const Vec3 origin=local->m_vecOrigin(),mins=local->m_vecMins();
            const Vec3 maxs=I::GameMovement->GetPlayerMaxs(true)*local->m_flModelScale();
            const float gravity=SDK::GetGravity();
            const float jumpSpeed=std::sqrt((std::max)(0.f,gravity)*90.f);
            const auto window=StickyJump::FindWindow(TICK_INTERVAL,remaining,[&](float time,float delay) {
                const Vec3 at=origin+travel*(time+delay)+Vec3(0,0,jumpSpeed*delay-.5f*gravity*delay*delay);
                float score=0;
                for(auto* entity:projectiles)
                {
                    auto* sticky=entity->As<CTFGrenadePipebombProjectile>();
                    if(sticky->m_hOriginalLauncher().Get()!=weapon||m_baseline.contains(entity->GetRefEHandle().ToInt())) continue;
                    const Vec3 blast=sticky->m_vecOrigin();
                    const Vec3 nearest=at+Vec3(std::clamp(blast.x-at.x,mins.x,maxs.x),std::clamp(blast.y-at.y,mins.y,maxs.y),std::clamp(blast.z-at.z,mins.z,maxs.z));
                    const Vec3 force=at+(mins+maxs)*.5f+Vec3(0,0,10)-blast;
                    const float value=RocketJump::BoostScore(Mode.Value==ModeEnum::Vertical?RocketJump::Mode::Vertical:RocketJump::Mode::Horizontal,
                        force.x,force.y,force.z,nearest.DistTo(blast),sticky->GetDamageRadius()-8.f,m_plan.wish.x,m_plan.wish.y);
                    if(value<=0) return 0.f;
                    score+=value;
                }
                return score;
            });
            m_diagnostics.current=window.current;m_diagnostics.best=window.score;m_diagnostics.ahead=window.time;
            // A stationary manual approach must not turn a weak distant blast
            // into the optimum merely because every predicted position is identical.
            const bool useful=travel.Length2DSqr()>1.f||window.score>=wanted*(Mode.Value==ModeEnum::Vertical?.65f:.55f);
            if(window.valid&&useful)
            {
                m_plan.takeoff=origin+travel*window.time;m_reason=nullptr;
                // Validate the selected route, not proximity to the old marker.
                if(!Route(local,m_plan.takeoff,&m_plan)) {Stage(Phase::Failed,now);Publish(remaining,"Launch route blocked");Finalize(local,cmd);return;}
            }
            else if((origin-centre).Dot(m_plan.wish)>minRadius+32.f)
            {Stage(Phase::Failed,now);Publish(remaining,"No armed blast corridor ahead");Finalize(local,cmd);return;}
            const bool dot=weapon->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT;
            const bool aimReady=!dot||Rotate(cmd,Math::CalcAngle(local->GetShootPos(),centre));
            // Pipebomb detonation is available in ItemBusyFrame while reloading.
            // Generic CanSecondaryAttack also tests a busy-frame clock and can
            // be false here even though the actual detonator is usable.
            if(aimReady&&clear&&local->IsOnGround()&&!F::Ticks.m_bShifting&&useful&&window.Now(TICK_INTERVAL))
            {m_setButtons|=IN_JUMP|IN_DUCK;m_jumpCommand=G::OriginalCmd.command_number;Stage(Phase::Jump,now);}
        }
    }
    if(m_phase==Phase::Jump)
    {
        m_setButtons|=IN_DUCK;
        if(weapon->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT)
            Rotate(cmd,Math::CalcAngle(local->GetShootPos(),centre));
        if(cmd->command_number>m_jumpCommand&&!local->IsOnGround()&&local->m_vecVelocity().z>0&&armed&&clear&&seen==wanted)
        {
            const auto score=[&](int tick) {
                const float time=TICKS_TO_TIME(tick);
                Vec3 origin=local->m_vecOrigin()+local->m_vecVelocity()*time;
                origin.z-=SDK::GetGravity()*time*time*.5f;
                const Vec3 mins=local->m_vecMins(),maxs=I::GameMovement->GetPlayerMaxs(true)*local->m_flModelScale();
                const Vec3 nearest=origin+Vec3(std::clamp(centre.x-origin.x,mins.x,maxs.x),std::clamp(centre.y-origin.y,mins.y,maxs.y),std::clamp(centre.z-origin.z,mins.z,maxs.z));
                const Vec3 force=origin+(mins+maxs)*.5f+Vec3(0,0,10)-centre;
                return RocketJump::BoostScore(Mode.Value==ModeEnum::Vertical?RocketJump::Mode::Vertical:RocketJump::Mode::Horizontal,
                    force.x,force.y,force.z,nearest.DistTo(centre),minRadius,m_plan.wish.x,m_plan.wish.y);
            };
            const float current=score(0);float future=current;
            for(int tick=1;tick<=3;++tick) future=(std::max)(future,score(tick));
            if(StickyJump::DetonateWindow(current,future,cmd->command_number-m_jumpCommand))
            {m_setButtons|=IN_ATTACK2;m_beforeBlastVelocity=local->m_vecVelocity();Stage(Phase::Blast,now);}
        }
        else if(now-m_phaseTime>.3f) Stage(Phase::Failed,now);
    }
    if(m_phase==Phase::Blast)
    {
        m_setButtons|=IN_DUCK;
        if(!seen)
        {
            if(local->InCond(TF_COND_BLASTJUMPING)||(local->m_vecVelocity()-m_beforeBlastVelocity).LengthSqr()>1600.f)
            {m_airborne=!local->IsOnGround();Stage(Phase::Air,now);}
            else if(now-m_phaseTime>.3f) Stage(Phase::Failed,now);
        }
        else if(now-m_phaseTime>.75f) Stage(Phase::Failed,now);
        else
        {
            if(weapon->As<CTFPipebombLauncher>()->GetDetonateType()==TF_DETONATE_MODE_DOT)
                Rotate(cmd,Math::CalcAngle(local->GetShootPos(),centre));
            m_setButtons|=IN_ATTACK2;
        }
    }
    if(AutoWalk.Value&&m_plan.valid) m_worldMove=m_plan.wish*450.f;
    Publish(remaining);Finalize(local,cmd);
}
void CAutoStickyJump::Finalize(CTFPlayer* local,CUserCmd* cmd)
{
    if(!Controls(cmd)) return;
    cmd->buttons=(cmd->buttons&~m_clearButtons)|m_setButtons;
    // Keep explicit shoot/detonate rotations after late movement/anti-aim helpers.
    if(m_rotationValid) {cmd->viewangles=m_rotation;if(Rotation.Value==RotationEnum::Silent||Rotation.Value==RotationEnum::SilentLegit) G::SilentAngles=true;else I::EngineClient->SetViewAngles(m_rotation);}
    {
        Vec3 forward,right;Math::AngleVectors({0,cmd->viewangles.y,0},&forward,&right);
        cmd->forwardmove=std::clamp(m_worldMove.Dot(forward),-450.f,450.f);
        cmd->sidemove=std::clamp(m_worldMove.Dot(right),-450.f,450.f);
    }

}

void CAutoStickyJump::AfterNoSpread(CUserCmd* cmd)
{
    if(Controls(cmd)&&StickyJump::OwnsCommand(m_shotCommand,G::OriginalCmd.command_number)) m_rotation=cmd->viewangles;
}
