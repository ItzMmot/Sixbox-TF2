# Test the actual serialization/startup functions without running the game.
from pathlib import Path
import subprocess,tempfile,argparse
root=Path(__file__).resolve().parents[1]
source=(root/'SixBox/src/Features/Configs/Configs.cpp').read_text()
codec=source[source.index('static void SaveSkinChanger('):source.index('\ntemplate <class T> void CConfigs::SaveJson')]
startup=source[source.index('void CConfigs::LoadStartup()'):source.index('\nvoid CConfigs::LoadState()')]
state=source[source.index('void CConfigs::LoadState()'):source.index('\n#define IsType')]
validation=source[source.index('static bool IsSafeConfigName('):source.index('\nstatic std::filesystem::path GetConfigFilePath')]
pre=r'''
#include "SKIN_PROFILE_HEADER"
#include <boost/property_tree/json_parser.hpp>
#include <cassert>
#include <sstream>
#include <string>
#include <algorithm>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cctype>
constexpr int DEFAULT_BIND=-1;
template<class T> struct ConfigVar {T Value{};std::map<int,T> Map;T& operator[](int i){return Map[i];}};
namespace Vars::Visuals::SkinChanger {inline ConfigVar<bool> Enabled,AllowUnsupported;}
struct CConfigs {
 template<class T> void SaveJson(boost::property_tree::ptree& t,const std::string& k,const T& v){t.put(k,v);}
 template<class T> void LoadJson(const boost::property_tree::ptree& t,const std::string& k,T& v){if(auto n=t.get_optional<T>(k))v=*n;}
 std::string m_sCurrentConfig="main",m_sCurrentVisuals="visual",m_sCurrentSkins="paint";
 bool m_bUseSkinProfile=true,m_bConfigHasSkins=false,mainHasSkins=false,mainSucceeds=true;
 bool loadedProfile=false;std::string loadedName;
 std::string m_sStatePath;
 void LoadStartup();void LoadState();void SaveState() const;
 bool LoadConfig(const std::string&,bool){m_bConfigHasSkins=mainSucceeds&&mainHasSkins;if(m_bConfigHasSkins)m_bUseSkinProfile=false;m_sCurrentVisuals="";return mainSucceeds;}
 bool LoadVisual(const std::string& name,bool){assert(name=="visual");return true;}
 bool LoadSkins(const std::string& name,bool){loadedProfile=true;loadedName=name;m_bUseSkinProfile=true;return true;}
};
struct CSkinChanger {
 static int Key(int i){return i;}
 SkinProfile_t profile;int commits=0,reads=0;
 SkinProfile_t GetProfile(){++reads;return profile;}
 void SetProfile(SkinProfile_t p){profile=std::move(p);++commits;}
};
namespace F {inline CConfigs Configs;inline CSkinChanger SkinChanger;struct Binds_t{std::recursive_mutex m_mMutex;};inline Binds_t Binds;}
'''.replace('SKIN_PROFILE_HEADER',(root/'SixBox/src/Features/Visuals/SkinChanger/SkinProfile.h').as_posix())
state_writer=r'''
static void WriteJson(const std::filesystem::path& path,const boost::property_tree::ptree& tree)
{boost::property_tree::write_json(path.string(),tree);}
static void ReadJson(const std::filesystem::path& path,boost::property_tree::ptree& tree)
{boost::property_tree::read_json(path.string(),tree);}
'''
post=r'''
int main(){
 SkinProfile_t wanted;
 Skin_t skin;skin.iPaintKit=101;skin.iWear=3;skin.iSeed=123456789;skin.iQuality=11;
 skin.iStatTrak=345;skin.iKillEffect=2002;skin.iPaintColor=0;skin.iBotkiller=8;
 skin.bAustralium=true;skin.bFestive=true;skin.iKillstreak=3;skin.iSheen=7;skin.iUnusual=2;
 wanted.skins[13]=skin;wanted.skins[0]={}; // Empty overrides are deliberately omitted.
 wanted.backpack={{0xFFFFFFu,727,123,0,0xFFFFFF,1},{2,727,77,0x123456,0,0},{1,13,0,-1,-1,0}};
 wanted.loadout={{19,0x554E49FFFFFF02D7ull},{171,0xFFFFFFFFFFFFFFFFull}};
 F::SkinChanger.profile=wanted;
 Vars::Visuals::SkinChanger::Enabled.Value=true;
 Vars::Visuals::SkinChanger::AllowUnsupported.Value=true;
 boost::property_tree::ptree saved;SaveSkinChanger(saved);
 assert(F::SkinChanger.reads==1);
 assert(saved.get_child("SkinChanger").count("0")==0);
 std::stringstream text;boost::property_tree::write_json(text,saved);
 boost::property_tree::ptree read;boost::property_tree::read_json(text,read);
 F::SkinChanger.profile={};Vars::Visuals::SkinChanger::Enabled.Value=false;
 assert(LoadSkinChanger(read));assert(F::SkinChanger.commits==1);
 auto result=F::SkinChanger.profile;
 assert(result.skins.size()==1&&result.skins.at(13)==skin);
 assert(result.backpack.size()==3&&result.backpack[0].uSerial==0xFFFFFFu);
 assert(result.backpack[0].iDef==727&&result.backpack[0].iStyle==1&&result.backpack[0].iUnusual==123);
 assert(result.backpack[0].iPaintColor==0&&result.backpack[0].iPaintColor2==0xFFFFFF);
 assert(result.backpack[1].uSerial==2&&result.backpack[1].iDef==727);
 assert(result.backpack[1].iStyle==0&&result.backpack[1].iUnusual==77&&result.backpack[1].iPaintColor==0x123456);
 assert(result.backpack[2].iPaintColor==-1&&result.backpack[2].iPaintColor2==-1);
 assert(result.loadout==wanted.loadout);
 assert(Vars::Visuals::SkinChanger::Enabled.Value&&Vars::Visuals::SkinChanger::AllowUnsupported.Value);
 // Embedded main configs leave the real per-bind Vars maps authoritative.
 boost::property_tree::ptree embedded;
 Vars::Visuals::SkinChanger::Enabled[DEFAULT_BIND]=false;
 SaveSkinChanger(embedded,false);
 assert(!embedded.get_optional<bool>("SkinChanger.Enabled"));
 Vars::Visuals::SkinChanger::Enabled.Value=false;
 assert(LoadSkinChanger(read,false));
 assert(!Vars::Visuals::SkinChanger::Enabled.Value&&!Vars::Visuals::SkinChanger::Enabled[DEFAULT_BIND]);
 // Restore the counter baseline after the explicit embedded reload.
 const int commits=F::SkinChanger.commits;
 // Old main configs preserve the active inventory; explicit empty profiles clear it.
 boost::property_tree::ptree legacy;
 assert(!LoadSkinChanger(legacy));assert(F::SkinChanger.commits==commits);
 assert(F::SkinChanger.profile.loadout==wanted.loadout);
 legacy.put("SkinChanger","broken");bool failed=false;
 try{LoadSkinChanger(legacy);}catch(const std::runtime_error&){failed=true;}
 assert(failed&&F::SkinChanger.commits==commits);
 boost::property_tree::ptree empty;empty.put_child("SkinChanger",{});
 assert(LoadSkinChanger(empty));assert(F::SkinChanger.profile.backpack.empty());
 // Old skin files without option fields retain existing option values.
 saved.get_child("SkinChanger").erase("Enabled");saved.get_child("SkinChanger").erase("AllowUnsupported");
 Vars::Visuals::SkinChanger::Enabled.Value=false;
 assert(LoadSkinChanger(saved));assert(!Vars::Visuals::SkinChanger::Enabled.Value);
 // Remember the selected source through actual active.cfg serialization.
 const auto statePath=std::filesystem::temp_directory_path()/("sixbox-skin-state-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
 for(bool separate:{false,true}) {
   CConfigs savedState;savedState.m_sStatePath=statePath.string();savedState.m_bUseSkinProfile=separate;savedState.SaveState();
   CConfigs restored;restored.m_sStatePath=statePath.string();restored.LoadState();
   assert(restored.m_bUseSkinProfile==separate&&restored.m_sCurrentSkins=="paint");
   restored.mainHasSkins=true;restored.LoadStartup();assert(restored.loadedProfile==separate);
 }
 boost::property_tree::ptree oldState;oldState.put("Skins","paint");WriteJson(statePath,oldState);
 CConfigs restored;restored.m_sStatePath=statePath.string();restored.m_bUseSkinProfile=false;restored.LoadState();
 assert(restored.m_bUseSkinProfile);std::filesystem::remove(statePath);
 // Actual startup selection, including old active.cfg without a source flag.
 for(bool separate:{false,true})for(bool embedded:{false,true})for(bool success:{false,true}){
   CConfigs cfg;cfg.m_bUseSkinProfile=separate;cfg.mainHasSkins=embedded;cfg.mainSucceeds=success;
   cfg.LoadStartup();assert(cfg.loadedProfile==(separate||!embedded||!success));
   if(cfg.loadedProfile)assert(cfg.loadedName=="paint");
 }
}
'''
parser=argparse.ArgumentParser();parser.add_argument('--emit-dir',type=Path);args=parser.parse_args()
if args.emit_dir:
 args.emit_dir.mkdir(parents=True,exist_ok=True)
 (args.emit_dir/'codec.cpp').write_text(pre+codec+validation+state_writer+state+startup+post)
with tempfile.TemporaryDirectory(prefix='sixbox-skin-config-') as d:
 d=Path(d);cpp=d/'test.cpp';exe=d/'test'
 cpp.write_text(pre+codec+validation+state_writer+state+startup+post)
 subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-DBOOST_BIND_GLOBAL_PLACEHOLDERS',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: skin/backpack/loadout JSON roundtrip, legacy/empty/corrupt sections, options, startup source selection.')
