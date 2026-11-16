"""Run the production local-command probe against a deterministic fake engine.

The fake is deliberately movement-only: this verifies transaction boundaries and
command semantics, not TF2 physics or the Windows ABI. No client SDK is needed.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MOVEMENT = ROOT / 'SixBox/src/Features/Simulation/MovementSimulation'

STUB = r'''
#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <cassert>
#include <iostream>
using byte = unsigned char;
constexpr int PC_EVERYTHING=0, PC_DATA_PACKED=1, PC_DATA_NORMAL=2;
constexpr int IN_ATTACK=1, IN_JUMP=2, IN_DUCK=4, IN_ATTACK2=2048, IN_ATTACK3=1<<25;
constexpr float TICK_INTERVAL=.015f;
#define TICKS_TO_TIME(t) ((t)*TICK_INTERVAL)
struct Vec3 {
    float x=0,y=0,z=0;
    float LengthSqr() const {return x*x+y*y+z*z;}
};
struct CUserCmd {int buttons=0,weaponselect=0,impulse=0;float forwardmove=0;Vec3 viewangles;};
struct CGlobalVarsBase {int tickcount=500;float curtime=7.5f,frametime=.1f;};
struct datamap_t {};
struct State {
    Vec3 origin,velocity;
    bool ducked=false,ground=true;
    float duckTimer=.137f;
    int buttons=0,lastButtons=0,tickBase=80;
};
struct CTFPlayer {
    State state;
    Vec3 absolute,absoluteVelocity;
    CUserCmd* command=nullptr;
    datamap_t map;
    bool valid=true;
    datamap_t* GetPredDescMap() {return valid?&map:nullptr;}
    size_t GetIntermediateDataSize() {return sizeof(State);}
    CUserCmd*& m_pCurrentCommand() {return command;}
    Vec3 GetAbsOrigin() {return absolute;}
    Vec3 GetAbsVelocity() {return absoluteVelocity;}
    void SetAbsOrigin(Vec3 v) {absolute=v;}
    void SetAbsVelocity(Vec3 v) {absoluteVelocity=v;}
    Vec3& m_vecOrigin() {return state.origin;}
    Vec3& m_vecVelocity() {return state.velocity;}
    int& m_nTickBase() {return state.tickBase;}
    int& m_nButtons() {return state.buttons;}
    int& m_afButtonLast() {return state.lastButtons;}
    int entindex() {return 1;}
};
struct CPredictionCopy {
    int type;void* to;int toType;const void* from;int fromType;
    void TransferData(const char*,int,datamap_t*) {std::memcpy(to,from,sizeof(State));}
};
struct CMoveData {int m_nOldButtons=0,buttons=0;float forward=0;};
struct MoveHelper {CTFPlayer* host=nullptr;void SetHost(CTFPlayer* p) {host=p;}};
struct Prediction {
    bool m_bInPrediction=false,m_bFirstTimePredicted=true;
    Vec3 angles{15,60,0};
    void GetLocalViewAngles(Vec3& v) {v=angles;}
    void SetLocalViewAngles(Vec3& v) {angles=v;}
    void SetupMove(CTFPlayer*,CUserCmd* cmd,MoveHelper*,CMoveData* move) {
        assert(!(cmd->buttons&(IN_ATTACK|IN_ATTACK2|IN_ATTACK3)));
        assert(!cmd->weaponselect&&!cmd->impulse);
        move->buttons=cmd->buttons;move->forward=cmd->forwardmove;angles=cmd->viewangles;
    }
    void FinishMove(CTFPlayer*,CUserCmd*,CMoveData*) {}
};
struct CTFGameMovement {
    virtual ~CTFGameMovement()=default;
    CTFPlayer* player=nullptr;
    CTFPlayer* m_pTFPlayer=nullptr;
    CMoveData* mv=nullptr;
    int cache=123;
    bool fail=false;
    int jumps=0;
    void ProcessMovement(CTFPlayer* p,CMoveData* move) {
        player=m_pTFPlayer=p;mv=move;++cache;
        if(fail) {p->state.origin.x=std::nanf("");return;}
        if(p->state.ground&&(move->buttons&IN_JUMP)&&!(move->m_nOldButtons&IN_JUMP)) {
            p->state.velocity.z=289;p->state.ground=false;++jumps;
        }
        p->state.velocity.x+=move->forward*.01f;
        p->state.origin.x+=p->state.velocity.x*TICK_INTERVAL;
        p->state.origin.z+=p->state.velocity.z*TICK_INTERVAL;
        p->state.ducked=move->buttons&IN_DUCK;p->state.duckTimer+=.015f;
        p->absolute=p->state.origin;p->absoluteVelocity=p->state.velocity;
    }
};
namespace I {
    CGlobalVarsBase clock;CGlobalVarsBase* GlobalVars=&clock;
    ::Prediction prediction;::Prediction* Prediction=&prediction;
    ::MoveHelper helper;::MoveHelper* MoveHelper=&helper;
    CTFGameMovement movement;CTFGameMovement* GameMovement=&movement;
}
namespace F {
    struct EnginePrediction_t {
        State base;bool valid=true;
        bool RestoreSimulationBase(CTFPlayer* p) const {if(!valid)return false;p->state=base;return true;}
    } EnginePrediction;
}
'''

CHECKS = r'''
int main() {
    CTFPlayer player;player.state.origin={10,20,30};player.state.velocity={140,-20,4};
    player.state.ducked=true;player.state.buttons=IN_DUCK;player.state.lastButtons=IN_ATTACK;
    player.absolute={11,21,31};player.absoluteVelocity={141,-21,5};
    CUserCmd previous;player.command=&previous;
    State original=player.state;
    CTFPlayer sentinel;CMoveData oldMove;
    I::GameMovement->player=I::GameMovement->m_pTFPlayer=&sentinel;I::GameMovement->mv=&oldMove;
    auto checkRestored=[&] {
        assert(std::memcmp(&player.state,&original,sizeof(State))==0);
        assert(player.absolute.x==11&&player.absoluteVelocity.x==141&&player.command==&previous);
        assert(I::GameMovement->player==&sentinel&&I::GameMovement->m_pTFPlayer==&sentinel);
        assert(I::GameMovement->mv==&oldMove&&I::GameMovement->cache==123&&I::GameMovement->jumps==0);
        assert(!I::Prediction->m_bInPrediction&&I::Prediction->m_bFirstTimePredicted);
        assert(I::Prediction->angles.y==60&&I::GlobalVars->tickcount==500&&I::GlobalVars->frametime==.1f);
        assert(I::MoveHelper->host==nullptr);
    };
    {
        CScopedLocalMovement probe(&player,false);assert(probe.Valid());
        assert(player.state.velocity.x==140&&player.state.ducked&&player.state.duckTimer==.137f);
        CUserCmd move;move.buttons=IN_JUMP|IN_ATTACK|IN_ATTACK2|IN_DUCK;
        move.weaponselect=5;move.impulse=7;move.forwardmove=450;
        assert(probe.Step(move));
        assert(player.state.origin.x>10&&player.state.velocity.z==289);
        assert(I::GameMovement->jumps==1);
        // Holding jump while grounded still does not create a repeated press.
        player.state.ground=true;assert(probe.Step(move));assert(I::GameMovement->jumps==1);
        assert(move.weaponselect==5&&move.impulse==7&&(move.buttons&IN_ATTACK));
        probe.Rewind();
        assert(std::memcmp(&player.state,&original,sizeof(State))==0);
        assert(I::GameMovement->cache==123&&I::GameMovement->jumps==0);
        assert(probe.Step(move));
    }
    checkRestored();
    // Reuse a latency prefix without losing the original restoration point,
    // its movement scratch state or the simulation clock/button history.
    {
        CScopedLocalMovement probe(&player,false);
        CUserCmd move;move.forwardmove=100;move.buttons=IN_DUCK;
        assert(probe.Step(move));probe.Checkpoint();State prefix=player.state;
        int cache=I::GameMovement->cache;
        assert(probe.Step(move));State advanced=player.state;int tick=I::GlobalVars->tickcount;
        probe.Rewind();
        assert(std::memcmp(&player.state,&prefix,sizeof(State))==0&&I::GameMovement->cache==cache);
        assert(probe.Step(move));
        assert(std::memcmp(&player.state,&advanced,sizeof(State))==0&&I::GlobalVars->tickcount==tick);
    }
    checkRestored();
    // A late probe uses the pre-command base, and restores the caller's
    // already predicted pose rather than prematurely ending its scope.
    F::EnginePrediction.base=original;F::EnginePrediction.base.origin.x=-50;
    {
        CScopedLocalMovement probe(&player,true);assert(probe.Valid());
        assert(player.state.origin.x==-50);
        CUserCmd move;move.forwardmove=100;assert(probe.Step(move));
        probe.Rewind();assert(player.state.origin.x==-50);
    }
    checkRestored();
    F::EnginePrediction.valid=false;
    {CScopedLocalMovement probe(&player,true);assert(!probe.Valid());}
    checkRestored();
    F::EnginePrediction.valid=true;
    I::GameMovement->fail=true;
    {CScopedLocalMovement probe(&player,false);CUserCmd cmd;assert(!probe.Step(cmd));}
    checkRestored();
    assert(I::GameMovement->fail);
    I::GameMovement->fail=false;
    player.valid=false;
    {CScopedLocalMovement probe(&player,false);assert(!probe.Valid());}
    checkRestored();
    std::cout<<"local command simulation transaction tests passed\n";
}
'''


class LocalCommandTest(unittest.TestCase):
    def test_production_probe(self):
        header = (MOVEMENT / 'MovementSimulation.h').read_text()
        declaration = header[header.index('class CScopedLocalMovement\n'):]
        source = (MOVEMENT / 'MovementSimulation.cpp').read_text()
        implementation = source[source.index('CScopedLocalMovement::CScopedLocalMovement('):
                                source.index('bool CMovementSimulation::Store(')]
        with tempfile.TemporaryDirectory(prefix='sixbox-dodge-movement-') as folder:
            cpp = Path(folder) / 'probe.cpp'
            executable = Path(folder) / 'probe'
            cpp.write_text(STUB + declaration + implementation + CHECKS)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-O2', '-g', str(cpp), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
