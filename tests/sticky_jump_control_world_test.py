"""Exercise the production sticky world adapter with a deterministic engine.

Uses the shared movement transaction fixture, not either sticky controller's
policies. This validates adapter behavior, not live TF2 physics or Windows ABI.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from auto_dodge_movement_test import STUB, MOVEMENT, ROOT


ENTITY = r'''
    int id=1;float created=1;bool touched=true;
    bool attackAllowed=true;float nextAttack=0,nextSecondary=0;
    bool CanAttack(){return attackAllowed;}
    float m_flNextAttack(){return nextAttack;}float m_flNextSecondaryAttack(){return nextSecondary;}
    CTFPlayer* thrower=nullptr;CTFPlayer* launcher=nullptr;
    struct Handle {CTFPlayer* value;CTFPlayer* Get(){return value;}};
    struct Flags {CTFPlayer* player;void operator|=(int){player->state.ground=true;}};
    Flags m_fFlags(){return {this};}
    template<class T> T* As(){return reinterpret_cast<T*>(this);}
    bool IsOnGround(){return state.ground;}bool IsDucking(){return state.ducked;}
    Vec3 m_vecMins(){return {-24,-24,0};}
    Vec3 GetShootPos(){return state.origin+Vec3{0,0,50};}
    Vec3 m_vecMaxs(){return {24,24,state.ducked?62.f:82.f};}
    float GetDamage(bool){return 120;}int GetDamageType(){return 0;}
    bool HasStickyEffects(){return true;}bool InCond(int){return false;}
    bool m_bTouched(){return touched;}float m_flCreationTime(){return created;}
    Handle m_hThrower(){return {thrower};}Handle m_hLauncher(){return {launcher};}
'''

ADAPTER = r'''
#include <chrono>
using CBaseEntity=CTFPlayer;using CTFWeaponBase=CTFPlayer;
using CTFGrenadePipebombProjectile=CTFPlayer;
constexpr int FL_ONGROUND=1,MASK_PLAYERSOLID=1,MASK_SOLID=2;
constexpr int IN_FORWARD=8,IN_BACK=16,IN_MOVELEFT=512,IN_MOVERIGHT=1024,IN_RELOAD=8192;
constexpr int DMG_RADIUS_MAX=1,DMG_HALF_FALLOFF=2;
constexpr int TF_COND_RUNE_HASTE=1,TF_COND_RUNE_KING=2,TF_COND_KING_BUFFED=3;
#define TIME_TO_TICKS(t) (int(.5f+(t)/TICK_INTERVAL))
namespace EntityEnum {constexpr int LocalStickies=0;}
namespace H {
    struct Inventory {
        std::vector<CTFPlayer*> list;
        const auto& GetGroup(int){return list;}
    } Entities;
}
struct CTraceFilterWorldAndPropsOnly {explicit CTraceFilterWorldAndPropsOnly(CTFPlayer*){}};
struct CTraceFilterCollideable {explicit CTraceFilterCollideable(CTFPlayer*){}};
struct CGameTrace {
    bool startsolid=false,allsolid=false;float fraction=1;Vec3 endpos;
    struct {Vec3 normal{0,0,1};} plane;
    CTFPlayer* m_pEnt=nullptr;
};
bool obstructed=false;
float floorSlope=0, floorEnd=1e9f;
namespace SDK {
    float GetGravity(){return 800;}
    void FixMovement(CUserCmd* command,const Vec3& angles){
        const float old=command->viewangles.y*StickyControl::Pi/180,newYaw=angles.y*StickyControl::Pi/180;
        const float x=command->forwardmove*std::cos(old)+command->sidemove*std::sin(old);
        const float y=command->forwardmove*std::sin(old)-command->sidemove*std::cos(old);
        command->forwardmove=x*std::cos(newYaw)+y*std::sin(newYaw);
        command->sidemove=x*std::sin(newYaw)-y*std::cos(newYaw);
    }
    template<class T> T AttribHookValue(T value,const char*,CTFPlayer*){return value;}
    template<class Filter>
    void TraceHull(Vec3 from,Vec3 to,Vec3,Vec3,int,Filter*,CGameTrace* trace){
        trace->endpos=to;
        if(obstructed){trace->startsolid=true;return;}
        if(from.x>floorEnd&&to.x>floorEnd)return;
        const float fromHeight=from.z-floorSlope*from.x,toHeight=to.z-floorSlope*to.x;
        if(fromHeight<0){trace->startsolid=true;return;}
        if(toHeight<0){
            trace->fraction=fromHeight/(fromHeight-toHeight);
            trace->endpos=from+(to-from)*trace->fraction;
            const float length=std::sqrt(1+floorSlope*floorSlope);
            trace->plane.normal={-floorSlope/length,0,1/length};
        }
    }
}
struct ProjectileInfo {Vec3 m_vHull{1,1,1},origin,velocity,m_vPos;float m_flVelocity=900,m_flGravity=800;};
namespace ProjSimEnum {constexpr int InitCheck=1,NoRandomAngles=2;}
Vec3 lastShotOrigin,lastShotVelocity;
int shotInitializations=0;
struct CProjectileSimulation {
    void* m_pEnv=nullptr;void* m_pBoxCollide=nullptr;void* m_pObj=nullptr;
    void* m_pObjBox=nullptr;void* m_pCurrent=nullptr;
    Vec3 origin,velocity;
    bool GetInfo(CTFPlayer* player,CTFPlayer*,Vec3 angles,ProjectileInfo& info,int,float){
        lastShotOrigin=player->m_vecOrigin();lastShotVelocity=player->m_vecVelocity();
        const float pitch=angles.x*StickyControl::Pi/180,yaw=angles.y*StickyControl::Pi/180;
        info.origin=player->m_vecOrigin()+Vec3{0,0,50};
        info.m_vPos=info.origin;
        const float horizontal=900*std::cos(pitch)+200*std::sin(pitch);
        info.velocity={horizontal*std::cos(yaw),horizontal*std::sin(yaw),-900*std::sin(pitch)+200*std::cos(pitch)};
        return true;
    }
    void GetInfo(CTFPlayer* player,ProjectileInfo& info){info.origin=player->m_vecOrigin();info.velocity=player->m_vecVelocity();}
    bool Initialize(ProjectileInfo& info,bool,bool=false){origin=info.origin;velocity=info.velocity;++shotInitializations;return true;}
    Vec3 GetOrigin(){return origin;}
    void RunTick(ProjectileInfo&,bool){origin=origin+velocity*TICK_INTERVAL;velocity.z-=800*TICK_INTERVAL;}
    void SetupTrace(CTraceFilterCollideable&,int&,CTFPlayer*){}
};
void DestroySimulation(CProjectileSimulation&){}
using namespace StickyControl;
Vec V(Vec3 v){return {v.x,v.y,v.z};}Vec3 E(Vec v){return {v.x,v.y,v.z};}
uint32_t Identity(CTFPlayer* entity){return entity->id;}
float Convar(const char*,float fallback){return fallback;}
'''

CHECKS = r'''
static float Reference(CStickyJumpWorld& world,Frame frame,Request request){
    world.BeginSearch(frame,request);Search search;int cursor=0;
    for(int work=0;work<10000;++work){
        Candidate candidate;auto result=world.NextCandidate(cursor,frame,request,candidate);
        if(result==SampleResult::Done)return search.Maximum();
        if(result==SampleResult::Sample){
            ++cursor;
            search.Add(candidate,request.Wish());
        }
    }
    assert(false);return 0;
}
int main(){
    CUserCmd manual;manual.forwardmove=450;manual.sidemove=-200;manual.upmove=10;
    manual.buttons=IN_FORWARD|IN_MOVELEFT|IN_DUCK;manual.viewangles={12,140,0};
    Output assist;assist.controls=true;
    auto command=Compose(assist,manual);
    assert(command.forwardmove==450&&command.sidemove==-200&&command.upmove==10);
    assert(command.buttons==manual.buttons&&command.viewangles.x==12&&command.viewangles.y==140);
    assist.jump=assist.detonate=true;
    command=Compose(assist,manual);
    assert(command.buttons==(manual.buttons|IN_JUMP|IN_ATTACK2));
    assert(command.forwardmove==450&&command.sidemove==-200&&command.upmove==10);
    assert(command.viewangles.x==12&&command.viewangles.y==140);

    CTFPlayer player,weapon,sticky;
    player.nextAttack=weapon.nextSecondary=100;
    assert(DetonatorAvailable(&player)); // Reload clocks do not disable native detonation.
    player.attackAllowed=false;assert(!DetonatorAvailable(&player));
    player.attackAllowed=true;player.nextAttack=weapon.nextSecondary=0;
    sticky.id=10;sticky.thrower=&player;sticky.launcher=&weapon;
    sticky.state.origin=sticky.absolute={0,0,1};
    player.state.origin=player.absolute={-80,0,0};
    H::Entities.list={&sticky};
    CUserCmd raw;CStickyJumpWorld world;world.Bind(&player,&weapon,raw);
    Frame frame;frame.enabled=frame.eligible=frame.canDetonate=true;
    frame.gameTime=frame.projectileTime=10;frame.interval=TICK_INTERVAL;
    frame.body=ReadBody(&player);frame.grounded=true;frame.bombs=world.Bombs();
    assert(frame.bombs.size()==1);
    const State original=player.state;const auto clock=*I::GlobalVars;
    Request request;request.start=frame.body.origin;
    // The player is behind the bomb, so current movement cannot provide the
    // requested forward boost. That must not erase the directional reference.
    for(auto direction:{Direction::Horizontal,Direction::Diagonal,Direction::Vertical}){
        request.direction=direction;
        assert(Reference(world,frame,request)>0);
        Vec delta;assert(!world.Ready(frame,request,500,delta));
        assert(std::memcmp(&player.state,&original,sizeof(State))==0);
        assert(I::GlobalVars->curtime==clock.curtime);
    }
    player.state.ground=false;player.state.origin.z=player.absolute.z=60;
    frame.body=ReadBody(&player);frame.grounded=false;
    request.direction=Direction::Vertical;
    assert(Reference(world,frame,request)>0); // Activation may be airborne.
    assert(!player.state.ground&&player.state.origin.z==60);

    player.state.ground=true;player.state.origin=player.absolute={100,0,0};
    player.state.velocity=player.absoluteVelocity={150,0,0};
    raw.forwardmove=250;world.Bind(&player,&weapon,raw);
    frame.body=ReadBody(&player);frame.grounded=true;frame.bombs=world.Bombs();
    request.direction=Direction::Diagonal;world.BeginSearch(frame,request);
    Body future;
    {
        CScopedLocalMovement movement(&player,false);CUserCmd command=raw;
        command.buttons=IN_JUMP|IN_DUCK;
        for(int tick=0;tick<8;++tick){
            assert(movement.Step(command));command.buttons&=~IN_JUMP;
        }
        future=ReadBody(&player);
    }
    const float target=BlastImpulse(frame.bombs[0],future,true).Length();
    assert(MeetsTarget(BlastImpulse(frame.bombs[0],future,true),request.Wish(),target));
    Vec immediate;assert(!world.Ready(frame,request,target,immediate));
    Candidate delayed;delayed.jumpTicks=8;
    assert(world.CanBeginJump(frame,request,delayed,target));
    assert(player.state.velocity.x==150&&player.state.origin.x==100&&!player.state.ducked);
    obstructed=true;
    assert(Reference(world,frame,request)==0);
    assert(!world.Ready(frame,request,target,immediate));
    obstructed=false;
    // Keep live movement and blast validation in the production adapter.
    // Use a fixed reference boost to isolate the timed launch and reload gate.
    struct ObservedWorld:CStickyJumpWorld {
        Vec observedDelta;
        int liveProbes=0;
        SampleResult NextCandidate(int cursor,const Frame&,const Request& request,Candidate& candidate){
            if(cursor)return SampleResult::Done;
            candidate.valid=true;candidate.delta=request.Wish()*1000;
            return SampleResult::Sample;
        }
        bool Ready(const Frame& frame,const Request& request,float target,Vec& delta){
            ++liveProbes;
            const bool ready=CStickyJumpWorld::Ready(frame,request,target,delta);
            if(ready)observedDelta=delta;
            return ready;
        }
    };
    for(float armTime:{.2f,.8f}){
        ObservedWorld actual;Controller controller;
        player.state={};player.state.origin=player.absolute={-120,0,0};
        player.state.velocity=player.absoluteVelocity={280,0,0};
        sticky.state.origin=sticky.absolute={0,0,1};sticky.created=1;sticky.touched=true;
        H::Entities.list={&sticky};raw={};raw.forwardmove=450;
        frame={};frame.command=1;frame.weapon=42;frame.wallTime=1;frame.gameTime=frame.projectileTime=10;
        frame.enabled=frame.eligible=frame.canDetonate=frame.grounded=frame.pressed=true;
        Request requested;requested.power=100;
        // Reproduce Assist's reported cooldown stall after the sticky is armed.
        // The clocks stay in the future throughout the complete launch.
        player.nextAttack=weapon.nextSecondary=100;
        bool jumped=false,detonated=false;
        for(int tick=0;tick<120&&!detonated;++tick){
            actual.Bind(&player,&weapon,raw);
            frame.body=ReadBody(&player);frame.grounded=frame.body.grounded;frame.bombs=actual.Bombs();
            frame.bombs[0].armTime=armTime;
            frame.canDetonate=DetonatorAvailable(&player);
            const auto saved=player.state;
            const auto output=controller.Run(frame,requested,actual);
            assert(std::string(controller.Snapshot().status)!="Waiting for detonation cooldown");
            assert(std::memcmp(&saved,&player.state,sizeof(State))==0);
            assert(controller.Busy());
            if(output.jump){jumped=true;assert(frame.body.origin.x<150);}
            detonated=output.detonate;
            const auto command=Compose(output,raw);
            State next;
            {CScopedLocalMovement movement(&player,false);assert(movement.Step(command));next=player.state;}
            player.state=next;player.absolute=next.origin;player.absoluteVelocity=next.velocity;
            ++frame.command;frame.pressed=false;frame.wallTime+=TICK_INTERVAL;
            frame.gameTime+=TICK_INTERVAL;frame.projectileTime+=TICK_INTERVAL;
        }
        assert(jumped&&detonated&&actual.liveProbes>0);
        assert(MeetsTarget(actual.observedDelta,requested.Wish(),controller.Snapshot().target));
        player.state.velocity=player.state.velocity+E(actual.observedDelta);
        player.absoluteVelocity=player.state.velocity;H::Entities.list.clear();
        actual.Bind(&player,&weapon,raw);frame.body=ReadBody(&player);frame.grounded=false;frame.bombs=actual.Bombs();
        assert(!controller.Run(frame,requested,actual).controls&&!controller.Busy());
    }
    std::cout<<"Sticky Jump Control: production detonation assist world checks passed\n";
}
'''


def production_source(stub=STUB):
    stub = stub.replace('float LengthSqr() const', '''
Vec3 operator+(Vec3 b)const{return {x+b.x,y+b.y,z+b.z};}
Vec3 operator-(Vec3 b)const{return {x-b.x,y-b.y,z-b.z};}
Vec3 operator-()const{return {-x,-y,-z};}
Vec3 operator*(float s)const{return {x*s,y*s,z*s};}
float Length()const{return std::sqrt(LengthSqr());}
float LengthSqr() const''')
    stub = stub.replace('float forwardmove=0;', 'float forwardmove=0,sidemove=0,upmove=0;')
    stub = stub.replace('int entindex()', ENTITY + '\n    int entindex()')
    header = (MOVEMENT / 'MovementSimulation.h').read_text()
    declaration = header[header.index('class CScopedLocalMovement\n'):]
    movement = (MOVEMENT / 'MovementSimulation.cpp').read_text()
    implementation = movement[movement.index('CScopedLocalMovement::Snapshot CScopedLocalMovement::Capture('):
                              movement.index('bool CMovementSimulation::Store(')]
    source = (ROOT / 'SixBox/src/Features/Misc/StickyJumpControl/StickyJumpControl.cpp').read_text()
    body_reader = source[source.index('    Body ReadBody('):source.index('    uint32_t Identity(')]
    detonator = source[source.index('    bool DetonatorAvailable('):source.index('    void DestroySimulation(')]
    adapter = source[source.index('class CStickyJumpWorld\n'):
                     source.index('CStickyJumpControl::CStickyJumpControl()')]
    start = source.index('    m_intent=G::OriginalCmd;')
    composition = source[start:source.index('    Apply(local,command);', start)]
    composition = composition.replace('m_intent=G::OriginalCmd;', 'CUserCmd m_intent=raw;', 1)
    composition = ('CUserCmd Compose(const Output& output,const CUserCmd& raw){\n'
                   + composition + '\nreturn m_intent;\n}\n')
    include = '#include "' + str(ROOT / 'SixBox/src/Features/Misc/StickyJumpControl/JumpController.h') + '"\n'
    return include + stub + declaration + implementation + ADAPTER + body_reader + detonator + adapter + composition


class StickyWorldTest(unittest.TestCase):
    def test_production_world_and_command_ownership(self):
        with tempfile.TemporaryDirectory(prefix='sixbox-sticky-world-') as folder:
            cpp = Path(folder) / 'world.cpp'
            executable = Path(folder) / 'world'
            cpp.write_text(production_source() + CHECKS)
            subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', str(cpp), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
