"""Exercise production inventory selection with real-item identities and fake-item exclusions."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'SixBox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text()
a=s.index('uint64_t CSkinChanger::FindOwnedWeapon(');b=s.index('\nstd::map<int, uint64_t> CSkinChanger::GetLoadout()',a)
method=s[a:b]
fixture=r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <unordered_map>
#include "SixBox/src/Features/Visuals/SkinChanger/InventoryWeaponPolicy.h"
struct CEconItemView {uint16_t def=0;uint64_t id=0;};
constexpr int kViewDefIndex=offsetof(CEconItemView,def);
constexpr int kEconItemViewSize=sizeof(CEconItemView);
struct Inventory {uint8_t* data=nullptr;int count=0;};
constexpr int kInventoryItemsData=offsetof(Inventory,data),kInventoryItemsCount=offsetof(Inventory,count);
constexpr int Misc_t_GoldFryingPan=1071,Misc_t_FryingPan=264;
template<class T>T& Field(void* p,std::size_t offset){return *reinterpret_cast<T*>(uintptr_t(p)+offset);}
uint64_t ViewItemID(CEconItemView* p){return p->id;}
struct Info {std::string sClass="saxxy";unsigned uClassMask=0x1ff;};
std::unordered_map<int,Info> infos;
const Info* FindItemInfo(int def){auto it=infos.find(def);return it==infos.end()?nullptr:&it->second;}
int RedirectIndex(int def){return def>=0&&def<=8?190+def:def;}
int LoadoutWeaponType(int def,int cls,int slot){
 if(def==423||def==1071||def==264)return slot==2?RedirectIndex(InventoryWeaponPolicy::Stock(cls,slot)):-1;
 return RedirectIndex(def);
}
struct CSkinChanger {
 Inventory inv;std::vector<CEconItemView> views;
 Inventory* GetLocalInventory(){inv.data=reinterpret_cast<uint8_t*>(views.data());inv.count=int(views.size());return &inv;}
 CEconItemView* FindItemView(Inventory*,uint64_t id){for(auto& v:views)if(v.id==id)return &v;return nullptr;}
 bool IsClientItem(uint64_t id){return id>=10000&&id!=~0ull;}
 uint64_t FindOwnedWeapon(uint64_t,int,int);
};
'''
checks=r'''
int main(){
infos[1071]={};infos[264]={};infos[423]={};infos[196]={"tf_weapon_shovel",2};
// The actual normal pan schema excludes Spy/Engineer.
infos[264].uClassMask=0x1ffu^InventoryWeaponPolicy::ClassMask(8)^InventoryWeaponPolicy::ClassMask(9);
for(int cls=1;cls<=9;++cls){
 int stock=InventoryWeaponPolicy::Stock(cls,2);
 CSkinChanger c;c.views={{1071,10001},{uint16_t(stock),1},{264,20},{264,10},{264,10002},{1071,2}};
 assert(c.FindOwnedWeapon(10001,cls,2)==uint64_t(cls<=7?10:1));
 c.views={{1071,10001},{uint16_t(stock),1},{264,10002},{1071,2}};
 assert(c.FindOwnedWeapon(10001,cls,2)==1);
 c.views={{1071,10001},{264,10002},{1071,2}};
 assert(c.FindOwnedWeapon(10001,cls,2)==0); // Caller uses native default stock.
 assert(c.FindOwnedWeapon(10001,cls,0)==0);
 assert(c.FindOwnedWeapon(2,cls,2)==0);
}
CSkinChanger c;c.views={{423,10001},{6,1},{264,10}};
assert(c.FindOwnedWeapon(10001,3,2)==1); // Saxxy still maps directly to stock.
c.views={{196,10001},{196,20},{6,1}};assert(c.FindOwnedWeapon(10001,3,2)==20);
}
'''
with tempfile.TemporaryDirectory() as directory:
 cpp=Path(directory)/'owned.cpp';exe=Path(directory)/'owned'
 cpp.write_text(fixture+method+checks)
 subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Production FindOwnedWeapon: pan priority, class restrictions, stock and fake exclusions passed')
