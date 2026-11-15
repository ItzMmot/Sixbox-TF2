"""Compile the production event/queue methods against a deterministic engine fixture."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'SixBox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text()
def method(name):
    start = source.index('void '+name+'(')
    brace = source.index('{',start)
    depth = 1
    end = brace+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}')
        end += 1
    return source[start:end]
fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <bit>
#include <xmmintrin.h>
#include <string>
#include <unordered_map>
#define __fastcall
#include "SixBox/src/Features/Visuals/SkinChanger/GoldKillPolicy.h"
namespace FNV1A {
constexpr unsigned Hash32Const(const char* s) { unsigned n=2166136261u;while(*s)n=(n^unsigned(*s++))*16777619u;return n; }
unsigned Hash32(const char* s){return Hash32Const(s);}
}
namespace G { bool Unload=false; }
struct Value { int Value=0; };
namespace Vars::Visuals::SkinChanger { ::Value Enabled{1}; }
namespace Vars::Visuals::Removals { ::Value Ragdolls{0}; }
namespace Vars::Visuals::Effects {
::Value RagdollEffects{0};
namespace RagdollEffectsEnum { constexpr int Burning=1,Electrocuted=2,Ash=4,Dissolve=8,Gold=16,Ice=32; }
}
constexpr int TF_DMG_CUSTOM_GOLD_WRENCH=19;
namespace SDK { bool screenshot=false; bool CleanScreenshot(){return screenshot;} }
struct Engine {
    int GetPlayerForUserID(int id){return id==101?1:id==202?2:id==303?3:0;}
    int GetLocalPlayer(){return 1;}
};
struct Globals {float realtime=10.f;};
namespace I { Engine engine;Engine* EngineClient=&engine; Globals vars;Globals* GlobalVars=&vars; }
struct IGameEvent {
    std::string name="player_death";
    std::unordered_map<std::string,int> ints{{"userid",202},{"attacker",101},{"weaponid",7},{"customkill",2},{"death_flags",0}};
    std::unordered_map<std::string,std::string> strings{{"weapon","knife"}};
    const char* GetName(){return name.c_str();}
    int GetInt(const char* key,int fallback=0){auto it=ints.find(key);return it==ints.end()?fallback:it->second;}
    void SetString(const char* key,const char* val){strings[key]=val;}
};
struct CTFRagdoll {
    int index=2, user=202;bool feign=false,valid=true;
    int calls=0;bool gold=false,gib=true,burning=false,electrocuted=false,ash=false,dissolve=false,ice=false; int custom=2;
    bool& m_bGoldRagdoll(){return gold;}
    bool& m_bGib(){return gib;}
    bool& m_bBurning(){return burning;}
    bool& m_bElectrocuted(){return electrocuted;}
    bool& m_bBecomeAsh(){return ash;}
    bool& m_bDissolving(){return dissolve;}
    bool& m_bIceRagdoll(){return ice;}
    int& m_iDamageCustom(){return custom;}
    int m_iPlayerIndex(){return index;}
    bool m_bFeignDeath(){return feign;}
};
template<class T> struct CHandle {
    T* ptr=nullptr; T* Get(){return ptr&&ptr->valid?ptr:nullptr;} void Set(T* p){ptr=p;}
};
constexpr int MAX_WEAPONS=48;
constexpr int kEconEntityAttrManager=32,kAttrContainerItem=16,kItemViewAttributeList=240,kViewDefIndex=72,kAttrAustralium=2027;
struct CEconItemView {};
struct CAttributeList {float value=0.f;float GetValue(int,float=0.f){return value;}};
template<class T>T& Field(void* p,int offset){return *reinterpret_cast<T*>(uintptr_t(p)+offset);}
struct CTFWeaponBase {
    alignas(16) std::array<unsigned char,512> data={};bool valid=true;
    int GetWeaponID(){return Field<int>(this,0);}
};
struct CTFPlayer {
    std::array<CHandle<CTFWeaponBase>,MAX_WEAPONS> weapons;
    auto& m_hMyWeapons(){return weapons;}
};
struct GoldWeapon {int weaponID=-1;GoldKillPolicy::Kind kind=GoldKillPolicy::Kind::None;};
std::array<GoldWeapon,48> s_goldWeapons;
GoldKillPolicy::Deaths s_goldDeaths;
struct PendingGoldRagdoll { CHandle<CTFRagdoll> entity;void(*create)(void*)=nullptr;float queued=0;};
std::array<PendingGoldRagdoll,129> s_goldPending;
int RagdollUserID(CTFRagdoll* r){return r->user;}
void FinishRagdoll(CTFRagdoll*,void(*)(void*),bool);
void NativeCreate(void* p){auto* r=static_cast<CTFRagdoll*>(p);++r->calls;
if(r->gold){assert(!r->gib&&!r->ice&&!r->ash&&!r->dissolve);assert(r->custom==TF_DMG_CUSTOM_GOLD_WRENCH);}}
struct CSkinChanger {
    void CacheGoldWeapons(CTFPlayer*);
    void GoldKillEvent(IGameEvent*);
    void CreateGoldRagdoll(CTFRagdoll*,void(*)(void*));
    void FlushGoldRagdolls(bool force=false);
};
void Reset(){s_goldWeapons={};s_goldWeapons[0]={7,GoldKillPolicy::Kind::Saxxy};s_goldPending={};s_goldDeaths.Clear();SDK::screenshot=false;G::Unload=false;Vars::Visuals::SkinChanger::Enabled.Value=1;I::vars.realtime=10.f;}
'''
checks = r'''
int main(){
CSkinChanger feature;
// Australium's integer bit 1 must survive SSE DAZ/FTZ; absent attributes stay false.
Reset();CTFPlayer local;CTFWeaponBase weapon;
local.weapons[0].Set(&weapon);
Field<int>(&weapon,0)=7;
auto* view=reinterpret_cast<CEconItemView*>(uintptr_t(&weapon)+kEconEntityAttrManager+kAttrContainerItem);
Field<uint16_t>(view,kViewDefIndex)=205;
auto* attrs=reinterpret_cast<CAttributeList*>(uintptr_t(view)+kItemViewAttributeList);
feature.CacheGoldWeapons(&local);assert(s_goldWeapons[0].kind==GoldKillPolicy::Kind::None);
attrs->value=std::bit_cast<float>(uint32_t{1});
const unsigned oldCSR=_mm_getcsr();_mm_setcsr(oldCSR|0x8040);
feature.CacheGoldWeapons(&local);_mm_setcsr(oldCSR);
assert(s_goldWeapons[0].kind==GoldKillPolicy::Kind::Australium);
for(int def:{423,169,1071}){Field<uint16_t>(view,kViewDefIndex)=uint16_t(def);attrs->value=0.f;feature.CacheGoldWeapons(&local);assert(s_goldWeapons[0].kind!=GoldKillPolicy::Kind::None);}
feature.CacheGoldWeapons(nullptr);assert(s_goldWeapons[0].kind==GoldKillPolicy::Kind::None);
// Both packet orders create exactly one gold corpse; custom damage stays a backstab.
for(bool eventFirst:{false,true}){
Reset();IGameEvent death;CTFRagdoll rag;
if(eventFirst)feature.GoldKillEvent(&death);
feature.CreateGoldRagdoll(&rag,NativeCreate);
if(!eventFirst){assert(rag.calls==0);feature.GoldKillEvent(&death);feature.FlushGoldRagdolls();}
assert(rag.calls==1&&rag.gold);
assert(death.strings["weapon"]=="saxxy"&&death.ints["customkill"]==2);
feature.FlushGoldRagdolls();assert(rag.calls==1);
}
// Other attackers, another weapon, suicides and feigned deaths never get gold.
for(int mode=0;mode<4;++mode){
Reset();IGameEvent death;CTFRagdoll rag;
if(mode==0)death.ints["attacker"]=303;
if(mode==1)death.ints["weaponid"]=9;
if(mode==2)death.ints["attacker"]=202;
if(mode==3){death.ints["death_flags"]=32;rag.feign=true;}
feature.CreateGoldRagdoll(&rag,NativeCreate);feature.GoldKillEvent(&death);feature.FlushGoldRagdolls();
assert(rag.calls==1&&!rag.gold&&death.strings["weapon"]=="knife");
}
// Missing event, repeated callback, expired entity, respawn and screenshot handling.
Reset();CTFRagdoll rag;feature.CreateGoldRagdoll(&rag,NativeCreate);feature.CreateGoldRagdoll(&rag,NativeCreate);
I::vars.realtime+=0.11f;feature.FlushGoldRagdolls();assert(rag.calls==1&&!rag.gold);
Reset();CTFRagdoll removed;feature.CreateGoldRagdoll(&removed,NativeCreate);removed.valid=false;feature.FlushGoldRagdolls();assert(removed.calls==0);
Reset();CTFRagdoll old;feature.CreateGoldRagdoll(&old,NativeCreate);IGameEvent spawn;spawn.name="player_spawn";feature.GoldKillEvent(&spawn);assert(old.calls==1&&!old.gold);
IGameEvent death;feature.GoldKillEvent(&death);feature.FlushGoldRagdolls();assert(old.calls==1);
Reset();CTFRagdoll clean;feature.CreateGoldRagdoll(&clean,NativeCreate);feature.GoldKillEvent(&death);SDK::screenshot=true;feature.FlushGoldRagdolls();assert(clean.calls==1&&!clean.gold);
Reset();CTFRagdoll off;feature.CreateGoldRagdoll(&off,NativeCreate);feature.GoldKillEvent(&death);Vars::Visuals::SkinChanger::Enabled.Value=0;feature.FlushGoldRagdolls();assert(off.calls==1&&!off.gold);
Reset();CTFRagdoll round;feature.CreateGoldRagdoll(&round,NativeCreate);feature.GoldKillEvent(&death);IGameEvent reset;reset.name="teamplay_round_start";feature.GoldKillEvent(&reset);assert(round.calls==1&&!round.gold);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/'gold.cpp'
    exe=Path(directory)/'gold-test'
    cpp.write_text(fixture+'\n'+'\n'.join(method(n) for n in ['FinishRagdoll','CSkinChanger::CacheGoldWeapons','CSkinChanger::GoldKillEvent','CSkinChanger::CreateGoldRagdoll','CSkinChanger::FlushGoldRagdolls'])+checks)
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Gold kill production event/queue regression checks passed')
