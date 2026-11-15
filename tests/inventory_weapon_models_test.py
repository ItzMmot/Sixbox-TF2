"""Compile production model-refresh methods against an engine with stale model caches."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'SixBox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text()
def method(name):
 start=source.index('    void '+name+'(');brace=source.index('{',start);end=brace+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
fixture=r'''
#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
constexpr int TF_COND_TAUNTING=7,DATA_UPDATE_DATATABLE_CHANGED=1;
constexpr int kEconEntityAttrManager=32,kAttrContainerItem=16,kViewDefIndex=0;
template<class T>T& Field(void* p,int offset){return *reinterpret_cast<T*>(uintptr_t(p)+offset);}
struct CEconItemView {uint16_t definition;};
struct CBaseEntity {
 alignas(16) std::array<unsigned char,512> bytes={};int modelIndex=1;bool player=false;CBaseEntity* owner=nullptr;
 int& m_nModelIndex(){return modelIndex;} bool IsPlayer(){return player;}
 CBaseEntity* GetOwnerEntity(){return owner;}
 template<class T>T* As(){return static_cast<T*>(this);}
 void InvalidateBoneCache(){}
};
using CBaseAnimating=CBaseEntity;
template<class T>struct Handle {T* p=nullptr;T* Get(){return p;}};
struct CTFWeaponBase;
struct CTFPlayer:CBaseEntity {
 Handle<CBaseEntity> active,view;bool taunt=false;
 auto& m_hActiveWeapon(){return active;}auto& m_hViewModel(){return view;}
 bool InCond(int){return taunt;}int m_iClass(){return 3;}int m_iTeamNum(){return 2;}
};
struct Network {void OnDataChanged(int){}};
struct CTFWeaponBase:CBaseEntity {
 int world=1,hands=5,addons=0;bool specialWorld=false;Network net;
 int& m_iWorldModelIndex(){return world;}int& m_iViewModelIndex(){return hands;}
 int Definition(){return Field<uint16_t>(this,kEconEntityAttrManager+kAttrContainerItem);}
 const char* GetWorldModel(){return specialWorld?"world_glove":Definition()==6?"shovel":Definition()==423?"saxxy":"pan";}
 const char* GetWeaponViewModel(int){return Definition()==6?"v_shovel":"arms";}
 int GetWorldModelIndex(){int& cache=Field<int>(this,400);if(!cache)cache=specialWorld?6:Definition()==6?1:Definition()==423?3:2;return world=cache;}
 Network* GetClientNetworkable(){return &net;}void UpdateAllViewmodelAddons(){++addons;}
};
struct WeaponVisual_t {int iWorldModel=0,iViewModel=0;uint64_t uNextModelAttempt=0;};
struct Model {const char* name;};
struct ModelInfo {
 int registrations=0;std::array<Model,7> models={Model{""},Model{"shovel"},Model{"pan"},Model{"saxxy"},Model{"arms"},Model{"v_shovel"},Model{"world_glove"}};
 int GetModelIndex(const char* path){for(int i=1;i<7;++i)if(!std::strcmp(models[i].name,path))return i;return -1;}
 Model* GetModel(int id){return id>0&&id<7?&models[id]:nullptr;}
 const char* GetModelName(Model* model){return model->name;}
 void RegisterDynamicModel(const char*,bool){++registrations;}
};
namespace I {ModelInfo info;ModelInfo* ModelInfoClient=&info;}
namespace H {struct EntitiesType {CTFPlayer* local=nullptr;CTFPlayer* GetLocal(){return local;}} Entities;}
namespace U {struct MemoryType {
 template<int index,class R>R CallVirtual(CTFWeaponBase* weapon){static_assert(index==320);return weapon->GetWorldModel();}
} Memory;}
namespace S {
 struct DisplayModel {
 bool operator()(){return true;}
 template<class R>R Call(CEconItemView* view,int,int){return view->definition==6?"shovel":view->definition==423?"saxxy":"pan";}
 } CEconItemView_GetPlayerDisplayModel;
 struct SetModel {
 bool operator()(){return true;}
 template<class R>R Call(CBaseEntity* entity,const char* path){entity->modelIndex=I::info.GetModelIndex(path);return true;}
 } Inventory_CBaseEntity_SetModel;
}
int WorldModelCacheOffset(){return 400;}uint64_t GetTickCount64(){return 2000;}
'''
checks=r'''
int main(){
 static CTFPlayer local;local.player=true;H::Entities.local=&local;
 static CBaseEntity vm;vm.modelIndex=5;local.view.p=&vm;
 for(int def:{1071,423}){
  CTFWeaponBase weapon;weapon.owner=&local;local.active.p=&weapon;
  Field<uint16_t>(&weapon,kEconEntityAttrManager+kAttrContainerItem)=uint16_t(def);
  Field<int>(&weapon,400)=1;
  RefreshWeaponModels(&weapon);
  assert(weapon.world==(def==423?3:2)&&weapon.hands==4&&vm.modelIndex==4);
  assert(weapon.modelIndex==weapon.world&&Field<int>(&weapon,400)==weapon.world);
  WeaponVisual_t visual{weapon.world,weapon.hands,0};
  // Server replays the old indices without changing the selected local econ item.
  weapon.world=1;weapon.hands=5;vm.modelIndex=5;Field<int>(&weapon,400)=1;
  const int allocations=I::info.registrations;
  KeepWeaponModels(visual,&weapon);
  assert(weapon.world==visual.iWorldModel&&weapon.hands==4&&vm.modelIndex==4);
  assert(Field<int>(&weapon,400)==visual.iWorldModel&&I::info.registrations==allocations);
  // Inactive weapon updates cannot change the held weapon's model.
  local.active.p=nullptr;vm.modelIndex=5;KeepWeaponModels(visual,&weapon);assert(vm.modelIndex==5);
  local.active.p=&weapon;local.taunt=true;weapon.world=99;
  KeepWeaponModels(visual,&weapon);assert(weapon.world==99);local.taunt=false;
  // Restoring the real item recomputes both caches, even if the server restored the view first.
  Field<uint16_t>(&weapon,kEconEntityAttrManager+kAttrContainerItem)=6;
  RefreshWeaponModels(&weapon);assert(weapon.world==1&&weapon.hands==5&&vm.modelIndex==5);local.active.p=nullptr;
 }
 CTFWeaponBase special;special.specialWorld=true;special.owner=&local;local.active.p=&special;
 Field<uint16_t>(&special,kEconEntityAttrManager+kAttrContainerItem)=1181;
 RefreshWeaponModels(&special);assert(special.world==6&&special.modelIndex==6);local.active.p=nullptr;
}
'''
with tempfile.TemporaryDirectory() as directory:
 cpp=Path(directory)/'models.cpp';exe=Path(directory)/'models'
 cpp.write_text(fixture+'\n'.join(method(n) for n in ['ApplyActiveViewModel','RefreshWeaponModels','KeepWeaponModels'])+checks)
 subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Production model refresh: pan/Saxxy, first/third person, replay, taunts and restore passed')
