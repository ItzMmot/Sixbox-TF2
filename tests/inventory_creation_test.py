"""Exercise actual client wearable creation with a cold model cache."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'SixBox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text()
a = source.index('CEconWearable* CSkinChanger::CreateWearable(')
b = source.index('\nbool CSkinChanger::AppendWearable(', a)
method = source[a:b]
fixture = r'''
#include <cassert>
#include <string>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>
#include <cstdlib>
struct CTFPlayer {};
void WearableDiagnostic(int,int,const char*){}
template<class T>T& Field(void* p,int offset){return *reinterpret_cast<T*>(uintptr_t(p)+offset);}
constexpr int kViewDefIndex=0;
namespace std {template<class... T>string format(const char*,T...){return {};}}
struct CEconItemView { int def = 1; };
constexpr int kRenderGroupOpaqueEntity=2,DATA_UPDATE_CREATED=0;
constexpr float kClientThinkAlways=-1293.f;
constexpr size_t kTFWearableSize=1024;
bool registered=false,failAllocation=false,failInitialize=false,failView=false;
int allocated=0,released=0,assigned=0;
std::vector<int> order;
struct CEconWearable {
 CEconItemView item;CTFPlayer* owner=nullptr;
 bool spawned=false,equipped=false,updated=false,thinking=false;
 bool InitializeAsClientEntity(const char*,int) {
  order.push_back(3);assert(registered&&owner&&item.def==42);
  return !failInitialize;
 }
 void ReleaseClientEntity(){++released;}
 void Spawn(){order.push_back(4);spawned=true;}
 void Equip(CTFPlayer* p){assert(spawned&&p==owner);equipped=true;}
 CEconWearable* GetClientNetworkable(){return this;}
 void OnDataChanged(int){assert(equipped);updated=true;}
 void SetNextClientThink(float){assert(updated);thinking=true;}
};
struct Models {
 int GetModelIndex(const char*){return -2;}
 int RegisterDynamicModel(const char*,bool client){assert(client);registered=true;order.push_back(1);return -2;}
};
namespace I {Models models;Models* ModelInfoClient=&models;}
CEconItemView* WearableItemView(CEconWearable* w){return failView?nullptr:&w->item;}
void AssignItemView(CEconItemView* to,CEconItemView* from){*to=*from;++assigned;}
CEconWearable wearable;
namespace S {
 template<int Id> struct Signature {
  uintptr_t operator()()const{return 1;}
  template<class T=void,class... A>T Call(A... args)const {
   if constexpr(Id==0){++allocated;order.push_back(2);if constexpr(std::is_pointer_v<T>)return failAllocation?nullptr:static_cast<T>(std::malloc(kTFWearableSize));}
   if constexpr(Id==1){wearable={};std::free(std::get<0>(std::tuple(args...)));return &wearable;}
   if constexpr(Id==2)return "models/test/hat.mdl";
   if constexpr(Id==3)wearable.owner=std::get<1>(std::tuple(args...));
  }
 };
 Signature<0> MemAlloc_Zero;Signature<1> CTFWearable_Constructor;
 Signature<2> CEconItemView_GetPlayerDisplayModel;Signature<3> CBaseEntity_SetOwnerEntity;
}
struct CSkinChanger {CEconWearable* CreateWearable(CTFPlayer*,CEconItemView*,int,int);};
'''
checks = r'''
int main(){
 CSkinChanger changer;CTFPlayer player;CEconItemView view{42};
 assert(changer.CreateWearable(&player,&view,1,2)==&wearable);
 assert(wearable.item.def==42&&wearable.thinking);
 assert((order==std::vector<int>{1,2,3,4}));
 failAllocation=true;assert(!changer.CreateWearable(&player,&view,1,2));assert(released==0);
 failAllocation=false;failInitialize=true;
 assert(!changer.CreateWearable(&player,&view,1,2));assert(released==1&&!wearable.spawned);
 failInitialize=false;failView=true;
 assert(!changer.CreateWearable(&player,&view,1,2));assert(released==2);
 assert(!changer.CreateWearable(nullptr,&view,1,2));
 assert(!changer.CreateWearable(&player,nullptr,1,2));
}
'''
fixture = '#include <tuple>\n' + fixture
with tempfile.TemporaryDirectory(prefix='sixbox-creation-') as directory:
    cpp=Path(directory)/'creation.cpp'; exe=Path(directory)/'creation'
    cpp.write_text(fixture+method+checks)
    subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Production wearable creation: cold model cache, assignment, lifecycle and failure cleanup passed')
