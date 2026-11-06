#include "SpyKnife.h"
#include "../Aimbot.h"
#include "../AutoTrolldier/AutoTrolldier.h"
#include "../../Ticks/Ticks.h"
#include "../../Backtrack/Backtrack.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"

namespace
{
    SpyKnife::V Point(const Vec3& v) {return {v.x,v.y,v.z};}
    Vec3 Native(SpyKnife::V v) {return {v.x,v.y,v.z};}
    Vec3 TargetOrigin(CTFPlayer* target)
    {
        if(Vars::Aimbot::Melee::SwingAccountPing.Value)
            if(const auto* sample=target->m_iv_vecOrigin().m_LastNetworkedValue) return *sample;
        return target->m_vecOrigin();
    }
    bool Finite(const Vec3& v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    struct TargetScope
    {
        CTFPlayer* target;Vec3 origin,mins,maxs;
        explicit TargetScope(CTFPlayer* t):target(t),origin(t->GetAbsOrigin()),mins(t->m_vecMins()),maxs(t->m_vecMaxs()) {}
        ~TargetScope() {target->SetAbsOrigin(origin);target->m_vecMins()=mins;target->m_vecMaxs()=maxs;}
    };
    // A native movement probe must preserve the active EnginePrediction scope.
    // Full snapshots are private to this scoped probe; MoveSim's global snapshot
    // policy remains the upstream networked-only policy restored previously.
    struct MovementScope
    {
        CTFPlayer* player;MoveStorage storage{};byte* saved;datamap_t* map;
        CGlobalVarsBase clock;CUserCmd* command;CUserCmd dummy;
        CBasePlayer* movementPlayer;CTFPlayer* tfPlayer;CMoveData* move;
        bool inPrediction,first,savedState=false,initialized=false;
        Vec3 absolute;
        MovementScope(CTFPlayer* p,byte* data,size_t capacity,byte* scratch,size_t scratchSize,bool strafe=false,const Vec3* origin=nullptr):
            player(p),saved(data),map(p->GetPredDescMap()),clock(*I::GlobalVars),command(p->m_pCurrentCommand()),dummy(G::DummyCmd),
            movementPlayer(I::GameMovement->player),tfPlayer(I::GameMovement->m_pTFPlayer),move(I::GameMovement->mv),
            inPrediction(I::Prediction->m_bInPrediction),first(I::Prediction->m_bFirstTimePredicted),absolute(p->GetAbsOrigin())
        {
            if(!map) return;
            const auto size=p->GetIntermediateDataSize();
            if(size<=0||size>capacity||size>scratchSize) return;
            CPredictionCopy copy={PC_EVERYTHING,saved,PC_DATA_PACKED,p,PC_DATA_NORMAL};
            copy.TransferData("SpyKnifeProbeSave",p->entindex(),map);savedState=true;
            storage.m_pExternalData=scratch;storage.m_nExternalBytes=scratchSize;storage.m_bStorePath=false;
            const Vec3 velocity=p->m_vecVelocity();
            initialized=F::MoveSim.Initialize(p,storage,false,strafe,false,origin)&&!storage.m_bFailed;
            if(initialized&&!strafe) storage.m_MoveData.m_vecVelocity=p->m_vecVelocity()=velocity;
        }
        ~MovementScope()
        {
            if(storage.m_pData) F::MoveSim.Restore(storage);
            if(savedState)
            {
                CPredictionCopy copy={PC_EVERYTHING,player,PC_DATA_NORMAL,saved,PC_DATA_PACKED};
                copy.TransferData("SpyKnifeProbeRestore",player->entindex(),map);
                player->SetAbsOrigin(absolute);
            }
            player->m_pCurrentCommand()=command;G::DummyCmd=dummy;
            *I::GlobalVars=clock;I::Prediction->m_bInPrediction=inPrediction;I::Prediction->m_bFirstTimePredicted=first;
            I::GameMovement->player=movementPlayer;I::GameMovement->m_pTFPlayer=tfPlayer;I::GameMovement->mv=move;
        }
    };
}

bool CSpyKnife::Eligible(CTFPlayer* local,CTFWeaponBase* weapon) const
{
    return local&&weapon&&local->IsAlive()&&!local->IsAGhost()&&local->m_iClass()==TF_CLASS_SPY
        &&weapon->GetWeaponID()==TF_WEAPON_KNIFE&&weapon->As<CTFKnife>()->m_bKnifeExists()
        &&Vars::Aimbot::General::AimType.Value&&Vars::Aimbot::General::AutoShoot.Value
        &&Vars::Aimbot::Melee::KnifePrecision.Value&&Vars::Aimbot::Melee::AutoBackstab.Value
        &&(Vars::Aimbot::General::Target.Value&Vars::Aimbot::General::TargetEnum::Players)
        &&!local->IsTaunting()&&!local->IsSwimming()&&local->m_MoveType()==MOVETYPE_WALK
        &&!I::ClientState->IsPaused()&&!I::EngineVGui->IsGameUIVisible()&&!G::Unload;
}
bool CSpyKnife::ValidTarget(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target) const
{
    if(!target||!target->IsAlive()||target->IsDormant()||target->IsAGhost()||target->m_iTeamNum()==local->m_iTeamNum()
        ||F::AimbotGlobal.ShouldIgnore(target,local,weapon)||SDK::AttribHookValue(0,"cannot_be_backstabbed",target)) return false;
    if(Vars::Aimbot::Melee::IgnoreRazorback.Value)
    {
        CBaseEntity* items[64]{};CUtlVector<CBaseEntity*> sources(items,64);
        if(SDK::AttribHookValue(0,"set_blockbackstab_once",target,&sources))
            for(int i=0;i<sources.Count();++i) if(sources[i]&&sources[i]->ShouldDraw()) return false;
    }
    return true;
}
void CSpyKnife::Reset(bool clearTransport)
{
    if(m_active) F::Ticks.StopKnifeWarp();
    if(clearTransport) {m_warpBarrier=-1;m_rejectedCommand=-1;m_display.Set({});}
    m_active=m_settling=m_trick=false;m_count=0;m_start=m_prepared=m_holdCommand=m_nextCommand=-1;
    m_owner.Term();m_target.Term();m_weapon.Term();m_worldMove={};
}

CSpyKnife::Observation* CSpyKnife::Observe(CTFPlayer* target)
{
    const int index=target->entindex();if(index<=0||index>=int(m_observations.size())) return nullptr;
    auto& o=m_observations[index];const float simulation=target->m_flSimulationTime(),yaw=target->GetEyeAngles().y;
    if(o.target.Get()!=target||o.spawn!=target->m_iSpawnCounter()||simulation<o.simulation)
    {o={};o.target.Set(target);o.spawn=target->m_iSpawnCounter();o.simulation=simulation;o.yaw=yaw;o.seen=GetTickCount64();}
    if(simulation>o.simulation)
    {
        const float delta=Math::NormalizeAngle(yaw-o.yaw),span=simulation-o.simulation;
        const float rate=delta/span;
        if(!std::isfinite(rate)||std::abs(delta)>50.f||std::abs(rate)>900.f) {o.unpredictable=true;o.stable=0;o.rate=0;}
        else {o.rate=std::clamp(rate,-360.f,360.f);if(++o.stable>=3) o.unpredictable=false;}
        o.simulation=simulation;o.yaw=yaw;o.seen=GetTickCount64();
    }
    return &o;
}
bool CSpyKnife::Reliable(CTFPlayer* target)
{
    auto* o=Observe(target);
    return o&&!o->unpredictable&&GetTickCount64()-o->seen<=350&&std::isfinite(o->yaw);
}
bool CSpyKnife::Contact(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target,const Vec3& origin,
    const Vec3& currentAngles,Vec3& angles,Vec3& point,bool planning,const Vec3* predictedOwner,bool fixedAngles,float yawOffset,float reachReserve)
{
    auto reject=[&](Status reason) {if(!planning) m_status=reason;return false;};
    if(!Reliable(target)) return reject(Status::Uncertain);
    const float safety=SpyKnife::Safety(Vars::Aimbot::Melee::BackstabSafety.Value);
    const float scale=(std::max)(1.f,local->m_flModelScale());
    // Inset the actual contact, rather than trusting a grazing full-size hull.
    const float range=SDK::AttribHookValue(weapon->GetSwingRange(),"melee_range_multiplier",weapon)*scale-(2.f+4.f*safety)-std::clamp(reachReserve,0.f,24.f);
    const float hull=(std::max)(0.f,SDK::AttribHookValue(18.f,"melee_bounds_multiplier",weapon)*scale-(1.f+2.f*safety));
    if(!Finite(origin)||!std::isfinite(range)||!std::isfinite(hull)||range<=0) return reject(Status::Range);
    const Vec3 eye=local->GetShootPos()+(predictedOwner?*predictedOwner-local->m_vecOrigin():Vec3{});
    const Vec3 mins=origin+target->m_vecMins(),maxs=origin+target->m_vecMaxs();
    if(!Finite(eye)||!Finite(mins)||!Finite(maxs)||mins.x+.2f>=maxs.x||mins.y+.2f>=maxs.y||mins.z+.2f>=maxs.z) return reject(Status::Range);
    const Vec3 nearest=eye.Clamp(mins+Vec3::Get(.1f),maxs-Vec3::Get(.1f));
    if(eye.DistToSqr(nearest)>(range+hull)*(range+hull)) return reject(Status::Range);
    TargetScope restore(target);target->SetAbsOrigin(origin);
    const Vec3 ownerCenter=(predictedOwner?*predictedOwner:local->GetAbsOrigin())+(local->m_vecMins()+local->m_vecMaxs())*.5f;
    const Vec3 centre=(mins+maxs)*.5f;
    Vec3 victimAngles=target->GetEyeAngles();victimAngles.y+=yawOffset;
    Vec3 victimForward;Math::AngleVectors(victimAngles,&victimForward);
    const float uncertainty=std::abs(Observe(target)->rate)*.03f;
    const Vec3 points[]={nearest,{centre.x,centre.y,std::clamp(eye.z,mins.z+.1f,maxs.z-.1f)},centre};
    const int mode=Vars::Aimbot::General::AimType.Value;
    bool geometry=false;
    for(const auto& candidate:points)
    {
        Vec3 aim=Math::CalcAngle(eye,candidate);Math::ClampAngles(aim);
        if(fixedAngles||(!planning&&(mode==Vars::Aimbot::General::AimTypeEnum::Legit||mode==Vars::Aimbot::General::AimTypeEnum::Assistive))) aim=currentAngles;
        else if(!planning&&(mode==Vars::Aimbot::General::AimTypeEnum::Smooth||mode==Vars::Aimbot::General::AimTypeEnum::SmoothVelocity))
            aim=currentAngles.LerpAngle(aim,F::Aimbot.GetSmoothStrength(currentAngles,aim));
        if(!planning&&!F::AimbotGlobal.ShouldAimAtAngle(aim)) continue;
        Vec3 forward;Math::AngleVectors(aim,&forward);
        if(!SpyKnife::SafeGeometry(Point(centre-ownerCenter),Point(forward),Point(victimForward),safety,uncertainty)) continue;
        geometry=true;
        CTraceFilterHitscan filter(local);CGameTrace hit={};const Vec3 end=eye+forward*range;
        auto trace=[&](float extent) {
            SDK::TraceHull(eye,end,Vec3::Get(-extent),Vec3::Get(extent),MASK_SOLID,&filter,&hit);
            if(hit.m_pEnt==target||hit.startsolid||hit.allsolid) return;
            Ray_t ray;ray.Init(eye,end,Vec3::Get(-extent),Vec3::Get(extent));
            CGameTrace targetHit={};I::EngineTrace->ClipRayToCollideable(ray,MASK_SOLID,target->m_Collision(),&targetHit);
            if(targetHit.DidHit()&&targetHit.m_pEnt==target&&targetHit.fraction<hit.fraction) hit=targetHit;
        };
        trace(0);if(hit.fraction>=1.f&&!hit.startsolid&&!hit.allsolid) trace(hull);
        if(hit.m_pEnt==target&&!hit.startsolid&&!hit.allsolid)
        {angles=aim;point=hit.endpos;if(!planning)m_status=Status::Ready;return true;}
        if(fixedAngles) break;
    }
    return reject(geometry?Status::Blocked:Status::Front);
}

bool CSpyKnife::PlanWarp(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target,CUserCmd* command)
{
    if(!Vars::Aimbot::Melee::WarpBehind.Value||Active()||!G::CanPrimaryAttack||!local->IsOnGround()||local->IsDucking()
        ||!target->IsOnGround()||F::AntiCheatCompatibility.Active()||F::Ticks.m_bWarp||F::Ticks.m_bDoubletap||F::Ticks.m_bRecharge
        ||F::AutoTrolldier.IsActive(command)||I::EngineClient->IsPlayingDemo()) return false;
    const int mode=Vars::Aimbot::General::AimType.Value;
    if(mode!=Vars::Aimbot::General::AimTypeEnum::Silent&&mode!=Vars::Aimbot::General::AimTypeEnum::Plain
        &&mode!=Vars::Aimbot::General::AimTypeEnum::Locking) return false;
    const int ticks=F::Ticks.AvailableKnifeWarpTicks();
    if(ticks<1||m_searchFrame==I::GlobalVars->framecount) return false;
    const Vec3 targetOrigin=TargetOrigin(target),localOrigin=local->m_vecOrigin();
    if(!std::isfinite(Vars::Aimbot::Melee::WarpBehindDistance.Value)) return false;
    const float distance=std::clamp(Vars::Aimbot::Melee::WarpBehindDistance.Value,48.f,256.f);
    if(!Finite(targetOrigin)||localOrigin.DistToSqr(targetOrigin)>distance*distance) return false;
    m_searchFrame=I::GlobalVars->framecount;
    Vec3 victimForward;Math::AngleVectors(target->GetEyeAngles(),&victimForward);victimForward.Normalize2D();
    const float radius=SpyKnife::RearRadius(Point(local->m_vecMaxs()),Point(target->m_vecMaxs()),Point(victimForward));
    const float safety=SpyKnife::Safety(Vars::Aimbot::Melee::BackstabSafety.Value);
    const float reserve=SpyKnife::ArrivalReserve(safety);
    const int arrival=SpyKnife::ArrivalWindow(F::Backtrack.GetReal(),TICK_INTERVAL);
    if(!arrival) return false;
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    const auto deadline=now.QuadPart+(std::max)(frequency.QuadPart*3/1000,int64_t(1));
    // Forecast the acknowledgement window once, independently of warp length.
    // Probe before the local scope; never nest MoveSim's shared movement state.
    {
        MovementScope forecast(target,m_saved.data(),m_saved.size(),m_moveScratch.data(),m_moveScratch.size(),true,&targetOrigin);
        if(!forecast.initialized) return false;
        m_forecast[0]=targetOrigin;
        for(int tick=1;tick<=arrival;++tick)
        {
            QueryPerformanceCounter(&now);if(now.QuadPart>=deadline) return false;
            F::MoveSim.RunTick(forecast.storage,false);
            if(forecast.storage.m_bFailed||!target->IsOnGround()) return false;
            m_forecast[tick]=forecast.storage.m_MoveData.m_vecAbsOrigin;
            if(!Finite(m_forecast[tick])) return false;
        }
    }
    const float yawRate=Observe(target)->rate;
    Vec3 arrivalForward;
    const float arrivalYaw=std::clamp(yawRate*TICKS_TO_TIME(arrival),-45.f,45.f);
    Math::AngleVectors(target->GetEyeAngles()+Vec3(0,arrivalYaw,0),&arrivalForward);
    for(int attempt=0;attempt<2;++attempt)
    {
        QueryPerformanceCounter(&now);if(now.QuadPart>=deadline) break;
        const int route=m_route++%SpyKnife::Routes;
        int found=0;
        {
            TargetScope targetRestore(target);target->SetAbsOrigin(targetOrigin);
            MovementScope probe(local,m_saved.data(),m_saved.size(),m_moveScratch.data(),m_moveScratch.size());
            if(!probe.initialized) continue;
            m_positions[0]=localOrigin;
            for(int tick=1;tick<=ticks;++tick)
            {
                QueryPerformanceCounter(&now);if(now.QuadPart>=deadline) break;
                const Vec3 wish=Native(SpyKnife::RouteWish(Point(probe.storage.m_MoveData.m_vecAbsOrigin),Point(m_forecast[arrival]),Point(arrivalForward),radius,route));
                auto& move=probe.storage.m_MoveData;
                move.m_vecViewAngles=command->viewangles;move.m_vecViewAngles.x=move.m_vecViewAngles.z=0;
                const float yaw=Math::Deg2Rad(move.m_vecViewAngles.y);
                move.m_flForwardMove=wish.x*std::cos(yaw)+wish.y*std::sin(yaw);
                move.m_flSideMove=wish.x*std::sin(yaw)-wish.y*std::cos(yaw);move.m_nButtons=0;
                F::MoveSim.RunTick(probe.storage,false);
                if(probe.storage.m_bFailed||!local->IsOnGround()) break;
                m_moves[tick]=wish;m_positions[tick]=move.m_vecAbsOrigin;
                Vec3 angles,point;
                // First contact is insufficient: require inset reach through the
                // sampled acknowledgement window even if the owner cannot advance.
                const bool safe=SpyKnife::ArrivalContactWindow(arrival,[&](int sample) {
                    QueryPerformanceCounter(&now);if(now.QuadPart>=deadline) return false;
                    const float yawOffset=std::clamp(yawRate*TICKS_TO_TIME(sample),-45.f,45.f);
                    return Contact(local,weapon,target,m_forecast[sample],command->viewangles,angles,point,true,
                        &move.m_vecAbsOrigin,false,yawOffset,reserve);
                });
                if(safe) {found=tick;break;}
            }
        }
        if(found&&Reliable(target)&&F::Ticks.BeginKnifeWarp(found))
        {
            m_owner.Set(local);m_target.Set(target);m_weapon.Set(weapon);
            m_ownerSpawn=local->m_iSpawnCounter();m_targetSpawn=target->m_iSpawnCounter();
            m_targetOrigin=targetOrigin;m_targetAngles=target->GetEyeAngles();
            m_start=command->command_number;m_count=found;m_active=true;m_prepared=-1;m_status=Status::Warp;
            return true;
        }
    }
    return false;
}

void CSpyKnife::FinishApproach(CTFPlayer*,CUserCmd* command)
{
    F::Ticks.StopKnifeWarp();m_active=false;m_settling=true;m_trick=false;
    m_finish=command->command_number;
    m_expiry=m_finish+std::clamp(TIME_TO_TICKS(2.f*F::Backtrack.GetReal()+.2f),6,32);
    m_settleDeadline=GetTickCount64()+uint64_t(std::clamp(2000.f*F::Backtrack.GetReal()+200.f,150.f,800.f));
    m_holdCommand=command->command_number;m_status=Status::Settling;
}
void CSpyKnife::Prepare(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command)
{
    m_holdCommand=-1;m_attack={};
    if(!Active()) return;
    auto* target=m_target.Get();
    if(!Eligible(local,weapon)||local!=m_owner.Get()||weapon!=m_weapon.Get()||!ValidTarget(local,weapon,target)
        ||!Reliable(target)||command->weaponselect||!local->IsOnGround()||!G::CanPrimaryAttack
        ||local->m_iSpawnCounter()!=m_ownerSpawn||target->m_iSpawnCounter()!=m_targetSpawn
        ||F::AntiCheatCompatibility.Active()) {Reset();return;}
    const float safety=SpyKnife::Safety(Vars::Aimbot::Melee::BackstabSafety.Value);
    if(m_settling)
    {
        if(!Vars::Aimbot::Melee::WarpBehind.Value||command->command_number>m_expiry||GetTickCount64()>m_settleDeadline)
            {Reset();return;}
        // Keep pace with the victim while the movement packet is acknowledged.
        // Braking here used to discard the reach gained by the shifted commands.
        Vec3 forward;Math::AngleVectors(target->GetEyeAngles(),&forward);
        const Vec3 origin=TargetOrigin(target),velocity=target->m_vecVelocity();
        if(!Finite(origin)||!Finite(velocity)||!target->IsOnGround()) {Reset();return;}
        const float radius=SpyKnife::RearRadius(Point(local->m_vecMaxs()),Point(target->m_vecMaxs()),Point(forward));
        m_worldMove=Native(SpyKnife::ChaseWish(Point(local->m_vecOrigin()),Point(origin),Point(forward),Point(velocity),radius));
        m_prepared=command->command_number;ApplyMovement(local,weapon,command);return;
    }
    if(m_trick)
    {
        if(!Vars::Aimbot::Melee::AutoTrickstab.Value||m_nextCommand!=command->command_number
            ||local->m_vecOrigin().DistToSqr(m_nextOrigin)>8.f*8.f
            ||int64_t(command->command_number)-m_trickStart>SpyKnife::TrickTicks)
            {Reset();return;}
        m_worldMove=m_nextMove;m_prepared=command->command_number;
        ApplyMovement(local,weapon,command);return;
    }
    if(!Vars::Aimbot::Melee::WarpBehind.Value||!F::Ticks.KnifeWarpActive()) {Reset();return;}
    const int64_t offset=int64_t(command->command_number)-m_start;
    const int index=offset>0&&offset<=m_count?int(offset):-1;
    const int previous=m_prepared<0?m_start:m_prepared;
    const float error=index>0&&index<=m_count?local->m_vecOrigin().DistTo(m_positions[index-1]):INFINITY;
    const float yaw=Math::NormalizeAngle(target->GetEyeAngles().y-m_targetAngles.y);
    if(int64_t(command->command_number)!=int64_t(previous)+1
        ||!SpyKnife::Stable(m_start,command->command_number,m_count,m_ownerSpawn,local->m_iSpawnCounter(),
            m_targetSpawn,target->m_iSpawnCounter(),error,yaw)||!SpyKnife::CalibratedDrift(error,yaw,safety)
        ||TargetOrigin(target).DistToSqr(m_targetOrigin)>8.f*8.f) {Reset();return;}
    m_worldMove=m_moves[index];m_prepared=command->command_number;ApplyMovement(local,weapon,command);
}
void CSpyKnife::ApplyMovement(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command)
{
    if(m_prepared!=command->command_number) return;
    const bool held=command->command_number==m_holdCommand;
    if(!held&&(!Active()||local!=m_owner.Get()||weapon!=m_weapon.Get())) return;
    const float yaw=Math::Deg2Rad(command->viewangles.y);
    command->forwardmove=m_worldMove.x*std::cos(yaw)+m_worldMove.y*std::sin(yaw);
    command->sidemove=m_worldMove.x*std::sin(yaw)-m_worldMove.y*std::cos(yaw);
    command->upmove=0;command->buttons&=~(IN_JUMP|IN_DUCK|IN_SPEED|IN_WALK);
    if(!held) command->buttons&=~(IN_ATTACK|IN_ATTACK2);
}
void CSpyKnife::AfterAim(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command)
{
    if(!Active()) return;
    if((m_active&&command->command_number==m_start&&local->m_vecOrigin().DistToSqr(m_positions[0])>4.f*4.f)
        ||(m_trick&&command->command_number==m_trickPlanCommand&&local->m_vecOrigin().DistToSqr(m_trickPath[0])>4.f*4.f))
        {Reset();return;}
    if(m_active&&int64_t(command->command_number)-m_start>=m_count)
        {FinishApproach(local,command);return;}
    if(!Eligible(local,weapon)||G::Attacking==1)
    {
        const int prepared=m_prepared;const Vec3 move=m_worldMove;
        Reset();if(prepared==command->command_number) {m_holdCommand=prepared;m_prepared=prepared;m_worldMove=move;}
    }
}
bool CSpyKnife::PlanTrick(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target,CUserCmd* command)
{
    if(!Vars::Aimbot::Melee::AutoTrickstab.Value||m_active||m_settling||!G::CanPrimaryAttack
        ||F::Ticks.m_bWarp||F::Ticks.m_bDoubletap||F::Ticks.m_bRecharge||F::AntiCheatCompatibility.Active()
        ||!local->IsOnGround()||local->IsDucking()||!target->IsOnGround()||!Reliable(target)) return false;
    if(m_trickFrame==I::GlobalVars->framecount)
    {
        const int64_t at=int64_t(command->command_number)-m_trickPlanCommand;
        if(m_trick&&at>=0&&at+1<m_trickCount)
        {m_nextMove=m_trickMoves[size_t(at+1)];m_nextOrigin=m_trickPath[size_t(at)];m_nextCommand=command->command_number+1;return true;}
        return false;
    }
    const Vec3 origin=TargetOrigin(target);
    if(!Finite(origin)||local->m_vecOrigin().DistToSqr(origin)>180.f*180.f) return false;
    m_trickFrame=I::GlobalVars->framecount;
    if(!std::isfinite(Vars::Aimbot::Melee::TrickstabPrediction.Value)) return false;
    const int ticks=std::clamp(TIME_TO_TICKS(Vars::Aimbot::Melee::TrickstabPrediction.Value),8,SpyKnife::TrickTicks);
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    const auto deadline=now.QuadPart+(std::max)(frequency.QuadPart*3/1000,int64_t(1));
    // These are ordinary future game ticks. Unlike a shifted packet, target
    // movement advances here; the complete probe is restored before execution.
    {
        MovementScope forecast(target,m_saved.data(),m_saved.size(),m_moveScratch.data(),m_moveScratch.size(),true,&origin);
        if(!forecast.initialized) return false;
        m_forecast[0]=origin;
        for(int tick=1;tick<=ticks;++tick)
        {
            QueryPerformanceCounter(&now);if(now.QuadPart>=deadline) return false;
            F::MoveSim.RunTick(forecast.storage,false);
            if(forecast.storage.m_bFailed) return false;
            m_forecast[tick]=forecast.storage.m_MoveData.m_vecAbsOrigin;
            if(!Finite(m_forecast[tick])) return false;
        }
    }
    const int mode=Vars::Aimbot::Melee::TrickstabMode.Value;
    const float rate=Observe(target)->rate;
    const float radius=local->m_vecMaxs().To2D().Length()+target->m_vecMaxs().To2D().Length()+3.f;
    const int phase=m_trick?int(int64_t(command->command_number)-m_trickStart):0;
    int steps=ticks;
    for(int attempt=0;attempt<(m_trick?1:2);++attempt)
    {
        QueryPerformanceCounter(&now);if(now.QuadPart>=deadline) break;
        int route=m_trick?m_trickRoute:m_route++%SpyKnife::TrickRoutes;
        if(mode==Vars::Aimbot::Melee::TrickstabModeEnum::Sidestab) route=route%SpyKnife::Routes;
        else if(mode==Vars::Aimbot::Melee::TrickstabModeEnum::Matador) route=5+route%2;
        int found=0;
        {
            TargetScope restore(target);
            MovementScope probe(local,m_saved.data(),m_saved.size(),m_moveScratch.data(),m_moveScratch.size());
            if(!probe.initialized) continue;
            m_trickPath[0]=local->m_vecOrigin();
            for(int tick=1;tick<=ticks;++tick)
            {
                QueryPerformanceCounter(&now);if(now.QuadPart>=deadline||++steps>96) break;
                const float yawOffset=std::clamp(rate*TICKS_TO_TIME(tick),-45.f,45.f);
                Vec3 face;Math::AngleVectors(target->GetEyeAngles()+Vec3(0,yawOffset,0),&face);
                const Vec3 wish=Native(SpyKnife::TrickWish(Point(probe.storage.m_MoveData.m_vecAbsOrigin),Point(m_forecast[tick]),
                    Point(face),radius,route,phase+tick-1));
                target->SetAbsOrigin(m_forecast[tick]);
                auto& move=probe.storage.m_MoveData;move.m_vecViewAngles=command->viewangles;
                move.m_vecViewAngles.x=move.m_vecViewAngles.z=0;
                const float yaw=Math::Deg2Rad(move.m_vecViewAngles.y);
                move.m_flForwardMove=wish.x*std::cos(yaw)+wish.y*std::sin(yaw);
                move.m_flSideMove=wish.x*std::sin(yaw)-wish.y*std::cos(yaw);move.m_nButtons=0;
                F::MoveSim.RunTick(probe.storage,false);
                if(probe.storage.m_bFailed||!local->IsOnGround()||!Finite(move.m_vecAbsOrigin)) break;
                m_trickMoves[tick]=wish;m_trickPath[tick]=move.m_vecAbsOrigin;
                Vec3 aim,point;
                if(Contact(local,weapon,target,m_forecast[tick],command->viewangles,aim,point,true,&move.m_vecAbsOrigin,false,yawOffset))
                    {found=tick;break;}
            }
        }
        if(found)
        {
            if(!m_trick) m_trickStart=command->command_number;
            m_owner.Set(local);m_target.Set(target);m_weapon.Set(weapon);
            m_ownerSpawn=local->m_iSpawnCounter();m_targetSpawn=target->m_iSpawnCounter();
            m_trick=true;m_trickRoute=route;m_trickCount=found+1;m_trickPlanCommand=command->command_number;
            m_nextCommand=command->command_number+1;m_nextMove=m_trickMoves[1];m_nextOrigin=m_trickPath[0];
            m_status=route>=5&&phase<6?Status::Feint:Status::Side;
            return true;
        }
    }
    return false;
}

bool CSpyKnife::Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command,const std::vector<Target_t>& targets)
{
    if(!Eligible(local,weapon)) {if(Active()) Reset();return false;}
    m_status=G::CanPrimaryAttack?Status::NoTarget:Status::Cooldown;
    bool selected=false;
    for(const auto& candidate:targets)
    {
        if(!candidate.m_pEntity->IsPlayer()) continue;
        auto* victim=candidate.m_pEntity->As<CTFPlayer>();
        if(Active()&&victim!=m_target.Get()) continue;
        if(!ValidTarget(local,weapon,victim)) continue;
        selected=true;m_visualTarget.Set(victim);
        if(!m_active&&(F::Ticks.m_bWarp||F::Ticks.m_bShifting||m_warpBarrier>I::ClientState->last_command_ack))
            {command->buttons&=~IN_ATTACK;m_status=Status::Settling;return true;}
        // Approach packets never contain an attack. A fresh command after the
        // server acknowledges the movement is the only place a warp may stab.
        if(m_active) {command->buttons&=~IN_ATTACK;m_status=Status::Warp;return true;}
        if(m_settling&&!SpyKnife::AckReady(command->command_number,m_finish,I::ClientState->last_command_ack,m_expiry))
            {command->buttons&=~IN_ATTACK;m_status=Status::Settling;return true;}
        Vec3 angles,point;const Vec3 origin=TargetOrigin(victim);
        if(G::CanPrimaryAttack&&Contact(local,weapon,victim,origin,command->viewangles,angles,point))
        {
            command->buttons|=IN_ATTACK;G::Attacking=SDK::IsAttacking(local,weapon,command,true);
            if(G::Attacking==1)
            {
                m_attack={};m_attack.target.Set(victim);m_attack.weapon.Set(weapon);
                m_attack.command=command->command_number;m_attack.spawn=victim->m_iSpawnCounter();
                m_attack.origin=origin;m_attack.angles=angles;m_attack.simulation=victim->m_flSimulationTime();
                SDK::FixMovement(command,angles);command->viewangles=angles;
                if(Vars::Aimbot::General::AimType.Value==Vars::Aimbot::General::AimTypeEnum::Plain) I::EngineClient->SetViewAngles(angles);
                else if(Vars::Aimbot::General::AimType.Value!=Vars::Aimbot::General::AimTypeEnum::Legit) G::SilentAngles=true;
                G::AimTarget={victim->entindex(),I::GlobalVars->tickcount};G::AimPoint={point,I::GlobalVars->tickcount};
                F::Aimbot.m_eRanType=EWeaponType::MELEE;
                return true;
            }
        }
    }
    if(Active())
    {
        if(!selected) Reset();
        else if(m_settling) {command->buttons&=~IN_ATTACK;return true;}
        else if(m_trick)
        {
            if(PlanTrick(local,weapon,m_target.Get(),command)) {command->buttons&=~IN_ATTACK;return true;}
            Reset();
        }
    }
    if(!targets.empty()&&targets.front().m_pEntity->IsPlayer())
    {
        auto* victim=targets.front().m_pEntity->As<CTFPlayer>();
        if(ValidTarget(local,weapon,victim)&&Reliable(victim))
        {
            m_visualTarget.Set(victim);
            if(PlanWarp(local,weapon,victim,command)||PlanTrick(local,weapon,victim,command))
                {command->buttons&=~IN_ATTACK;return true;}
        }
    }
    // Precision owns auto-backstab completely. Legacy/history paths must not
    // re-authorize a body slash after this current-pose check rejected it.
    command->buttons&=~IN_ATTACK;G::Attacking=SDK::IsAttacking(local,weapon,command,true);
    return true;
}

void CSpyKnife::Finalize(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command)
{
    if(local&&weapon&&local->m_iClass()==TF_CLASS_SPY&&weapon->GetWeaponID()==TF_WEAPON_KNIFE
        &&(F::Ticks.m_bWarp||F::Ticks.m_bShifting)) m_warpBarrier=command->command_number;
    if(Eligible(local,weapon)&&(command->buttons&IN_ATTACK))
    {
        auto* target=m_attack.target.Get();Vec3 angles,point;
        bool valid=Finite(command->viewangles)&&!F::Ticks.m_bWarp&&!F::Ticks.m_bShifting&&m_warpBarrier<=I::ClientState->last_command_ack
            &&m_attack.command==command->command_number&&m_attack.weapon.Get()==weapon&&target
            &&m_attack.spawn==target->m_iSpawnCounter()&&ValidTarget(local,weapon,target)&&Reliable(target)
            &&std::abs(target->m_flSimulationTime()-m_attack.simulation)<TICK_INTERVAL*.5f
            &&TargetOrigin(target).DistToSqr(m_attack.origin)<.25f;
        if(valid)
        {
            if(const Vec3* held=F::Ticks.GetShootAngle();held&&Math::CalcFov(*held,command->viewangles)>.25f) valid=false;
            else valid=Contact(local,weapon,target,m_attack.origin,command->viewangles,angles,point,false,nullptr,true);
            // Reject disagreements between the sampled server pose and the
            // visible pose, rather than letting interpolation turn a graze into a slash.
            if(valid&&target->m_vecOrigin().DistToSqr(m_attack.origin)>1.f)
                valid=Contact(local,weapon,target,target->m_vecOrigin(),command->viewangles,angles,point,false,nullptr,true);
        }
        if(!valid) {command->buttons&=~IN_ATTACK;m_status=Status::Uncertain;m_rejectedCommand=command->command_number;F::Ticks.CancelKnifeAttack();}
        else if(!F::AntiCheatCompatibility.Active()
            &&std::abs(TICKS_TO_TIME(F::Backtrack.GetRewindTick())-m_attack.simulation)<.2f)
            command->tick_count=TIME_TO_TICKS(m_attack.simulation)+TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
        G::Attacking=SDK::IsAttacking(local,weapon,command,true);
        G::SendPacket=true;
    }
    CacheVisual(local,weapon,command);
}
