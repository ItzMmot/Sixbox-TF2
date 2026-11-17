"""Run the production sticky-jump controller against a command-driven fake engine.

Only route/shot planning and logging are replaced. Entity classification, observed
bombs, charge/release, arming, movement buttons and phase transitions use production
code. This catches practice bombs disappearing before the jump policy sees them.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FEATURE = ROOT / 'SixBox/src/Features/Aimbot/AutoStickyJump'

STUB = r'''
#include <array>
#include <cassert>
#include <cfloat>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
constexpr float TICK_INTERVAL=1.f/66.f;
#define TICKS_TO_TIME(n) ((n)*TICK_INTERVAL)
#define ADD_FEATURE(Type,Name) namespace F {inline Type Name;}
constexpr int IN_ATTACK=1,IN_JUMP=2,IN_DUCK=4,IN_ATTACK2=2048,IN_RELOAD=8192;
constexpr int TF_CLASS_DEMOMAN=4,TF_WEAPON_PIPEBOMBLAUNCHER=24,MOVETYPE_WALK=2;
constexpr int TF_COND_RUNE_HASTE=1,TF_COND_RUNE_KING=2,TF_COND_KING_BUFFED=3,TF_COND_BLASTJUMPING=4;
constexpr int TF_DETONATE_MODE_DOT=1,EntityLocal=1;
constexpr int TF_GL_MODE_REGULAR=0,TF_GL_MODE_REMOTE_DETONATE=1,TF_GL_MODE_REMOTE_DETONATE_PRACTICE=2,TF_GL_MODE_CANNONBALL=3;
constexpr int MASK_SOLID=1,CONTENTS_GRATE=8;
uint64_t GetTickCount64(){return 1;}
struct Vec3 {
    float x=0,y=0,z=0;
    Vec3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    Vec3 operator+(Vec3 v)const{return {x+v.x,y+v.y,z+v.z};}
    Vec3 operator-(Vec3 v)const{return {x-v.x,y-v.y,z-v.z};}
    Vec3 operator*(float s)const{return {x*s,y*s,z*s};}
    Vec3 operator/(float s)const{return *this*(1.f/s);}
    Vec3& operator+=(Vec3 v){*this=*this+v;return *this;}
    Vec3& operator/=(float s){*this=*this/s;return *this;}
    float Dot(Vec3 v)const{return x*v.x+y*v.y+z*v.z;}
    float LengthSqr()const{return Dot(*this);}
    float Length()const{return std::sqrt(LengthSqr());}
    float Length2DSqr()const{return x*x+y*y;}
    float Length2D()const{return std::sqrt(Length2DSqr());}
    float DistToSqr(Vec3 v)const{return (*this-v).LengthSqr();}
    float DistTo(Vec3 v)const{return (*this-v).Length();}
    bool IsZero()const{return LengthSqr()<1e-8f;}
    void Normalize(){if(!IsZero()) *this/=Length();}
    Vec3 Normalized2D()const{return Length2D()>0?Vec3{x,y,0}/Length2D():Vec3{};}
};
namespace Math {
    void AngleVectors(Vec3 a,Vec3* f=nullptr,Vec3* r=nullptr){
        const float yaw=a.y*.01745329252f;
        if(f) *f={std::cos(yaw),std::sin(yaw),0};
        if(r) *r={std::sin(yaw),-std::cos(yaw),0};
    }
    Vec3 CalcAngle(Vec3 a,Vec3 b){Vec3 v=b-a;return {-std::atan2(v.z,v.Length2D())*57.2957795f,std::atan2(v.y,v.x)*57.2957795f,0};}
    void ClampAngles(Vec3&){}
}
template<class T>struct CHandle {
    T* value=nullptr;T* Get()const{return value;}void Set(T* p){value=p;}void Term(){value=nullptr;}
    int GetEntryIndex()const{return value?value->id:0;}
};
struct CUserCmd {int command_number=0,buttons=0,weaponselect=0;Vec3 viewangles;float forwardmove=0,sidemove=0;};
struct CTFWeaponBase {
    int clip=8,count=0,detonate=0;float charge=0,next=0;
    template<class T>T* As(){return static_cast<T*>(this);}
    int GetWeaponID(){return TF_WEAPON_PIPEBOMBLAUNCHER;}
    int m_iClip1(){return clip;}int m_iPipebombCount(){return count;}
    int m_iPrimaryAmmoType(){return 0;}int GetAmmoPerShot(){return 1;}
    int GetDetonateType(){return detonate;}float GetFireRate(){return .6f;}
    float m_flChargeBeginTime(){return charge;}float m_flNextPrimaryAttack(){return next;}
    bool HasPrimaryAmmoForShot(){return clip>0;}
};
using CTFPipebombLauncher=CTFWeaponBase;
struct CTFPlayer;
struct CCollisionProperty {CTFPlayer* player;void CalcNearestPoint(Vec3,Vec3*);};
struct CTFPlayer {
    int id=1,tick=6600;bool ground=true,blasted=false;
    Vec3 origin,velocity,mins{-24,-24,0},maxs{24,24,82};
    CCollisionProperty collision{this};
    int m_nTickBase(){return tick;}int m_iSpawnCounter(){return 1;}int m_iClass(){return TF_CLASS_DEMOMAN;}
    bool IsAlive(){return true;}bool IsAGhost(){return false;}bool CanAttack(){return true;}
    bool IsSwimming(){return false;}bool IsTaunting(){return false;}bool IsDucking(){return false;}
    bool IsOnGround(){return ground;}bool InCond(int cond){return cond==TF_COND_BLASTJUMPING&&blasted;}
    int m_MoveType(){return MOVETYPE_WALK;}int GetAmmoCount(int){return 24;}
    Vec3 m_vecOrigin(){return origin;}Vec3 m_vecVelocity(){return velocity;}
    Vec3 m_vecMins(){return mins;}Vec3 m_vecMaxs(){return maxs;}
    Vec3 GetShootPos(){return origin+Vec3{0,0,68};}float m_flModelScale(){return 1;}
    CCollisionProperty* GetCollideable(){return &collision;}
};
void CCollisionProperty::CalcNearestPoint(Vec3 point,Vec3* out){
    const Vec3 v=point-player->origin;
    *out=player->origin+Vec3{std::clamp(v.x,-24.f,24.f),std::clamp(v.y,-24.f,24.f),std::clamp(v.z,0.f,player->maxs.z)};
}
struct CTFGrenadePipebombProjectile {
    int id=0,type=TF_GL_MODE_REMOTE_DETONATE;float created=0;bool touched=false;
    Vec3 origin;CHandle<CTFWeaponBase> launcher;CHandle<CTFPlayer> owner;
    template<class T>T* As(){return static_cast<T*>(this);}
    struct Ref {int value;int ToInt()const{return value;}};
    Ref GetRefEHandle(){return {id};}CHandle<CTFWeaponBase> m_hOriginalLauncher(){return launcher;}
    CHandle<CTFPlayer> m_hThrower(){return owner;}
    Vec3 m_vecOrigin(){return origin;}Vec3 GetAbsVelocity(){return {900,0,200};}
    int m_iType(){return type;}bool m_bTouched(){return touched;}float m_flCreationTime(){return created;}
    float GetDamageRadius(){return 146;}bool HasStickyEffects();
};
namespace G {CUserCmd OriginalCmd;bool SilentAngles=false,CanPrimaryAttack=true;int Attacking=0;}
namespace EntityEnum {constexpr int LocalStickies=EntityLocal;}
namespace H {
    struct Entities_t {
        std::vector<CTFGrenadePipebombProjectile*> world,local;
        const auto& GetGroup(int){return local;}void Refresh(int nLocalIndex);
    } Entities;
    struct ConVar {float GetFloat(){return .8f;}};
    struct ConVars_t {ConVar value;ConVar* FindVar(const char*){return &value;}} ConVars;
}
namespace I {
    struct Globals_t {float curtime=100;} globals;auto* GlobalVars=&globals;
    struct Engine_t {bool IsPlayingDemo(){return false;}void SetViewAngles(Vec3){}} engine;auto* EngineClient=&engine;
    struct Gui_t {bool IsGameUIVisible(){return false;}} gui;auto* EngineVGui=&gui;
    struct Client_t {bool IsPaused(){return false;}} client;auto* ClientState=&client;
    struct Movement_t {Vec3 GetPlayerMaxs(bool){return {24,24,62};}} movement;auto* GameMovement=&movement;
}
namespace F {
    struct Menu_t {bool m_bIsOpen=false;} Menu;
    struct Ticks_t {
        bool m_bShifting=false;int burst=0;
        void CancelStickyJumpBurst(){burst=0;}
        int AvailableStickyJumpTicks(){return 22;}
        bool BeginStickyJumpBurst(int n){burst=n;return true;}
    } Ticks;
}
namespace Vars::Misc::Movement::AutoStickyJump {
    template<class T>struct Var{T Value;};
    Var<bool> Enabled{true},CoachIntegration{true},AutoWalk{true},FlushFirst{false},PreserveTraps{true},Doubletap{false},Repeat{false};
    Var<int> Stickies{1},Mode{0},Rotation{0};Var<float> RotationSpeed{100};
    namespace ModeEnum {constexpr int Vertical=1;}
    namespace RotationEnum {constexpr int Silent=1,Legit=2,SilentLegit=3;}
}
namespace SDK {
    float AttribHookValue(float v,const char*,CTFWeaponBase*){return v;}float GetGravity(){return 800;}
    bool VisPosCollideable(CTFGrenadePipebombProjectile*,CTFPlayer*,Vec3,Vec3,int){return true;}
}
'''

PLANNING = r'''
bool CAutoStickyJump::Plan(CTFPlayer* local,CTFWeaponBase*,CUserCmd*) {
    m_plan.wish={1,0,0};m_plan.blast=local->origin+Vec3{360,0,5};
    m_plan.takeoff=local->origin+Vec3{420,0,0};m_plan.valid=true;
    m_plan.wanted=Vars::Misc::Movement::AutoStickyJump::Stickies.Value;
    m_mode=Vars::Misc::Movement::AutoStickyJump::Mode.Value;
    m_started=TICKS_TO_TIME(local->tick);m_lastRelease=m_started;m_angles={};
    m_baseline.clear();m_firstSeen.clear();
    for(auto* entity:H::Entities.GetGroup(EntityEnum::LocalStickies)) m_baseline.insert(entity->GetRefEHandle().ToInt());
    Stage(StickyJump::Phase::Shoot,m_started);return true;
}
bool CAutoStickyJump::Shot(CTFPlayer*,CTFWeaponBase*,const Vec3&,Vec3& angle,Display*,bool){angle={};return true;}
bool CAutoStickyJump::Route(CTFPlayer*,const Vec3& to,Display* display){if(display) display->takeoff=to;return true;}
void CAutoStickyJump::Publish(float remaining,const char* status) {
    m_plan.active=true;m_plan.phase=m_phase;m_plan.armRemaining=remaining;
    if(status) m_reason=status;
    m_plan.status=m_reason?m_reason:StickyJump::Label(m_phase);m_display.Set(m_plan);
}
'''

CHECKS = r'''
void run(int type,int wanted,int mode,int rotation,bool autoWalk,bool burst,bool flush) {
    using namespace Vars::Misc::Movement::AutoStickyJump;
    F::AutoStickyJump.Reset();H::Entities.world.clear();H::Entities.local.clear();
    Stickies.Value=wanted;Mode.Value=mode;Rotation.Value=rotation;AutoWalk.Value=autoWalk;
    Doubletap.Value=burst;FlushFirst.Value=flush;
    CTFPlayer player;CTFWeaponBase weapon;
    std::array<CTFGrenadePipebombProjectile,4> bombs{};
    int fired=0,detonated=0,jumped=0;bool rewound=false,observed=false;
    if(flush) {
        auto& old=bombs[3];old.id=99;old.type=type;old.created=99;old.touched=true;
        old.launcher.Set(&weapon);old.owner.Set(&player);old.origin={-10,0,5};
        H::Entities.world.push_back(&old);weapon.count=1;
    }
    for(int command=1;command<660;++command,++player.tick) {
        if(burst&&F::Ticks.burst&&!rewound) {player.tick-=F::Ticks.burst;rewound=true;}
        const float now=TICKS_TO_TIME(player.tick);I::GlobalVars->curtime=now;
        for(auto* bomb:H::Entities.world) if(now-bomb->created>.25f) bomb->touched=true;
        H::Entities.Refresh(player.id);
        CUserCmd cmd;cmd.command_number=command;cmd.forwardmove=autoWalk?0:450;
        G::OriginalCmd=cmd;G::CanPrimaryAttack=now>=weapon.next;
        F::AutoStickyJump.Prepare(&player,&weapon,&cmd);
        const auto display=F::AutoStickyJump.Snapshot();observed|=display.count==wanted;
        if(display.phase==StickyJump::Phase::Failed) {
            std::cerr<<"failed type="<<type<<" wanted="<<wanted<<" mode="<<mode<<" rotation="<<rotation
                     <<" walk="<<autoWalk<<" burst="<<burst<<" flush="<<flush<<" status="<<display.status<<'\n';
            assert(false);
        }
        // Late helpers may edit the command: Finalize must restore its buttons.
        const int prepared=cmd.buttons;cmd.buttons=IN_RELOAD;
        F::AutoStickyJump.Finalize(&player,&cmd);
        if(F::AutoStickyJump.Controls(&cmd)) assert(cmd.buttons==prepared);
        if(cmd.buttons&IN_ATTACK) {if(weapon.charge<=0&&now>=weapon.next) weapon.charge=now;}
        else if(weapon.charge>0) {
            auto& bomb=bombs[fired++];bomb.id=fired; bomb.type=type;bomb.created=now;
            bomb.origin=display.blast;bomb.launcher.Set(&weapon);bomb.owner.Set(&player);
            H::Entities.world.push_back(&bomb);++weapon.count;--weapon.clip;
            weapon.charge=0;weapon.next=now+.6f;
        }
        if((cmd.buttons&IN_JUMP)&&player.ground) {
            assert(fired==wanted);assert(cmd.buttons&IN_DUCK);++jumped;
            player.ground=false;player.velocity.z=std::sqrt(800.f*90.f);player.maxs.z=62;
        }
        if(cmd.buttons&IN_ATTACK2) {
            if(!flush||fired) assert(jumped&&!player.ground);
            for(auto* bomb:H::Entities.world) assert(StickyJump::Armed(now,bomb->created,.8f,TICK_INTERVAL));
            if(fired) ++detonated;
            H::Entities.world.clear();weapon.count=0;player.blasted=fired>0;
        }
        Vec3 forward,right;Math::AngleVectors(cmd.viewangles,&forward,&right);
        const Vec3 wish=forward*cmd.forwardmove+right*cmd.sidemove;
        if(player.ground) player.velocity=wish.Normalized2D()*280;
        else player.velocity.z-=800*TICK_INTERVAL;
        player.origin+=player.velocity*TICK_INTERVAL;
        if(display.phase==StickyJump::Phase::Air) {
            assert(fired==wanted&&jumped==1&&detonated==1&&observed);
            assert(!burst||rewound);return;
        }
    }
    std::cerr<<"no launch type="<<type<<" wanted="<<wanted<<" phase="<<int(F::AutoStickyJump.Snapshot().phase)<<'\n';
    assert(false);
}
int main() {
    // Practice pipes must reach the same local group and controller as stock.
    // Grenades, cannonballs and another player's stickies must stay out.
    CTFPlayer owner,other;other.id=2;
    std::array<CTFGrenadePipebombProjectile,5> classification{};
    for(int i=0;i<5;++i) {
        classification[i].type=i<4?i:TF_GL_MODE_REMOTE_DETONATE;
        classification[i].owner.Set(i<4?&owner:&other);H::Entities.world.push_back(&classification[i]);
    }
    H::Entities.Refresh(owner.id);assert(H::Entities.local.size()==2);
    // Preserve existing traps must block a practice bomb just like stock.
    for(auto* bomb:H::Entities.local) {
        CTFWeaponBase weapon;bomb->launcher.Set(&weapon);
        F::AutoStickyJump.Reset();CUserCmd cmd;cmd.command_number=1;G::OriginalCmd=cmd;
        F::AutoStickyJump.Prepare(&owner,&weapon,&cmd);
        assert(!F::AutoStickyJump.Controls(&cmd));
        assert(F::AutoStickyJump.Snapshot().phase==StickyJump::Phase::Plan);
        assert(!(cmd.buttons&(IN_ATTACK|IN_ATTACK2|IN_JUMP)));
    }
    for(int type:{TF_GL_MODE_REMOTE_DETONATE,TF_GL_MODE_REMOTE_DETONATE_PRACTICE})
    for(int wanted:{1,2}) for(int mode:{0,1}) for(int rotation:{0,1,2,3})
    for(bool walk:{false,true}) {
        run(type,wanted,mode,rotation,walk,false,false);
        run(type,wanted,mode,rotation,walk,false,true);
        if(wanted==2) run(type,wanted,mode,rotation,walk,true,false);
    }
    std::cout<<"Sticky controller: stock/practice discovery, one/two shots, all modes, flush and tickbase rewind passed\n";
}
'''


def function(source, signature):
    """Extract one exact production function without copying its implementation."""
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def production_source():
    includes_removed = lambda s: '\n'.join(line for line in s.splitlines()
                                          if not line.startswith(('#include', '#pragma')))
    source = (FEATURE / 'AutoStickyJump.cpp').read_text()
    for signature in ('bool CAutoStickyJump::Plan(', 'bool CAutoStickyJump::Shot(',
                      'bool CAutoStickyJump::Route(', 'void CAutoStickyJump::Publish(', 'bool Floor('):
        source = source.replace(function(source, signature), '')
    # Exercise the actual shared entity-cache predicate as well as Prepare's filter.
    entities = (ROOT / 'SixBox/src/SDK/Helpers/Entities/Entities.cpp').read_text()
    condition = re.search(r'if \((pPipebomb->m_hThrower\(\).*?)\)\s*\n\s*m_aGroups\[EntityEnum::LocalStickies\]', entities)
    assert condition, 'LocalStickies classification not found'
    classification = ('void H::Entities_t::Refresh(int nLocalIndex) {local.clear();'
                      'for(auto* pPipebomb:world) if(' + condition[1] + ') local.push_back(pPipebomb);}')
    projectile = (ROOT / 'SixBox/src/SDK/Definitions/Main/CBaseProjectile.cpp').read_text()
    return ('\n'.join(f'#include "{path}"' for path in (
        FEATURE / 'StickyJumpPolicy.h', FEATURE.parent / 'AutoRocketJump/JumpPolicy.h',
        FEATURE.parent / 'AutoTrolldier/Rotation.h', ROOT / 'SixBox/src/Features/ImGui/IndicatorCache.h'))
        + STUB + function(projectile, 'bool CTFGrenadePipebombProjectile::HasStickyEffects(')
        + classification + includes_removed((FEATURE / 'AutoStickyJump.h').read_text())
        + includes_removed(source) + PLANNING + CHECKS)


class ControllerTest(unittest.TestCase):
    def test_observed_bombs_reach_jump_and_detonation(self):
        with tempfile.TemporaryDirectory(prefix='sixbox-sticky-controller-') as output:
            cpp = Path(output) / 'controller.cpp'
            exe = Path(output) / 'controller'
            cpp.write_text(production_source())
            subprocess.run(['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
