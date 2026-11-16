"""Run production OpenCrate against inventory/UI listeners and a local GC queue."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'SixBox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text()
a=source.index('bool CSkinChanger::OpenCrate(')
b=source.index('\nbool CSkinChanger::ClientInventoryActive(',a)
method=source[a:b]
fixture=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>
#include "SixBox/src/Features/Visuals/SkinChanger/LocalInventoryProtocol.h"
constexpr int kViewDefIndex=0x48,kItemViewAttributeList=0xF0,kInventoryOwner=8;
constexpr int kItemID=0x48,kItemAccountID=0x50,kItemInventoryPos=0x54,kItemDefIndex=0x58;
constexpr int kItemLevel=0x5A,kItemQuality=0x5B,kItemFlags=0x5C,kItemOrigin=0x5D;
constexpr int kViewNonSOCItem=0x78,kViewNonSOCItemID=0x80,kOriginFoundInCrate=8;
constexpr int kQualityUnique=6,kQualityUnusual=5;
constexpr double kRareChance=0.01;
template<class T>T& Field(void* p,size_t off){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+off);}
struct CEconItem {alignas(16) uint8_t bytes[128]{};};
struct CEconItemView {alignas(16) uint8_t bytes[344]{};bool attributesReady=false;};
struct CTFPlayerInventory {alignas(16) uint8_t bytes[64]{};std::map<uint64_t,CEconItemView> items;};
struct CEconItemSchema {static CEconItemSchema* GetInstance(){static CEconItemSchema s;return &s;}};
struct CAttributeList {float GetValue(int,float fallback){return fallback;}};
struct Profile {std::vector<uint16_t> vItems{42};bool bShuffle=false;int iSeries=1,iKeyDef=99,iAltKeyDef=0;};
std::map<uint16_t,Profile> s_mCrateProfiles{{10,{}}};
std::map<int,std::vector<uint16_t>> s_mCrateSeries{{1,{42}}};
namespace I {struct User {struct ID {uint32_t GetAccountID(){return 123;}};ID GetSteamID(){return {};}};User* SteamUser=nullptr;}
uint64_t GetTickCount64(){return 1000;}
uint64_t ClientItemID(uint32_t serial,uint16_t def){return (0x554E49ull<<40)|(uint64_t(serial)<<16)|def;}
bool IsWearableDef(uint16_t){return false;}
int RandomUnusualEffect(){return 0;}
bool CollectCrateLoot(CEconItemView*,std::vector<uint16_t>&){return false;}
const void* FindItemInfo(uint16_t){return reinterpret_cast<void*>(1);}
CEconItemView* FindItemView(CTFPlayerInventory* inv,uint64_t id){auto it=inv->items.find(id);return it==inv->items.end()?nullptr:&it->second;}
bool failCreate=false,failAdd=false;
std::vector<CEconItem*> allocations;
CEconItem* CreateEconItem(){if(failCreate)return nullptr;auto* p=new CEconItem;allocations.push_back(p);return p;}
void DestroyEconItem(CEconItem*){}
void AddEconItem(CTFPlayerInventory* inv,CEconItem* item){
 if(failAdd)return;
 auto& view=inv->items[Field<uint64_t>(item,kItemID)];
 Field<uint16_t>(&view,kViewDefIndex)=Field<uint16_t>(item,kItemDefIndex);
 // The normal drop path can acknowledge UNKNOWN before the collection listener.
 if(Field<uint32_t>(item,kItemInventoryPos)==0)Field<uint32_t>(item,kItemInventoryPos)=0x80000001;
}
void ApplyUnusual(CEconItemView* view,void*,int){view->attributesReady=true;}
int reveals=0;
void NotifyCreated(CTFPlayerInventory* inv,void* item){
 const auto id=Field<uint64_t>(item,kItemID);
 auto* view=FindItemView(inv,id);
 assert(view&&view->attributesReady&&Field<void*>(view,kViewNonSOCItem)==item);
 assert(Field<uint8_t>(item,kItemOrigin)==8);
 assert(Field<uint32_t>(item,kItemInventoryPos)==LocalInventoryProtocol::CratePosition);
 ++reveals;
}
struct CSkinChanger {
 struct PendingCrate_t {uint64_t uKey=0,uCrate=0,uTick=0,uSourceJob=~uint64_t(0);bool bNotifyGC=false;};
 struct Backpack {uint32_t serial;uint16_t def;int unusual;};
 std::mutex m_tMutex;
 std::map<uint64_t,void*> m_mClientItemsByID;
 std::vector<Backpack> m_vBackpack;
 std::map<uint64_t,uint64_t> m_mAwaitingPosition;
 std::map<uint64_t,int> m_mAppliedUnusual;
 LocalInventoryProtocol::Responses m_tLocalResponses;
 std::vector<uint64_t> removed;
 uint32_t serial=100;
 uint32_t NextSerial(){return serial++;}
 bool IsClientItem(uint64_t id){return (id>>40)==0x554E49;}
 void ApplyLocalDelete(uint64_t id){removed.push_back(id);}
 bool OpenCrate(CTFPlayerInventory*,const PendingCrate_t&);
};
'''
checks=r'''
int main(){
 CTFPlayerInventory inv;Field<uint32_t>(&inv,kInventoryOwner)=123;
 const uint64_t crate=ClientItemID(1,10),key=ClientItemID(2,99);
 Field<uint16_t>(&inv.items[crate],kViewDefIndex)=10;
 Field<uint16_t>(&inv.items[key],kViewDefIndex)=99;
 CSkinChanger changer;
 assert(changer.OpenCrate(&inv,{key,crate,0,12345,true}));
 assert(reveals==1&&changer.m_vBackpack.size()==1);
 assert((changer.removed==std::vector<uint64_t>{crate,key}));
 uint8_t packet[24];uint32_t type=0,size=0;
 assert(changer.m_tLocalResponses.Retrieve(&type,packet,sizeof(packet),&size)==0);
 assert(type==1008&&size==24);
 // A direct menu roll has no native GC dialog to answer.
 assert(changer.OpenCrate(&inv,{key,crate}));assert(reveals==2);
 assert(!changer.m_tLocalResponses.Available(nullptr));
 const auto count=changer.removed.size();
 for(auto request:{CSkinChanger::PendingCrate_t{123,crate},
                   CSkinChanger::PendingCrate_t{crate,crate},
                   CSkinChanger::PendingCrate_t{0,crate},
                   CSkinChanger::PendingCrate_t{key,ClientItemID(999,10)}})
  assert(!changer.OpenCrate(&inv,request));
 failCreate=true;assert(!changer.OpenCrate(&inv,{key,crate}));failCreate=false;
 assert(changer.removed.size()==count&&reveals==2&&!changer.m_tLocalResponses.Available(nullptr));
 // Failed publication rolls back only the new reward and leaves both inputs.
 failAdd=true;assert(!changer.OpenCrate(&inv,{key,crate,0,12345,true}));
 assert(changer.removed.size()==count+1&&changer.removed.back()!=key&&changer.removed.back()!=crate);
 assert(reveals==2&&!changer.m_tLocalResponses.Available(nullptr));
 for(auto* p:allocations)delete p;
}
'''
with tempfile.TemporaryDirectory(prefix='sixbox-crate-transaction-') as directory:
 cpp=Path(directory)/'crate.cpp';exe=Path(directory)/'crate'
 cpp.write_text(fixture+method+checks)
 subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Production crate transaction: ready view before reveal, crate token, local response, validation and rollback passed')
