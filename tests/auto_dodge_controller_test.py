"""Exercise the production dodge controller with the transaction fake engine."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from auto_dodge_movement_test import STUB, MOVEMENT, ROOT

PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <vector>
#include <functional>
int totalMoves=0,totalTraces=0;bool blockedSplash=false;
std::function<float(float,float)> groundHeight=[](float,float){return 0.f;};
int jumpProbes=0,prerollMoves=0,fallProbes=0;
float brakeFriction=.7f;
// Freeze time by default; inject native-operation costs for deterministic
// deadline tests, without changing production controller/budget logic.
struct FakeClock {
    using duration=std::chrono::nanoseconds;
    using time_point=std::chrono::time_point<FakeClock,duration>;
    static inline duration elapsed{},moveCost{},traceCost{},physicsCost{},infoCost{};
    static time_point now(){return time_point(elapsed);}
};
enum class ETFClassID {Other,CTFProjectile_Rocket,CTFProjectile_SentryRocket,CTFBaseRocket,
    CTFFlameRocket,CTFProjectile_EnergyBall,CTFProjectile_Flare,CTFProjectile_Jar,
    CTFProjectile_JarMilk,CTFProjectile_JarGas,CTFGrenadePipebombProjectile,CBaseGrenade,
    CTFWeaponBaseGrenadeProj,CTFProjectile_Arrow,CTFProjectile_HealingBolt,
    CTFProjectile_Cleaver,CTFStunBall,CTFBall_Ornament,CTFProjectile_EnergyRing,
    CTFProjectile_BallOfFire,CTFProjectile_MechanicalArmOrb};
constexpr int TF_GL_MODE_REGULAR=0,TF_GL_MODE_REMOTE_DETONATE=1,TF_GL_MODE_CANNONBALL=3;
constexpr int TF_TEAM_RED=2,MOVETYPE_WALK=2,MOVETYPE_NONE=0,FLOW_OUTGOING=0;
constexpr int TF_COND_HALLOWEEN_KART=1,TF_COND_SHIELD_CHARGE=2,TF_COND_STUNNED=3;
constexpr int IN_FORWARD=8,IN_BACK=16,IN_MOVELEFT=512,IN_MOVERIGHT=1024;
constexpr int SKIP_CHECK=0,OBJECT_DEFAULT=0,MASK_SOLID=1;
constexpr float TF_ROCKET_RADIUS=146;
constexpr int FIELD_FLOAT=1,FIELD_TIME=2,TD_OFFSET_NORMAL=0;
struct typedescription_t {const char* fieldName=nullptr;int fieldType=0;int fieldOffset[2]{};};
template<class T> struct CHandle {
    T* pointer=nullptr;T* Get() const {return pointer;}void Term(){pointer=nullptr;}void Set(T* p){pointer=p;}
};
#define TIME_TO_TICKS(t) (int(.5f+(t)/TICK_INTERVAL))
#define ADD_FEATURE(Type,Name) namespace F {inline Type Name;}
'''

PLAYER = r'''
    ETFClassID id=ETFClassID::Other;
    int team=2,moveType=MOVETYPE_WALK,spawn=1,pipeType=0;
    float radius=146,created=0,simTime=1.5f,waterJump=0,elasticity=.45f;
    bool touched=false,alive=true,dormant=false;
    Vec3 mins{-24,-24,0},maxs{24,24,82};
    CTFPlayer* owner=nullptr;
    template<class T> T* As(){return reinterpret_cast<T*>(this);}
    ETFClassID GetClassID(){return id;}
    int m_iTeamNum(){return team;}int m_MoveType(){return moveType;}
    int m_iSpawnCounter(){return spawn;}int m_iType(){return pipeType;}
    float m_flSimulationTime(){return simTime;}float m_flCreationTime(){return created;}
    float m_DmgRadius(){return radius;}float m_flElasticity(){return elasticity;}
    float m_flWaterJumpTime(){return waterJump;}
    bool m_bTouched(){return touched;}
    bool IsAlive(){return alive;}bool IsAGhost(){return false;}bool IsSwimming(){return false;}
    bool IsTaunting(){return false;}bool IsDormant(){return dormant;}bool InCond(int){return false;}
    bool IsOnGround(){return state.ground;}bool IsPlayer(){return id==ETFClassID::Other;}
    bool IsBaseObject(){return false;}int SolidMask(){return MASK_SOLID;}
    Vec3& m_vecMins(){return mins;}Vec3& m_vecMaxs(){return maxs;}
    CHandle<CTFPlayer> m_hGrapplingHookTarget(){return {};}
'''

ADAPTERS = r'''
using CBaseEntity=CTFPlayer;using CBaseGrenade=CTFPlayer;using CTFGrenadePipebombProjectile=CTFPlayer;
using IHandleEntity=CTFPlayer;
struct CTFWeaponBase {};
struct CTraceFilterCollideable {
    int m_iTeam=0,m_iType=0,m_iObject=0;
    virtual ~CTraceFilterCollideable()=default;
    virtual bool ShouldHitEntity(IHandleEntity*,int){return true;}
};
struct CTraceFilterWorldAndPropsOnly {explicit CTraceFilterWorldAndPropsOnly(CTFPlayer*){}};
struct CGameTrace {
    Vec3 endpos;bool startsolid=false,hit=false;
    struct {Vec3 normal{0,0,1};} plane;
    CBaseEntity* m_pEnt=nullptr;
    bool DidHit()const{return hit;}
};
namespace SDK {
    float AttribHookValue(float v,const char*,CTFWeaponBase*) {return v;}
    float MaxSpeed(CTFPlayer*){return 300;}
    template<class Filter> void TraceHull(Vec3 from,Vec3 to,Vec3,Vec3,int,Filter*,CGameTrace* out) {
        FakeClock::elapsed+=FakeClock::traceCost;++totalTraces;out->endpos=to;
        if(from.z>=0&&to.z<0){out->endpos.z=0;out->hit=true;}
    }
    void Trace(Vec3 from,Vec3 to,int,CTraceFilterWorldAndPropsOnly*,CGameTrace* out){
        FakeClock::elapsed+=FakeClock::traceCost;++totalTraces;out->endpos=to;
        if(from.x==to.x&&from.y==to.y&&from.z-to.z>=64) {
            float floor=groundHeight(to.x,to.y);
            out->hit=std::isfinite(floor)&&from.z>=floor&&to.z<=floor;
            if(out->hit) out->endpos.z=floor;
        } else out->hit=blockedSplash;
    }
}
namespace Math {float NormalizeAngle(float v){return std::remainder(v,360.f);}}
namespace G {bool Unload=false;CUserCmd* CurrentUserCmd=nullptr;CUserCmd* LastUserCmd=nullptr;}
namespace EntityEnum {constexpr int WorldProjectile=1;}
namespace H {
    struct Entities_t {std::vector<CBaseEntity*> list;const auto& GetGroup(int){return list;}} Entities;
    struct Live {float GetFloat(){return .8f;}};
    struct ConVars_t {Live* FindVar(const char*){return nullptr;}} ConVars;
}
namespace I {
    struct ClientState_t {bool IsPaused(){return false;}} clientState;auto* ClientState=&clientState;
    struct EngineClient_t {bool IsPlayingDemo(){return false;}} engineClient;auto* EngineClient=&engineClient;
    struct EngineVGui_t {bool IsGameUIVisible(){return false;}} engineVGui;auto* EngineVGui=&engineVGui;
    struct Physics_t {void DestroyEnvironment(void*){}} physics;auto* Physics=&physics;
    struct PhysicsCollision_t {void DestroyCollide(void*){}} collision;auto* PhysicsCollision=&collision;
}
namespace F {
    struct Ticks_t {bool m_bShifting=false,m_bWarp=false,m_bDoubletap=false,m_bRecharge=false,m_bAntiWarp=false;} Ticks;
    struct Backtrack_t {int m_iTickCount=100;float outgoing=0;float GetReal(int){return outgoing;}int GetAnticipatedChoke(){return 0;}} Backtrack;
    struct Reset_t {int count=0;void Reset(){++count;}} AutoRocketJump,AutoTrolldier,SpyKnife;
    struct AimbotProjectile_t {float GetSplashRadius(CBaseEntity* p,CTFWeaponBase*,CTFPlayer*){return p->radius;}} AimbotProjectile;
}
namespace Vars::Misc::Movement::AutoDodge {
    template<class T>struct Var{T Value;};
    Var<bool> Enabled{true},Jump{true},Duck{true},AvoidStickies{true};
    Var<float> PredictionTime{.8f},SafetyMargin{48},CalculationBudget{4};
}
struct ProjectileInfo {Vec3 m_vPos,m_vHull;float m_flVelocity=0;CTFWeaponBase* m_pWeapon=nullptr;CTFPlayer* m_pOwner=nullptr;};
std::vector<CBaseEntity*> refinementOrder;CBaseEntity* infoEntity=nullptr;int infoReads=0;
struct CProjectileSimulation {
    void* m_pEnv=nullptr;void* m_pBoxCollide=nullptr;void* m_pObj=nullptr;void* m_pObjBox=nullptr;
    ProjectileInfo* m_pCurrent=nullptr;
    Vec3 pos,vel;
    std::pair<CTFWeaponBase*,CTFPlayer*> GetEntities(CBaseEntity* p){return {nullptr,p->owner};}
    Vec3 GetVelocity(CBaseEntity* p){return p->state.velocity;}
    float GetGravity(CBaseEntity*,CTFWeaponBase*){return 0;}
    void GetInfo(CBaseEntity* p,ProjectileInfo& info){FakeClock::elapsed+=FakeClock::infoCost;++infoReads;infoEntity=p;info.m_vPos=p->state.origin;info.m_vHull={1,1,1};vel=p->state.velocity;info.m_flVelocity=std::sqrt(vel.LengthSqr());}
    bool Initialize(ProjectileInfo& info,bool,bool){refinementOrder.push_back(infoEntity);pos=info.m_vPos;m_pCurrent=&info;return true;}
    void SetupTrace(CTraceFilterCollideable&,int&,CBaseEntity*){}
    void RunTick(ProjectileInfo&,bool){FakeClock::elapsed+=FakeClock::physicsCost;pos=pos+vel*TICK_INTERVAL;}
    Vec3 GetOrigin(){return pos;}Vec3 GetVelocity(){return vel;}
    void SetState(Vec3 p,Vec3 v){pos=p;vel=v;}
};
'''

CHECKS = r'''
int main() {
    CTFPlayer player,rocket;player.state.origin={0,0,0};player.state.velocity={};
    rocket.team=3;rocket.id=ETFClassID::CTFProjectile_Rocket;rocket.state.origin={-600,0,41};rocket.state.velocity={1100,0,0};
    rocket.radius=146;rocket.simTime=1.5f;rocket.owner=&player; // reflected local rocket is hostile now
    CUserCmd command;command.command_number=1;command.buttons=IN_ATTACK|IN_ATTACK2;
    command.weaponselect=7;command.impulse=9;command.viewangles={10,0,0};
    G::CurrentUserCmd=G::LastUserCmd=&command;
    H::Entities.list={&rocket};
    State original=player.state;
    auto checkState=[&]{assert(std::memcmp(&player.state,&original,sizeof(State))==0);assert(!I::Prediction->m_bInPrediction);};
    F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command));
    assert(std::abs(command.forwardmove)+std::abs(command.sidemove)>0 || (command.buttons&(IN_JUMP|IN_DUCK)));
    assert((command.buttons&(IN_ATTACK|IN_ATTACK2))==(IN_ATTACK|IN_ATTACK2));
    assert(command.weaponselect==7&&command.impulse==9&&command.viewangles.y==0&&command.viewangles.x==10);
    assert(F::AutoRocketJump.count==1&&F::AutoTrolldier.count==1&&F::SpyKnife.count==1);
    assert(totalMoves<=2048&&totalTraces<=4096);checkState();
    Vec3 world=WorldWish(command);
    command.viewangles.y=150;F::AutoDodge.ApplyMovement(&command);
    assert((WorldWish(command)-world).LengthSqr()<.001f);
    // The decision owns exactly one command; no stale movement leaks forward.
    ++command.command_number;command.forwardmove=command.sidemove=0;command.buttons=IN_ATTACK;
    H::Entities.list.clear();F::AutoDodge.Prepare(&player,&command);
    assert(!F::AutoDodge.Controls(&command)&&command.forwardmove==0&&command.sidemove==0);checkState();
    // Safe manual movement remains byte-for-byte unchanged.
    ++command.command_number;player.state.origin.y=100;original=player.state;
    H::Entities.list={&rocket};command.viewangles={0,0,0};
    F::AutoDodge.Prepare(&player,&command);
    assert(!F::AutoDodge.Controls(&command)&&command.buttons==IN_ATTACK);
    // The late pass of identical world input must not simulate it again.
    int safeMoves=totalMoves,safeTraces=totalTraces;
    F::AutoDodge.Prepare(&player,&command,true);
    assert(totalMoves==safeMoves&&totalTraces==safeTraces);checkState();
    // A late NavBot route enters the reserved zone; evaluate from the original
    // snapshot and restore the already predicted pose on return.
    F::EnginePrediction.base=player.state;player.state.origin.x=3;original=player.state;
    command.sidemove=450;
    F::AutoDodge.Prepare(&player,&command,true);
    assert(F::AutoDodge.Controls(&command));checkState();
    assert(totalMoves<=4096&&totalTraces<=8192); // two command budgets
    // Unsupported transport and disabled feature neither change inputs nor
    // leave a previous command owning the player.
    ++command.command_number;command.forwardmove=123;command.sidemove=45;
    F::Ticks.m_bWarp=true;F::AutoDodge.Prepare(&player,&command);
    assert(!F::AutoDodge.Controls(&command)&&command.forwardmove==123&&command.sidemove==45);
    F::Ticks.m_bWarp=false;Vars::Misc::Movement::AutoDodge::Enabled.Value=false;
    F::AutoDodge.Prepare(&player,&command);
    assert(!F::AutoDodge.Controls(&command)&&command.forwardmove==123);checkState();
    Vars::Misc::Movement::AutoDodge::Enabled.Value=true;
    // Friendly projectiles cannot cause a dodge.
    ++command.command_number;rocket.team=player.team;
    F::AutoDodge.Prepare(&player,&command);
    assert(!F::AutoDodge.Controls(&command)&&command.forwardmove==123);checkState();
    rocket.team=3;
    // Failed movement probes leave the exact caller command/state intact.
    ++command.command_number;I::GameMovement->fail=true;
    F::AutoDodge.Prepare(&player,&command);
    assert(!F::AutoDodge.Controls(&command)&&command.forwardmove==123&&command.sidemove==45);checkState();
    I::GameMovement->fail=false;
    // Covered stationary hazards can exhaust explicit traces. The remaining
    // exposure stays conservative, and the hard trace ceiling still applies.
    ++command.command_number;totalMoves=totalTraces=0;
    Vars::Misc::Movement::AutoDodge::PredictionTime.Value=1.5f;blockedSplash=true;
    std::array<CTFPlayer,32> stickies;
    H::Entities.list.clear();
    for(auto& sticky:stickies) {
        sticky.team=3;sticky.id=ETFClassID::CTFGrenadePipebombProjectile;
        sticky.pipeType=TF_GL_MODE_REMOTE_DETONATE;sticky.touched=true;sticky.state.origin=player.state.origin;
        H::Entities.list.push_back(&sticky);
    }
    command.forwardmove=command.sidemove=0;
    F::AutoDodge.Prepare(&player,&command);
    assert(totalTraces>=3840&&totalTraces<=4096&&totalMoves<=2048);
    assert(!F::AutoDodge.Controls(&command)&&command.forwardmove==0&&command.sidemove==0);checkState();
    // A slow baseline must not eat the entire command and prevent an escape.
    // Finish the minimum comparison, then stop optional work. One command
    // must not get repeated minimum comparisons on unchanged late input.
    ++command.command_number;totalMoves=totalTraces=0;
    blockedSplash=false;H::Entities.list={&rocket};
    Vars::Misc::Movement::AutoDodge::PredictionTime.Value=.8f;
    FakeClock::moveCost=std::chrono::microseconds(100);
    F::AutoDodge.Prepare(&player,&command);
    assert(totalMoves==53&&!F::AutoDodge.Controls(&command));checkState(); // input is safe at y=100
    F::EnginePrediction.base=player.state;
    FakeClock::elapsed+=std::chrono::seconds(1); // unrelated aimbot/helpers
    F::AutoDodge.Prepare(&player,&command,true);
    assert(totalMoves==53);checkState();
    // A genuinely changed NavBot input gets one minimum late comparison even
    // when a safe early baseline has already exceeded the target budget.
    command.sidemove=450;
    F::AutoDodge.Prepare(&player,&command,true);
    assert(totalMoves>=159&&F::AutoDodge.Controls(&command));checkState();
    // Slow projectile physics leaves a complete conservative forecast, not an
    // empty threat set. The native movement comparison still runs.
    ++command.command_number;totalMoves=totalTraces=0;FakeClock::moveCost={};
    command.forwardmove=command.sidemove=0;player.state.origin={};original=player.state;
    FakeClock::physicsCost=std::chrono::microseconds(500);
    F::AutoDodge.Prepare(&player,&command);
    assert(totalMoves>=106&&F::AutoDodge.Controls(&command));checkState();
    FakeClock::physicsCost={};
    ++command.command_number;totalMoves=totalTraces=0;blockedSplash=true;
    H::Entities.list.clear();for(auto& sticky:stickies) H::Entities.list.push_back(&sticky);
    FakeClock::traceCost=std::chrono::microseconds(25);
    F::AutoDodge.Prepare(&player,&command);
    assert(totalTraces<=256&&totalMoves>=106&&F::AutoDodge.Controls(&command));checkState();
    FakeClock::traceCost={};blockedSplash=false;
    // Time spent between early/late calls must not consume dodge's allowance.
    ++command.command_number;totalMoves=totalTraces=0;
    H::Entities.list={&rocket};FakeClock::moveCost=std::chrono::microseconds(10);
    command.forwardmove=command.sidemove=0;player.state.origin.y=100;original=player.state;
    F::AutoDodge.Prepare(&player,&command);
    assert(totalMoves==53&&!F::AutoDodge.Controls(&command));
    FakeClock::elapsed+=std::chrono::seconds(1);
    F::EnginePrediction.base=player.state;command.sidemove=450;
    F::AutoDodge.Prepare(&player,&command,true);
    assert(totalMoves>53&&totalMoves<=400);checkState();
    FakeClock::moveCost={};
    // Stop after finding a fully clear route in preference order. A single
    // projectile used to consume ~2000 movement ticks, even with an escape.
    ++command.command_number;totalMoves=totalTraces=0;F::AutoDodge.Reset();
    player.state.origin={};player.state.velocity={};original=player.state;
    rocket.state.origin={-800,0,41};command.forwardmove=command.sidemove=0;command.buttons=0;
    F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command)&&totalMoves<=400);checkState();
    ++command.command_number;totalMoves=totalTraces=0;
    command.forwardmove=command.sidemove=0;command.buttons=0;
    F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command)&&totalMoves>=106&&totalMoves<=160);checkState();
    // Identical latency preroll is performed once, not once per candidate.
    ++command.command_number;totalMoves=totalTraces=0;F::AutoDodge.Reset();
    command.forwardmove=command.sidemove=0;command.buttons=0;
    CUserCmd prior;prior.forwardmove=13;G::LastUserCmd=&prior;prerollMoves=0;
    F::Backtrack.outgoing=.15f;
    F::AutoDodge.Prepare(&player,&command);
    assert(prerollMoves==10&&totalMoves<=2048);checkState();
    F::Backtrack.outgoing=0;
    G::LastUserCmd=&command;
    // Mass fire: every selected threat has a complete coarse forecast, even
    // if census uses the refinement slice. The urgent rocket is LAST in the
    // entity group; neither enumeration order nor slow missiles may hide it.
    ++command.command_number;totalMoves=totalTraces=0;F::AutoDodge.Reset();
    std::array<CTFPlayer,39> slow;
    H::Entities.list.clear();
    for(auto& missile:slow) {
        missile.team=3;missile.id=ETFClassID::CTFProjectile_Rocket;
        missile.state.origin={-620,250,41};missile.state.velocity={100,0,0};
        H::Entities.list.push_back(&missile);
    }
    rocket.state.origin={-600,0,41};H::Entities.list.push_back(&rocket);
    Vars::Misc::Movement::AutoDodge::CalculationBudget.Value=1;
    FakeClock::infoCost=std::chrono::microseconds(20);FakeClock::moveCost=std::chrono::microseconds(20);
    command.forwardmove=command.sidemove=0;command.buttons=0;infoReads=0;refinementOrder.clear();
    F::AutoDodge.Prepare(&player,&command);
    assert(infoReads==32&&refinementOrder.empty());
    assert(totalMoves>=106&&totalMoves<=160&&F::AutoDodge.Controls(&command));checkState();
    FakeClock::infoCost={};FakeClock::moveCost={};
    // With refinement time available the first native physics simulation is
    // the soonest impact, not a nearby slow miss. Interrupting a later
    // refinement must preserve both the completed urgent forecast and the
    // coarse forecasts for the rest of the barrage.
    ++command.command_number;totalMoves=totalTraces=0;F::AutoDodge.Reset();
    Vars::Misc::Movement::AutoDodge::CalculationBudget.Value=8;
    FakeClock::physicsCost=std::chrono::microseconds(30);refinementOrder.clear();
    command.forwardmove=command.sidemove=0;command.buttons=0;
    F::AutoDodge.Prepare(&player,&command);
    assert(refinementOrder.size()==2&&refinementOrder.front()==&rocket);
    assert(F::AutoDodge.Controls(&command));checkState();
    // Reducing the configurable budget reduces refinement work; it does not
    // remove the mandatory comparison or the urgent projectile.
    ++command.command_number;totalMoves=totalTraces=0;F::AutoDodge.Reset();
    Vars::Misc::Movement::AutoDodge::CalculationBudget.Value=1;refinementOrder.clear();
    command.forwardmove=command.sidemove=0;command.buttons=0;
    F::AutoDodge.Prepare(&player,&command);
    assert(refinementOrder.size()==1&&refinementOrder.front()==&rocket);
    assert(F::AutoDodge.Controls(&command));checkState();
    FakeClock::physicsCost={};
    F::AutoDodge.Unload();
    std::cout<<"auto dodge controller tests passed\n";
}
'''


CLIFF_CHECKS = r'''
int main() {
    CTFPlayer player,rocket;CUserCmd command;
    G::CurrentUserCmd=G::LastUserCmd=&command;
    auto setup=[&] {
        F::AutoDodge.Reset();++command.command_number;
        command.forwardmove=command.sidemove=0;command.buttons=IN_ATTACK;
        command.weaponselect=3;command.viewangles={};
        player.state={};player.state.ground=true;
        rocket.team=3;rocket.id=ETFClassID::CTFProjectile_Rocket;
        rocket.state.origin={-600,0,41};rocket.state.velocity={1100,0,0};
        H::Entities.list={&rocket};
        totalMoves=totalTraces=jumpProbes=fallProbes=0;brakeFriction=.7f;
        Vars::Misc::Movement::AutoDodge::PredictionTime.Value=.8f;
        Vars::Misc::Movement::AutoDodge::CalculationBudget.Value=4;
        FakeClock::traceCost=FakeClock::moveCost=FakeClock::physicsCost={};
    };
    auto check=[&](State original) {
        assert(std::memcmp(&player.state,&original,sizeof(State))==0);
        assert(!I::Prediction->m_bInPrediction&&!I::MoveHelper->host);
        assert(command.buttons&IN_ATTACK);assert(command.weaponselect==3);
        assert(totalMoves<=2048&&totalTraces<=4096);
    };
    // Preferred escape heads toward the cliff: pick the other side, keep a
    // setback and disable all automatic jumps while within the edge margin.
    setup();groundHeight=[](float,float y){return y<40?0.f:std::nanf("");};
    State original=player.state;
    F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command)&&WorldWish(command).y<0);
    assert(!(command.buttons&IN_JUMP)&&jumpProbes==0);check(original);
    // An unsafe original route from another helper remains a comparator,
    // but cannot beat a verified retreat simply by avoiding more projectiles.
    setup();command.sidemove=-450;
    original=player.state;F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command)&&WorldWish(command).y<=0&&jumpProbes==0);check(original);
    // A far-side platform must not make a jump across the middle gap eligible.
    setup();groundHeight=[](float,float y){return y<40||y>120?0.f:std::nanf("");};
    original=player.state;F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command)&&WorldWish(command).y<0&&jumpProbes==0);check(original);
    // A narrow bridge allows movement along it, even though the surrounding
    // edge probes disable jumps. It must not be treated as a solid wall.
    setup();groundHeight=[](float,float y){return std::abs(y)<30?0.f:std::nanf("");};
    rocket.state.origin={0,-600,41};rocket.state.velocity={0,1100,0};
    original=player.state;F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command)&&std::abs(WorldWish(command).x)>1);
    assert(std::abs(WorldWish(command).y)<1&&jumpProbes==0);check(original);
    // Timer expiry does not bypass terrain checks or permit the unsafe side.
    setup();groundHeight=[](float,float y){return y<40?0.f:std::nanf("");};
    FakeClock::traceCost=std::chrono::microseconds(500);
    original=player.state;F::AutoDodge.Prepare(&player,&command);
    assert(F::AutoDodge.Controls(&command)&&WorldWish(command).y<0&&jumpProbes==0);check(original);
    // A 0.2-second prefix ends on the platform, but high inertia on slippery
    // ground carries alternatives off it during braking. Reject those paths.
    setup();groundHeight=[](float,float y){return y<130?0.f:std::nanf("");};
    Vars::Misc::Movement::AutoDodge::PredictionTime.Value=.2f;
    player.state.velocity={0,300,0};brakeFriction=.99f;
    rocket.state.origin={-80,0,41};
    original=player.state;F::AutoDodge.Prepare(&player,&command);
    assert(fallProbes>0&&!F::AutoDodge.Controls(&command));check(original);
    F::AutoDodge.Unload();
    std::cout<<"auto dodge ledge and braking tests passed\n";
}
'''


def fake_engine():
    stub = STUB.replace('float LengthSqr()', '''
    Vec3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    Vec3 operator+(Vec3 v)const{return {x+v.x,y+v.y,z+v.z};}
    Vec3 operator-(Vec3 v)const{return {x-v.x,y-v.y,z-v.z};}
    Vec3 operator-()const{return {-x,-y,-z};}
    Vec3 operator*(float v)const{return {x*v,y*v,z*v};}
    Vec3& operator+=(Vec3 v){*this=*this+v;return *this;}
    float Dot(Vec3 v)const{return x*v.x+y*v.y+z*v.z;}
    float Length()const{return std::sqrt(LengthSqr());}
    float Length2DSqr()const{return x*x+y*y;}
    float Length2D()const{return std::sqrt(Length2DSqr());}
    float LengthSqr()''')
    stub = stub.replace('int buttons=0,weaponselect=0,impulse=0;float forwardmove=0;',
                        'int command_number=1,buttons=0,weaponselect=0,impulse=0;float forwardmove=0,sidemove=0,upmove=0;')
    stub = stub.replace('struct datamap_t {};', 'struct datamap_t {typedescription_t* dataDesc=nullptr;int dataNumFields=0;datamap_t* baseMap=nullptr;};')
    stub = stub.replace('    State state;', '    State state;\n' + PLAYER)
    stub = stub.replace('        player=m_pTFPlayer=p;', '''
        FakeClock::elapsed+=FakeClock::moveCost;++totalMoves;
        if(move->buttons&IN_JUMP) ++jumpProbes;
        if(move->forward==13&&!p->command->sidemove) ++prerollMoves;
        player=m_pTFPlayer=p;''')
    stub = stub.replace('p->state.origin.x+=p->state.velocity.x*TICK_INTERVAL;', '''
        p->state.origin.x+=p->state.velocity.x*TICK_INTERVAL;
        p->state.velocity.y-=p->command->sidemove*.01f;
        p->state.origin.y+=p->state.velocity.y*TICK_INTERVAL;
        float floor=groundHeight(p->state.origin.x,p->state.origin.y);
        if(p->state.ground&&(!std::isfinite(floor)||floor<p->state.origin.z-18)) {p->state.ground=false;++fallProbes;}
        if(p->state.ground&&!move->forward&&!p->command->sidemove) {
            p->state.velocity.x*=brakeFriction;p->state.velocity.y*=brakeFriction;
        }
        if(!p->state.ground) p->state.velocity.z-=800*TICK_INTERVAL;''')
    stub = stub.replace('p->state.origin.z+=p->state.velocity.z*TICK_INTERVAL;', '''
        p->state.origin.z+=p->state.velocity.z*TICK_INTERVAL;
        if(std::isfinite(floor)&&p->state.origin.z<=floor&&p->state.velocity.z<=0) {
            p->state.origin.z=floor;p->state.velocity.z=0;p->state.ground=true;
        }''')
    return PREFIX + stub + ADAPTERS


class ControllerTest(unittest.TestCase):
    def compile_production(self, checks):
        folder = ROOT / 'SixBox/src/Features/Misc/AutoDodge'
        includes_removed = lambda text: '\n'.join(line for line in text.splitlines()
                                                    if not line.startswith(('#include', '#pragma')))
        declaration = (MOVEMENT / 'MovementSimulation.h').read_text().split('class CScopedLocalMovement\n', 1)[1]
        declaration = 'class CScopedLocalMovement\n' + declaration
        move = (MOVEMENT / 'MovementSimulation.cpp').read_text()
        move = move[move.index('CScopedLocalMovement::CScopedLocalMovement('):move.index('bool CMovementSimulation::Store(')]
        source = (f'#include "{folder / "DodgePolicy.h"}"\n' + fake_engine() + declaration +
                  includes_removed((folder / 'AutoDodge.h').read_text()).replace('Dodge::TimeBudget<>', 'Dodge::TimeBudget<FakeClock>') + move +
                  includes_removed((folder / 'AutoDodge.cpp').read_text()) + checks)
        with tempfile.TemporaryDirectory(prefix='sixbox-dodge-controller-') as output:
            cpp = Path(output) / 'controller.cpp'
            exe = Path(output) / 'controller'
            cpp.write_text(source)
            subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_production_controller(self):
        self.compile_production(CHECKS)

    def test_ledge_safety(self):
        self.compile_production(CLIFF_CHECKS)


if __name__ == '__main__':
    unittest.main()
