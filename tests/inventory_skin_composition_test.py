"""Run the production weapon editor and inventory compositor in engine order."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'SixBox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text()

def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = r'''
#include "SixBox/src/Features/Visuals/SkinChanger/SkinProfile.h"
#include <cassert>
#include <array>
#include <cstring>
#include <mutex>
constexpr int MAX_WEAPONS=3,kLoadoutSlots=19,TF_COND_TAUNTING=7;
constexpr int kEconEntityAttrManager=0,kAttrContainerItem=0,kViewDefIndex=0;
constexpr int kItemViewAttributeList=16,kAttrStyleOverride=542;
struct CAttributeList {int style=0,paint=0,festive=0;float GetValue(int){return float(style);}};
struct CEconItemView {uint16_t def=0;uint64_t id=0;CAttributeList attrs;};
template<class T>T& Field(void* p,int offset){return *reinterpret_cast<T*>(uintptr_t(p)+offset);}
template<class T>struct Handle {
 T* p=nullptr;T* Get()const{return p;}bool IsValid()const{return p;}
 int ToInt()const{return int(uintptr_t(p)&0x7fffffff);}void Set(T* v){p=v;}void Term(){p=nullptr;}
};
struct CBaseEntity {CEconItemView view;bool IsPlayer(){return true;}template<class T>T* As(){return static_cast<T*>(this);}};
struct CTFWeaponBase:CBaseEntity {
 int slot=0,world=0,vm=0;Handle<CTFWeaponBase> extra,extraVM;
 int GetSlot(){return slot;}auto& m_hExtraWearable(){return extra;}
 auto& m_hExtraWearableViewModel(){return extraVM;}
 int m_iWorldModelIndex(){return world;}int m_iViewModelIndex(){return vm;}
};
struct CTFPlayer:CBaseEntity {
 bool alive=true;std::array<Handle<CTFWeaponBase>,MAX_WEAPONS> weapons;
 bool IsAlive(){return alive;}int m_iClass(){return 1;}auto& m_hMyWeapons(){return weapons;}
 bool InCond(int){return false;}
};
struct CTFPlayerInventory {std::map<uint64_t,CEconItemView> items;};
struct Engine {int GetLocalPlayer(){return 1;}};
struct Entities {CBaseEntity* local=nullptr;CBaseEntity* GetClientEntity(int){return local;}};
struct State {int m_nDeltaTick=1;};
namespace I {Engine engine;Engine* EngineClient=&engine;Entities entities;Entities* ClientEntityList=&entities;State state;State* ClientState=&state;}
namespace Vars::Visuals::SkinChanger {struct Toggle{bool Value=true;};Toggle Enabled,AllowUnsupported;}
namespace S {struct Init {template<class T>void Call(CTFWeaponBase*){}} CEconEntity_InitializeAttributes;}
struct WeaponVisual_t {
 Handle<CTFWeaponBase> hEntity;alignas(16) uint8_t aOriginal[sizeof(CEconItemView)]={};
 uint64_t uClientID=0;int iStyle=-1;Skin_t tSkin;bool bAllowUnsupported=false;int iAppliedDef=-1;
 int iWorldModel=0,iViewModel=0;bool bConstructed=false;
 Handle<CTFWeaponBase> hOriginalExtraWorld,hOriginalExtraVM;
};
std::array<WeaponVisual_t,MAX_WEAPONS> s_aWeaponVisuals;
uint64_t ViewItemID(CEconItemView* v){return v->id;}
CEconItemView* FindItemView(CTFPlayerInventory* inv,uint64_t id){auto it=inv->items.find(id);return it==inv->items.end()?nullptr:&it->second;}
bool ConstructItemView(void* v){std::memset(v,0,sizeof(CEconItemView));return true;}
void AssignItemView(CEconItemView* to,CEconItemView* from){*to=*from;}
int LoadoutWeaponType(int def,int,int){return def==13||def==200||def==900?13:def;}
uint16_t* DefIndex(CBaseEntity* e){return &e->view.def;}
int overlays=0,refreshes=0,lastExtra=0;
int ApplySkin(CTFWeaponBase* w,int key,const Skin_t& skin,bool,int base=-1){
 ++overlays;if(skin.iPaintKit>=0)w->view.attrs.paint=skin.iPaintKit;
 if(skin.bFestive)w->view.attrs.festive=1;
 return skin.iPaintKit>=0?900:base>=0?base:key;
}
void RefreshWeaponVisual(CTFWeaponBase* w){++refreshes;w->world=w->vm=w->view.def;}
void KeepWeaponModels(WeaponVisual_t& v,CTFWeaponBase* w){w->world=v.iWorldModel;w->vm=v.iViewModel;}
void UpdateWeaponExtras(WeaponVisual_t&,CTFPlayer*,CTFWeaponBase*,uint16_t def){lastExtra=def;}
void ResetWeaponVisual(WeaponVisual_t& v){
 if(v.bConstructed&&v.hEntity.Get()&&v.hEntity.Get()->view.id==v.uClientID)
  AssignItemView(&v.hEntity.Get()->view,reinterpret_cast<CEconItemView*>(v.aOriginal));
 v={};
}
unsigned ConfigHash(const std::unordered_map<int,Skin_t>& skins,bool unsupported){
 unsigned hash=unsupported?7:1;for(const auto& [key,s]:skins)hash+=key+s.iPaintKit+2*s.bFestive;return hash;
}
struct HeldWeapon_t {bool bValid=false;int iKey=-1;};
struct CSkinChanger {
 struct Tracked_t {CBaseEntity* pEntity=nullptr;int iOriginal=-1,iApplied=-1;unsigned uSkinHash=0;};
 std::mutex m_tMutex;std::map<int,uint64_t> m_mLoadout;std::unordered_map<int,Skin_t> m_mSkins;
 std::unordered_map<int,Tracked_t> m_mTracked;bool m_bWasEnabled=false;unsigned m_uLastHash=0;
 int scheduled=0;CTFPlayerInventory inv;HeldWeapon_t m_tHeld;
 static int Key(int def){return def==200?13:def;}
 static bool IsClientItem(uint64_t id){return id>=10000;}
 Skin_t Get(int key){auto it=m_mSkins.find(key);return it==m_mSkins.end()?Skin_t{}:it->second;}
 void ScheduleRefresh(){++scheduled;}int OriginalDef(CBaseEntity*,int,int)const;
 CTFPlayerInventory* GetLocalInventory(){return &inv;}
 void RestoreWeaponVisuals(){for(auto& v:s_aWeaponVisuals)ResetWeaponVisual(v);}
 void RunFrame();void UpdateWeaponVisuals(CTFPlayer*);void CacheMenuInfo(CTFWeaponBase*);
};
int ClientItemDef(uint64_t){return 200;}
'''

checks = r'''
int main(){
 CSkinChanger c;CTFPlayer local;CTFWeaponBase weapon;weapon.view={13,10,{0,3,0}};
 local.weapons[0].Set(&weapon);I::entities.local=&local;
 constexpr uint64_t id=10001;c.m_mLoadout[19]=id;c.inv.items[id]={200,id,{2,7,0}};
 const auto backpack=c.inv.items[id];const auto loadout=c.m_mLoadout;
 auto frame=[&]{c.RunFrame();c.UpdateWeaponVisuals(&local);c.RunFrame();};
 frame();assert(weapon.view.id==id&&weapon.view.def==200&&weapon.view.attrs.paint==7);
 // Repeated edit/render/network passes must compose instead of fighting each other.
 c.m_mSkins[13].iPaintKit=101;
 frame();assert(weapon.view.id==id&&weapon.view.def==900&&weapon.view.attrs.paint==101);
 assert(weapon.world==900&&weapon.vm==900&&lastExtra==900&&c.scheduled==0);
 int applied=overlays,refreshed=refreshes;
 for(int n=0;n<20;++n)frame();
 assert(overlays==applied&&refreshes==refreshed&&c.scheduled==0);
 c.CacheMenuInfo(&weapon);assert(c.m_tHeld.iKey==13);
 // Removing a paint restores the backpack paint/definition, while festive composes.
 c.m_mSkins[13]={};c.m_mSkins[13].bFestive=true;
 frame();assert(weapon.view.def==200&&weapon.view.attrs.paint==7&&weapon.view.attrs.festive==1);
 c.m_mSkins.clear();frame();
 assert(weapon.view.def==200&&weapon.view.attrs.paint==7&&weapon.view.attrs.festive==0);
 assert(c.inv.items[id].def==backpack.def&&c.inv.items[id].attrs.paint==backpack.attrs.paint&&c.m_mLoadout==loadout);
 // Server replay, respawn and disabling all restore or rebuild the right base.
 weapon.view={13,10,{0,3,0}};frame();assert(weapon.view.id==id&&weapon.view.attrs.paint==7);
 local.alive=false;c.UpdateWeaponVisuals(&local);assert(weapon.view.id==10&&weapon.view.def==13);
 local.alive=true;frame();assert(weapon.view.id==id);
 Vars::Visuals::SkinChanger::Enabled.Value=false;c.UpdateWeaponVisuals(&local);
 assert(weapon.view.id==10&&weapon.view.attrs.paint==3);
 Vars::Visuals::SkinChanger::Enabled.Value=true;
 // An unrelated native weapon with an unchanged skin must not cause a full
 // update (and invalidate clothes) when the inventory weapon is edited.
 CTFWeaponBase secondary;secondary.slot=1;secondary.view={42,11,{}};local.weapons[1].Set(&secondary);
 c.m_mSkins[42].bFestive=true;frame();int nativeRefreshes=c.scheduled;
 c.m_mSkins[13].iPaintKit=101;frame();assert(c.scheduled==nativeRefreshes);
 c.m_mSkins[13]={};frame();assert(c.scheduled==nativeRefreshes);
 local.weapons[1].Term();c.m_mSkins.clear();
 // Native weapons still use the legacy editor and schedule cleanup on reset.
 c.m_mLoadout.clear();c.m_mSkins[13].iPaintKit=101;frame();
 assert(weapon.view.def==900&&c.scheduled>0);
 const int scheduled=c.scheduled;c.m_mSkins.clear();c.RunFrame();assert(c.scheduled>scheduled);
}
'''

methods = '\n'.join(method(s) for s in [
    'unsigned HashSkin(', 'int CSkinChanger::OriginalDef(', 'void CSkinChanger::RunFrame(',
    'void CSkinChanger::UpdateWeaponVisuals(', 'void CSkinChanger::CacheMenuInfo('])
with tempfile.TemporaryDirectory(prefix='sixbox-composition-') as folder:
    cpp = Path(folder)/'test.cpp'
    exe = Path(folder)/'test'
    cpp.write_text(fixture+methods+checks)
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: production inventory/editor composition, edits/reset, models, replay, respawn and native cleanup')
