#include "StickyJumpControl.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../AutoDodge/AutoDodge.h"
#include "../../Ticks/Ticks.h"
#include "../../ImGui/Menu/Menu.h"
#include <array>
#include <chrono>
#include <cstdio>

namespace
{
    using namespace StickyControl;
    Vec V(const Vec3& v) { return {v.x,v.y,v.z}; }
    Vec3 E(Vec v) { return {v.x,v.y,v.z}; }
    float Convar(const char* name, float fallback)
    {
        auto var=H::ConVars.FindVar(name);
        return var ? var->GetFloat() : fallback;
    }
    Body ReadBody(CTFPlayer* player)
    {
        Body body;
        body.origin=V(player->m_vecOrigin()); body.velocity=V(player->m_vecVelocity());
        body.mins=V(player->m_vecMins()); body.maxs=V(player->m_vecMaxs());
        body.ducked=player->IsDucking(); body.grounded=player->IsOnGround();
        return body;
    }
    uint32_t Identity(CBaseEntity* entity)
    {
        return entity->GetRefEHandle().ToInt();
    }
    bool Supported(CTFWeaponBase* weapon)
    {
        // Stock, reskins, Jumper and Quickiebomb all use global detonation.
        return weapon && StickyControl::SupportedLauncher(weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER,
            weapon->GetWeaponID()==TF_WEAPON_PIPEBOMBLAUNCHER ? weapon->As<CTFPipebombLauncher>()->GetDetonateType() : -1);
    }
    bool DetonatorAvailable(CTFPlayer* local)
    {
        // The launcher's ItemBusyFrame calls SecondaryAttack during reload;
        // that path checks CanAttack, not the generic busy-frame attack clocks.
        return local->CanAttack();
    }
    void DestroySimulation(CProjectileSimulation& simulation)
    {
        if (simulation.m_pEnv) I::Physics->DestroyEnvironment(simulation.m_pEnv);
        if (simulation.m_pBoxCollide) I::PhysicsCollision->DestroyCollide(simulation.m_pBoxCollide);
        simulation.m_pEnv=nullptr; simulation.m_pBoxCollide=nullptr;
        simulation.m_pObj=simulation.m_pObjBox=nullptr; simulation.m_pCurrent=nullptr;
    }
}

// Engine access is confined to this adapter. Candidate generation is resumed
// between commands; the independent VPhysics instance survives those slices.
class CStickyJumpWorld
{
    using Clock=std::chrono::steady_clock;
    CTFPlayer* m_local=nullptr;
    CTFWeaponBase* m_weapon=nullptr;
    CUserCmd m_raw;
    StickyControl::Frame m_seed;
    CScopedLocalMovement::Snapshot m_seedPlayer;
    CUserCmd m_seedRaw;
    std::array<StickyControl::Body,17> m_jump;
    struct Landing { StickyControl::Vec origin; bool checked=false, valid=false; };
    std::vector<Landing> m_landings;
    bool m_jumpReady=false;
    CProjectileSimulation m_liveSimulation;
    struct SeedProjectile { uint32_t id; ProjectileInfo info; };
    std::vector<SeedProjectile> m_seedProjectiles;
    std::vector<CTFGrenadePipebombProjectile*> m_entities;
    struct ForecastCache
    {
        uint32_t id=0;
        int ticks=0;
        StickyControl::Vec position;
        bool valid=false;
    };
    std::vector<ForecastCache> m_forecasts;
    std::vector<ForecastCache> m_seedForecasts;
    StickyControl::Body m_baseline;
    float m_baselineTime=0;

    StickyControl::Bomb Prototype() const
    {
        StickyControl::Bomb bomb;
        bomb.damage=m_weapon->GetDamage(false);
        bomb.pushMultiplier=SDK::AttribHookValue(1.f,"mult_dmgself_push_force",m_weapon);
        const int type=m_weapon->GetDamageType();
        bomb.falloff=(type&DMG_RADIUS_MAX) ? 0.f : (type&DMG_HALF_FALLOFF) ? .5f : bomb.damage/146.f;
        const float modifier=SDK::AttribHookValue(1.f,"mult_dmg_falloff",m_weapon);
        if (modifier!=1.f) bomb.falloff+=modifier;
        return bomb;
    }
    bool Floor(StickyControl::Vec point, StickyControl::Vec& origin)
    {
        CTraceFilterWorldAndPropsOnly filter(m_local);
        CGameTrace trace;
        SDK::TraceHull(E(point)+Vec3{0,0,36},E(point)-Vec3{0,0,64},
            {-24,-24,0},{24,24,1},MASK_PLAYERSOLID,&filter,&trace);
        if (trace.startsolid || trace.allsolid || trace.fraction>=1 || trace.plane.normal.z<.7f) return false;
        origin=V(trace.endpos)+StickyControl::Vec{0,0,.1f};
        SDK::TraceHull(E(origin),E(origin),{-24,-24,0},{24,24,82},MASK_PLAYERSOLID,&filter,&trace);
        return !trace.startsolid && !trace.allsolid;
    }
    bool Visible(StickyControl::Vec from, const StickyControl::Body& body)
    {
        CTraceFilterWorldAndPropsOnly filter(m_local);
        CGameTrace trace;
        const auto center=body.origin+(body.mins+body.maxs)*.5f;
        SDK::TraceHull(E(from),E(center),{},{},MASK_SOLID,&filter,&trace);
        return !trace.startsolid && !trace.allsolid && trace.fraction>.99f;
    }
    bool Forecast(const StickyControl::Bomb& bomb, int ticks, StickyControl::Vec& position, bool seed=false)
    {
        position=bomb.position;
        if (bomb.touched || ticks<=0) return true;
        auto& forecasts=seed ? m_seedForecasts : m_forecasts;
        const auto cached=std::find_if(forecasts.begin(),forecasts.end(),[&](const auto& c){return c.id==bomb.id && c.ticks==ticks;});
        if (cached!=forecasts.end()) { position=cached->position; return cached->valid; }
        ProjectileInfo info;
        CTFGrenadePipebombProjectile* entity=nullptr;
        if (seed)
        {
            const auto it=std::find_if(m_seedProjectiles.begin(),m_seedProjectiles.end(),[&](const auto& p){return p.id==bomb.id;});
            if (it==m_seedProjectiles.end()) return false;
            info=it->info;
        }
        else
        {
            const auto it=std::find_if(m_entities.begin(),m_entities.end(),[&](auto* e){return Identity(e)==bomb.id;});
            if (it==m_entities.end()) return false;
            entity=*it; m_liveSimulation.GetInfo(entity,info);
        }
        if (!m_liveSimulation.Initialize(info,false,true)) return false;
        CTraceFilterCollideable filter(m_local); int mask=MASK_SOLID;
        if (entity) m_liveSimulation.SetupTrace(filter,mask,entity);
        for (int tick=0;tick<ticks;++tick)
        {
            const Vec3 previous=m_liveSimulation.GetOrigin();
            m_liveSimulation.RunTick(info,false);
            CGameTrace trace;
            SDK::TraceHull(previous,m_liveSimulation.GetOrigin(),-info.m_vHull,info.m_vHull,mask,&filter,&trace);
            if (trace.startsolid || trace.allsolid) return false;
            position=V(m_liveSimulation.GetOrigin());
            if (trace.fraction<1)
            {
                // A prospective bounce against a moving entity is uncertain.
                if (trace.m_pEnt && trace.m_pEnt->entindex()!=0) return false;
                position=V(trace.endpos+trace.plane.normal);
                break;
            }
        }
        const bool valid=position.Finite();
        forecasts.push_back({bomb.id,ticks,position,valid});
        return valid;
    }
    StickyControl::Vec Boost(const StickyControl::Frame& frame, const StickyControl::Body& body,
        int ticks, bool requireArmed, bool& certain)
    {
        using namespace StickyControl;
        Vec impulse; certain=true;
        for (auto bomb:frame.bombs)
        {
            // Distant traps still detonate, but cannot contribute to this jump.
            if ((bomb.position-body.origin).Length()>400.f+bomb.velocity.Length()*ticks*frame.interval) continue;
            if (requireArmed && !Armed(bomb,frame.projectileTime-2.f*frame.interval)) { certain=false; return {}; }
            if (!Forecast(bomb,ticks,bomb.position,&frame==&m_seed)) { certain=false; return {}; }
            impulse=impulse+BlastImpulse(bomb,body,Visible(bomb.position,body));
        }
        return impulse;
    }
    bool Clearance(const StickyControl::Body& body, StickyControl::Vec delta)
    {
        CTraceFilterWorldAndPropsOnly filter(m_local); CGameTrace trace;
        const auto end=body.origin+(body.velocity+delta)*.12f+StickyControl::Vec{0,0,-SDK::GetGravity()*.0072f};
        SDK::TraceHull(E(body.origin),E(end),E(body.mins),E(body.maxs),MASK_PLAYERSOLID,&filter,&trace);
        return !trace.startsolid && !trace.allsolid && trace.fraction>.99f;
    }
    bool Probe(int ticks, StickyControl::Body& result)
    {
        CScopedLocalMovement movement(m_local,false);
        if (!movement.Valid()) return false;
        CUserCmd command=m_raw;
        command.buttons&=~(IN_ATTACK|IN_ATTACK2|IN_JUMP);
        for (int tick=0;tick<ticks;++tick)
        {
            command.buttons|=IN_DUCK;
            if (tick==0 && m_local->IsOnGround()) command.buttons|=IN_JUMP;
            else command.buttons&=~IN_JUMP;
            if (!movement.Step(command)) return false;
        }
        result=ReadBody(m_local);
        return !result.grounded && result.origin.Finite();
    }
public:
    double Now() const { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
    void Unload() { DestroySimulation(m_liveSimulation); }
    void Bind(CTFPlayer* local, CTFWeaponBase* weapon, const CUserCmd& raw)
    { m_local=local; m_weapon=weapon; m_raw=raw; m_forecasts.clear(); }
    std::vector<StickyControl::Bomb> Bombs()
    {
        using namespace StickyControl;
        std::vector<Bomb> bombs; m_entities.clear();
        // Use the shared SDK entity inventory, including practice projectiles.
        for (auto entity:H::Entities.GetGroup(EntityEnum::LocalStickies))
        {
            auto sticky=entity->As<CTFGrenadePipebombProjectile>();
            if (!sticky->HasStickyEffects() || sticky->m_hThrower().Get()!=m_local) continue;
            auto launcher=sticky->m_hLauncher().Get();
            if (!launcher || launcher!=m_weapon) continue;
            Bomb bomb=Prototype(); bomb.id=Identity(sticky);
            bomb.position=V(sticky->GetAbsOrigin()); bomb.velocity=V(sticky->GetAbsVelocity());
            bomb.touched=sticky->m_bTouched(); bomb.created=sticky->m_flCreationTime();
            bomb.ageKnown=std::isfinite(bomb.created) && bomb.created>0;
            bomb.armTime=SDK::AttribHookValue(Convar("tf_grenadelauncher_livetime",.8f),"sticky_arm_time",m_weapon);
            if (m_local->InCond(TF_COND_RUNE_HASTE)) bomb.armTime*=.5f;
            else if (m_local->InCond(TF_COND_RUNE_KING)||m_local->InCond(TF_COND_KING_BUFFED)) bomb.armTime*=.75f;
            if (bomb.position.Finite() && bomb.velocity.Finite()) { bombs.push_back(bomb); m_entities.push_back(sticky); }
        }
        return bombs;
    }
    void BeginSearch(const StickyControl::Frame& frame, const StickyControl::Request&)
    {
        m_seed=frame; m_jumpReady=false;
        m_seedPlayer=CScopedLocalMovement::Capture(m_local); m_seedRaw=m_raw;
        m_seedForecasts.clear(); m_seedProjectiles.clear();
        for (auto* entity:m_entities)
        {
            ProjectileInfo info; m_liveSimulation.GetInfo(entity,info);
            m_seedProjectiles.push_back({Identity(entity),info});
        }
        m_landings.assign(frame.bombs.size()*43,{});
    }
    StickyControl::SampleResult NextCandidate(int cursor, const StickyControl::Frame&,
        const StickyControl::Request& request, StickyControl::Candidate& candidate)
    {
        using namespace StickyControl;
        if (!m_jumpReady)
        {
            CScopedLocalMovement movement(m_local,false,&m_seedPlayer);
            if (!movement.Valid()) return SampleResult::Done;
            // Normalize using local geometry independent of held movement.
            Vec floor;
            bool found=false;
            for (const auto& bomb:m_seed.bombs)
                if (Floor(bomb.position,floor)) { found=true; break; }
            if (!found) return SampleResult::Done;
            m_local->m_vecOrigin()=E(floor); m_local->SetAbsOrigin(E(floor));
            m_local->m_fFlags()|=FL_ONGROUND;
            CUserCmd command=m_seedRaw;
            command.forwardmove=command.sidemove=command.upmove=0;
            command.buttons&=~(IN_ATTACK|IN_ATTACK2|IN_JUMP);
            m_local->m_vecVelocity()={}; m_local->SetAbsVelocity({});
            m_jump[0]=ReadBody(m_local);
            for (int tick=1;tick<=16;++tick)
            {
                command.buttons|=IN_DUCK;
                if (tick==1) command.buttons|=IN_JUMP; else command.buttons&=~IN_JUMP;
                if (!movement.Step(command)) return SampleResult::Done;
                m_jump[tick]=ReadBody(m_local);
            }
            m_jumpReady=true;
            return SampleResult::Pending;
        }
        constexpr int radii=43, times=6;
        static constexpr int jumpTicks[]={1,2,4,7,11,16};
        const int anchors=int(m_seed.bombs.size());
        const int layout=cursor/(radii*times);
        if (layout>=anchors) return SampleResult::Done;
        const Vec anchor=m_seed.bombs[layout].position;
        // Sample along the desired push ray; the third coordinate comes from
        // traced walkable geometry instead of an assumed flat floor.
        const float yaw=request.yaw*Pi/180.f;
        const int radial=(cursor/times)%radii;
        const Vec point=anchor+Vec{std::cos(yaw),std::sin(yaw),0}*(float(radial)*4.f);
        auto& landing=m_landings[layout*radii+radial];
        if (!landing.checked)
        {
            landing.checked=true;
            landing.valid=(point-request.start).Length()<=256.f && Floor(point,landing.origin);
        }
        if (!landing.valid) return SampleResult::Sample;
        candidate.takeoff=landing.origin;
        candidate.jumpTicks=jumpTicks[cursor%times];
        Body body=m_jump[candidate.jumpTicks];
        body.origin=candidate.takeoff+(body.origin-m_jump[0].origin);
        if (body.grounded) return SampleResult::Sample;
        bool certain;
        candidate.delta=Boost(m_seed,body,candidate.jumpTicks,false,certain);
        candidate.valid=certain && Clearance(body,candidate.delta);
        return SampleResult::Sample;
    }
    bool Ready(const StickyControl::Frame& frame, const StickyControl::Request& request,
        float target, StickyControl::Vec& delta)
    {
        using namespace StickyControl;
        if (!LaunchArmed(frame)) return false;
        Body body;
        if (!Probe(1,body)) return false;
        bool certain;
        delta=Boost(frame,body,1,true,certain);
        m_baseline=body;
        return certain && MeetsTarget(delta,request.Wish(),target) && Clearance(body,delta);
    }
    bool CanBeginJump(const StickyControl::Frame& frame, const StickyControl::Request& request,
        StickyControl::Candidate& candidate, float target)
    {
        if (!frame.grounded || !StickyControl::LaunchArmed(frame)) return false;
        StickyControl::Body body;
        if (!Probe(candidate.jumpTicks,body)) return false;
        bool certain;
        const auto delta=Boost(frame,body,candidate.jumpTicks,true,certain);
        const bool valid=certain && StickyControl::MeetsTarget(delta,request.Wish(),target) && Clearance(body,delta);
        if (valid) candidate.delta=delta;
        return valid;
    }
    void RecordBaseline(const StickyControl::Frame& frame) { m_baselineTime=frame.gameTime+frame.interval; }
    float MeasuredBoost(const StickyControl::Body&, const StickyControl::Frame& frame)
    {
        const float elapsed=(std::max)(0.f,frame.gameTime-m_baselineTime);
        const auto expected=m_baseline.velocity+StickyControl::Vec{0,0,-SDK::GetGravity()*elapsed};
        return (frame.body.velocity-expected).Length();
    }
};

CStickyJumpControl::CStickyJumpControl() = default;
CStickyJumpControl::~CStickyJumpControl() = default;

void CStickyJumpControl::Reset()
{
    m_controller.Reset(); m_controls=false; m_command=0; m_key={};
}
void CStickyJumpControl::Unload()
{
    Reset();
    if (m_world) m_world->Unload();
    m_world.reset();
}
void CStickyJumpControl::Prepare(CTFPlayer* local, CTFWeaponBase* weapon, CUserCmd* command)
{
    using namespace StickyControl;
    namespace Config=Vars::Misc::Movement::StickyJumpControl;
    if (!local || !command) { Reset(); return; }
    if (!Config::Enabled.Value && !Busy()) { m_controls=false; return; }
    if (!m_world) m_world=std::make_unique<CStickyJumpWorld>();
    m_world->Bind(local,weapon,G::OriginalCmd);
    Frame frame;
    frame.command=command->command_number;
    frame.weapon=weapon ? Identity(weapon) : 0;
    frame.wallTime=m_world->Now(); frame.gameTime=TICKS_TO_TIME(local->m_nTickBase()); frame.interval=TICK_INTERVAL;
    // OnDataChanged stamps m_flCreationTime using this clock. Prediction's
    // tickbase can lead it and must only be used for weapon attack cooldowns.
    frame.projectileTime=I::GlobalVars->curtime;
    frame.body=ReadBody(local); frame.grounded=local->IsOnGround(); frame.yaw=G::OriginalCmd.viewangles.y;
    frame.pitch=G::OriginalCmd.viewangles.x;
    frame.enabled=Config::Enabled.Value;
    frame.eligible=Supported(weapon) && local->IsAlive() && local->m_iClass()==TF_CLASS_DEMOMAN
        && local->m_MoveType()==MOVETYPE_WALK && !local->IsSwimming() && !local->IsTaunting() && !local->IsAGhost()
        && !local->InCond(TF_COND_HALLOWEEN_KART) && !local->InCond(TF_COND_SHIELD_CHARGE)
        && !local->InCond(TF_COND_RUNE_PRECISION) && SDK::IsGameWindowInFocus()
        && !I::EngineVGui->IsGameUIVisible() && !F::Menu.m_bIsOpen
        && frame.body.origin.Finite() && frame.body.velocity.Finite();
    frame.dodge=F::AutoDodge.Controls(command);
    frame.shifting=F::Ticks.m_bWarp || F::Ticks.m_bDoubletap || F::Ticks.m_bShifting;
    frame.manualCombat=bool(G::OriginalCmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_ATTACK3|IN_JUMP));
    if (frame.eligible)
    {
        frame.bombs=m_world->Bombs();
        frame.canDetonate=DetonatorAvailable(local);
        if (!Busy() && weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime()>0) frame.eligible=false;
    }
    // A dedicated key storage makes a held key one activation, independent of
    // UI binds and repeated prediction of the same command number.
    if (command->command_number!=m_command) frame.pressed=U::KeyHandler.Pressed(Config::StartKey.Value,true,&m_key);
    Request request;
    request.direction=Config::Direction.Value==Config::DirectionEnum::Horizontal ? Direction::Horizontal
        : Config::Direction.Value==Config::DirectionEnum::Vertical ? Direction::Vertical : Direction::Diagonal;
    request.automatic=Config::Direction.Value==Config::DirectionEnum::Auto;
    request.power=Config::Power.Value;
    const auto output=m_controller.Run(frame,request,*m_world);
    m_command=command->command_number; m_controls=output.controls;
    if (!m_controls) return;
    m_intent=G::OriginalCmd;
    m_intent.buttons&=~(IN_ATTACK|IN_ATTACK2|IN_ATTACK3|IN_JUMP);
    m_intent.weaponselect=0;
    if (output.duck) m_intent.buttons|=IN_DUCK;
    if (output.jump) m_intent.buttons|=IN_JUMP;
    if (output.detonate) m_intent.buttons|=IN_ATTACK2;
    Apply(local,command);
}
void CStickyJumpControl::Apply(CTFPlayer* local, CUserCmd* command)
{
    if (!Controls(command)) return;
    command->buttons=m_intent.buttons;
    command->forwardmove=m_intent.forwardmove; command->sidemove=m_intent.sidemove; command->upmove=m_intent.upmove;
    command->weaponselect=0; command->viewangles=m_intent.viewangles;
    G::SilentAngles=false; G::PSilentAngles=false;
    G::Attacking=SDK::IsAttacking(local,H::Entities.GetWeapon(),command);
    G::SendPacket=true;
}
void CStickyJumpControl::Draw()
{
#ifndef TEXTMODE
    using namespace Vars::Misc::Movement::StickyJumpControl;
    if (!Enabled.Value || !m_world || SDK::CleanScreenshot()) return;
    const auto display=Snapshot();
    if (display.expires<m_world->Now()) return;
    const auto& font=H::Fonts.GetFont(FONT_INDICATORS);
    const Color_t color{145,218,255,255};
    if (DrawStatus.Value)
    {
        char mode[64]{};
        if (display.automatic)
        {
            const char* names[]={"Horizontal","Diagonal","Vertical"};
            std::snprintf(mode,sizeof(mode)," | Auto %s (%+.1f deg)",names[int(display.direction)],display.elevation);
        }
        char message[256];
        std::snprintf(message,sizeof(message),"Sticky Jump Control%s | %s | %.0f%% | target %.0f / predicted %.0f HU/s | measured %.0f",
            mode,display.status,display.power,display.target,display.predicted,display.measured);
        H::Draw.StringWithBackground(font,H::Draw.m_nScreenW/2,int(H::Draw.m_nScreenH*.75f),color,{17,20,28,220},ALIGN_TOP,message);
    }
    if (DrawMarkers.Value && display.markers)
    {
        const auto marker=[&](StickyControl::Vec position, const char* label, Color_t tint)
        {
            Vec3 screen;
            if (!SDK::W2S(E(position)+Vec3{0,0,4},screen)) return;
            H::Draw.Line(int(screen.x)-7,int(screen.y),int(screen.x)+7,int(screen.y),tint);
            H::Draw.Line(int(screen.x),int(screen.y)-7,int(screen.x),int(screen.y)+7,tint);
            H::Draw.String(font,int(screen.x),int(screen.y)+10,tint,ALIGN_TOP,label);
        };
        marker(display.plan.takeoff,"Takeoff",color);
    }
#endif
}
