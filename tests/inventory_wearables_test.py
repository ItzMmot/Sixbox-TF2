"""Compile production wearable reconciliation against an ownership/provision fixture."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'SixBox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text()
a=source.index('bool CSkinChanger::AppendWearable(')
b=source.index('\nvoid CSkinChanger::CacheMenuInfo(',a)
methods=source[a:b]
fixture=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>
#include <unordered_map>
#include "SixBox/src/Features/Visuals/SkinChanger/WearableLoadoutPolicy.h"
constexpr int EF_NODRAW=32,DATA_UPDATE_DATATABLE_CHANGED=0,kMaxWearables=8,kLoadoutSlots=19;
constexpr int kEconEntityAttrManager=0,kAttrContainerItem=0;
constexpr int kAttrItemTint=1,kAttrItemTint2=2,kAttrStyleOverride=3;
int registryID=0;
std::unordered_map<int,void*> registry;
template<class T>struct CHandle {
 int id=0;
 CHandle()=default;CHandle(T* p){Set(p);}
 void Set(T* p){id=p?p->handle:0;}
 T* Get() const {auto it=registry.find(id);return it==registry.end()?nullptr:static_cast<T*>(it->second);}
 int ToInt() const{return id;}
 bool IsValid() const{return id!=0;}
 void Term(){id=0;}
};
struct CAttributeList {std::array<float,4> data{};float GetValue(int key){return data[key];}};
struct CEconItemView {
 uint16_t def=0;uint64_t id=0;bool pyro=false;CAttributeList attributes;
 uint16_t GetDefinitionIndex(){return def;}uint64_t UUID(){return id;}
};
constexpr int kViewDefIndex=offsetof(CEconItemView,def),kItemViewAttributeList=offsetof(CEconItemView,attributes);
template<class T>T& Field(void* p,size_t offset){return *reinterpret_cast<T*>(uintptr_t(p)+offset);}
struct CEconWearable;
struct WearableList {
 std::vector<CHandle<CEconWearable>> data;
 int Count() const{return int(data.size());}
 auto& operator[](int i){return data.at(i);}
};
struct CTFPlayer {
 int handle=++registryID,cls=1,team=2,cosmeticVision=0,weaponVision=1,dirty=0,refreshes=0;
 bool alive=true;WearableList list;
 CTFPlayer(){registry[handle]=this;}
 bool IsAlive(){return alive;}int m_iClass(){return cls;}int m_iTeamNum(){return team;}
 WearableList& m_hMyWearables(){return list;}
 void SetBodygroupsDirty(){++dirty;}void UpdateWearables(){++refreshes;}
 int Vision(){return cosmeticVision|weaponVision;}
};
struct CEconWearable {
 // The production item-view accessor points at offset zero in this fixture.
 CEconItemView view;int handle=++registryID,effects=0,equips=0,unequips=0,updates=0;
 CTFPlayer* owner=nullptr;bool vm=false,released=false;
 CEconWearable(){registry[handle]=this;}
 auto GetRefEHandle(){return CHandle<CEconWearable>(this);}
 int& m_fEffects(){return effects;}CTFPlayer* GetOwnerEntity(){return owner;}
 bool IsWearableVM(){return vm;}
 void Equip(CTFPlayer* player){++equips;if(owner==player)return;if(owner&&view.pyro)--owner->cosmeticVision;owner=player;if(owner&&view.pyro)++owner->cosmeticVision;}
 void UnEquip(CTFPlayer*){++unequips;if(owner&&view.pyro)--owner->cosmeticVision;owner=nullptr;}
 void ReleaseClientEntity(){assert(!owner);released=true;registry.erase(handle);}
 CEconWearable* GetClientNetworkable(){return this;}void OnDataChanged(int){++updates;}
};
CEconItemView* WearableItemView(CEconWearable* entity){return entity?&entity->view:nullptr;}
struct ItemInfo {bool bCosmetic=false;};
std::unordered_map<int,ItemInfo> catalogue;
const ItemInfo* FindItemInfo(uint16_t def){auto it=catalogue.find(def);return it==catalogue.end()?nullptr:&it->second;}
bool ResizeWearables(CTFPlayer* p,int count){if(count<0||count>kMaxWearables)return false;p->list.data.resize(count);return true;}
int hudRefreshes=0;void RefreshInventoryHudModel(){++hudRefreshes;}
constexpr int WARNING_COLOR=1;
namespace SDK{void Output(const char*,const char*,int){assert(false&&"Unexpected tracking overflow");}}
namespace Vars::Visuals::SkinChanger {struct Toggle{bool Value=true;};Toggle Enabled;}
struct Inventory {
 std::map<uint64_t,CEconItemView> views;std::map<std::pair<int,int>,uint64_t> selected;
 CEconItemView* GetItemInLoadout(int cls,int slot){
  auto entry=selected.find({cls,slot});if(entry==selected.end())return nullptr;
  auto item=views.find(entry->second);return item==views.end()?nullptr:&item->second;
 }
};
uint64_t ViewItemID(CEconItemView* view){return view->id;}
CEconItemView* FindItemView(Inventory* p,uint64_t id){if(!p)return nullptr;auto it=p->views.find(id);return it==p->views.end()?nullptr:&it->second;}
struct CSkinChanger {
 struct Wearable_t {
  CHandle<CEconWearable> hEntity;uint64_t uItemID=0;
  int iDef=-1,iClass=-1,iTeam=-1,iUnusual=-1,iPaintColor=-1,iPaintColor2=-1,iStyle=-1;
 };
 struct HiddenWearable_t{CHandle<CEconWearable> hEntity;CHandle<CTFPlayer> hOwner;bool bWasHidden=false;};
 std::array<Wearable_t,19> m_aWearables{};
 std::array<HiddenWearable_t,32> m_aHiddenWearables{};
 std::array<uint64_t,19> m_aWearableViewKey{};
 CHandle<CTFPlayer> m_hWearableOwner;
 WearableLoadoutPolicy::Selection m_tWearableSelection;
 int m_iWearableClass=0,m_iWearableTeam=0;
 Inventory inv;std::map<int,uint64_t> loadout;
 std::array<CEconItemView,19> synthetic;
 int created=0;bool failNextCreate=false;
 Inventory* GetLocalInventory(){return &inv;}auto GetLoadout(){return loadout;}
 bool IsClientItem(uint64_t id){return id>=10000;}
 int GetItemUnusual(uint64_t){return 0;}
 CEconItemView* GetWearableView(int,int slot,int,int def,int,int paint,int paint2,int style){
  synthetic[slot]={uint16_t(def),uint64_t(900000+slot),def==101,{}};
  synthetic[slot].attributes.data={0,float(paint),float(paint2),float(style)};
  return &synthetic[slot];
 }
 CEconWearable* CreateWearable(CTFPlayer* local,CEconItemView* view,int,int){
  if(failNextCreate){failNextCreate=false;return nullptr;}
  ++created;auto* entity=new CEconWearable;entity->view=*view;entity->Equip(local);return entity;
 }
 bool AppendWearable(CTFPlayer*,CEconWearable*);
 void RemoveWearable(CTFPlayer*,Wearable_t&);
 void ResetWearables(CTFPlayer*);void ClearWearables();void RestoreHiddenWearables();
 bool HideReplacedWearables(CTFPlayer*,int);bool IsReplacedWearableItem(uint64_t);
 void UpdateWearables(CTFPlayer*);
};
CEconWearable* AddNative(CTFPlayer& player,CSkinChanger& changer,int def,uint64_t id,bool pyro=false){
 auto* entity=new CEconWearable;entity->view={uint16_t(def),id,pyro,{}};
 entity->Equip(&player);player.list.data.emplace_back(entity);changer.inv.views[id]=entity->view;return entity;
}
int Clones(CSkinChanger& changer){int count=0;for(const auto& slot:changer.m_aWearables)count+=slot.hEntity.Get()!=nullptr;return count;}
bool Has(CTFPlayer& player,CEconWearable* entity){for(const auto& h:player.list.data)if(h.Get()==entity)return true;return false;}
'''
checks=r'''
int main(){
 assert(WearableLoadoutPolicy::ItemViewOffset(0xCF0,0xEE0)==0xCA8);
 assert(WearableLoadoutPolicy::ItemViewOffset(0xD10,0xF00)==0xCC8);
 assert(WearableLoadoutPolicy::ItemViewOffset(0,0xEE0)==-1);
 assert(WearableLoadoutPolicy::ItemViewOffset(0xF00,0xEE0)==-1);
 catalogue={{101,{true}},{102,{true}},{103,{true}},{201,{false}}};
 assert(WearableLoadoutPolicy::CosmeticSlot(10));
 assert(!WearableLoadoutPolicy::CosmeticSlot(9)&&!WearableLoadoutPolicy::CosmeticSlot(11));
 // Unchanged slots still mean what the inventory UI shows as equipped.
 {
  CTFPlayer p;CSkinChanger mixed;
  auto* goggles=AddNative(p,mixed,101,801,true);auto* realHat=AddNative(p,mixed,102,802);
  mixed.inv.selected={{ {1,7},802 },{ {1,8},801 }};
  mixed.inv.views[10001]={103,10001,false,{}};mixed.loadout[19+10]=10001;
  mixed.UpdateWearables(&p);
  assert(Clones(mixed)==3&&p.cosmeticVision==1&&!goggles->owner&&!realHat->owner);
  assert(mixed.m_aWearables[7].hEntity.Get()->view.id==802);
  assert(mixed.m_aWearables[8].hEntity.Get()->view.id==801);
  mixed.loadout[19+8]=0;mixed.UpdateWearables(&p);
  assert(Clones(mixed)==2&&p.cosmeticVision==0); // Explicit removal overrides the old UI slot.
  mixed.ResetWearables(&p);assert(p.cosmeticVision==1&&goggles->owner==&p);
 }
 CTFPlayer player;CSkinChanger c;
 auto* glasses=AddNative(player,c,101,41,true);
 auto* hat=AddNative(player,c,102,42);hat->effects|=EF_NODRAW;
 auto* shield=AddNative(player,c,201,43);
 c.inv.views[10001]={102,10001,false,{}};
 c.inv.views[10002]={101,10002,true,{}};
 c.inv.views[10003]={103,10003,false,{}};
 // No cosmetic override: preserve the ordinary server loadout and attributes.
 c.UpdateWearables(&player);assert(player.cosmeticVision==1&&Has(player,glasses)&&Clones(c)==0);
 c.loadout[19+7]=10001;c.UpdateWearables(&player);
 assert(Clones(c)==1&&player.list.Count()==2&&Has(player,shield));
 assert(!Has(player,glasses)&&!Has(player,hat)&&!glasses->owner&&!hat->owner);
 assert((glasses->effects&EF_NODRAW)&&player.cosmeticVision==0);
 assert(player.weaponVision==1&&shield->owner==&player); // Never force all vision flags off.
 assert(c.IsReplacedWearableItem(41)&&!c.IsReplacedWearableItem(43));
 // Explicitly selected real cosmetic gets a client display clone alongside fake clothes.
 c.loadout[19+8]=42;c.UpdateWearables(&player);
 assert(Clones(c)==2&&c.m_aWearables[8].hEntity.Get()->view.id==42);
 assert(!c.IsReplacedWearableItem(42)&&c.IsReplacedWearableItem(41));
 const int created=c.created,unEquips=glasses->unequips;
 for(int frame=0;frame<40;++frame)c.UpdateWearables(&player);
 assert(c.created==created&&glasses->unequips==unEquips&&Clones(c)==2);
 // A server resend may reinsert and reprovision a hidden original; reconcile again.
 glasses->Equip(&player);player.list.data.emplace_back(glasses);
 assert(player.cosmeticVision==1);c.UpdateWearables(&player);
 assert(player.cosmeticVision==0&&!Has(player,glasses)&&Clones(c)==2);
 // Virtual Pyrovision must work; replacing/removing it must clear only its provider.
 c.loadout[19+7]=10002;c.UpdateWearables(&player);assert(player.cosmeticVision==1);
 c.loadout[19+7]=0;c.UpdateWearables(&player);assert(player.cosmeticVision==0&&Clones(c)==1);
 c.loadout[19+10]=10003;c.UpdateWearables(&player);
 assert(c.m_aWearables[10].hEntity.Get()&&!c.m_aWearables[11].hEntity.Get()&&Clones(c)==2);
 // Duplicate selected IDs cannot create duplicate clothes or attribute providers.
 c.loadout[19+7]=42;c.UpdateWearables(&player);
 assert(Clones(c)==2&&!c.m_aWearables[8].hEntity.Get());
 // Disabling restores native ownership, list membership, attributes and prior visibility.
 Vars::Visuals::SkinChanger::Enabled.Value=false;c.UpdateWearables(&player);
 assert(Clones(c)==0&&player.list.Count()==3&&Has(player,glasses)&&Has(player,hat));
 assert(glasses->owner==&player&&hat->owner==&player&&player.cosmeticVision==1);
 assert(!(glasses->effects&EF_NODRAW)&&(hat->effects&EF_NODRAW));
 Vars::Visuals::SkinChanger::Enabled.Value=true;
 // Action/taunt changes alone must not opt a class into cosmetic replacement.
 c.loadout={{19+9,10001},{19+11,10002}};c.UpdateWearables(&player);
 assert(Clones(c)==0&&Has(player,glasses)&&player.cosmeticVision==1);
 // Explicit empty cosmetics means completely empty cosmetics, preserving weapon wearables.
 c.loadout={{19+7,0}};c.UpdateWearables(&player);
 assert(Clones(c)==0&&player.list.Count()==1&&Has(player,shield)&&player.cosmeticVision==0);
 // Failed entity creation retries without leaking clones or attribute provision.
 c.loadout={{19+7,10002}};c.failNextCreate=true;c.UpdateWearables(&player);
 assert(Clones(c)==0&&player.cosmeticVision==0);
 c.UpdateWearables(&player);assert(Clones(c)==1&&player.cosmeticVision==1);
 player.cls=2;c.UpdateWearables(&player);assert(Clones(c)==0&&Has(player,glasses)&&player.cosmeticVision==1);
 // The server has transferred a hidden entity to a different player: never steal it back.
 player.cls=1;c.UpdateWearables(&player);assert(!glasses->owner);
 CTFPlayer other;glasses->Equip(&other);c.ResetWearables(&player);
 assert(glasses->owner==&other&&!Has(player,glasses)&&other.cosmeticVision==1);
 // A stale native list entry may point to an entity already owned by another
 // player before it was ever tracked. Do not unequip that new owner either.
 auto* transferred=AddNative(player,c,102,99);transferred->Equip(&other);
 c.UpdateWearables(&player);assert(transferred->owner==&other&&!(transferred->effects&EF_NODRAW));
 // Stale handles are dropped, not dereferenced or restored to reused entities.
 c.UpdateWearables(&player);registry.erase(hat->handle);c.ResetWearables(&player);
 assert(!c.m_hWearableOwner.IsValid());
}
'''
with tempfile.TemporaryDirectory() as directory:
 cpp=Path(directory)/'wearables.cpp';exe=Path(directory)/'wearables'
 cpp.write_text(fixture+methods+checks)
 subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Production wearables: exact cosmetics, real + virtual mix, Pyrovision provision, slot 10, idempotency, server resend, restore, ownership and stale handles passed')
