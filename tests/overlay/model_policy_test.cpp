#include "../../SixBox/src/Features/ImGui/Overlay/Linux/model_policy.hpp"
#include <cassert>
struct Model
{
    enum Kind { Player, Weapon, Wearable, Viewmodel, Other } kind;
    int team;
    Model* owner=nullptr;
    bool IsPlayer() const {return kind==Player;}
    bool IsBaseCombatWeapon() const {return kind==Weapon;}
    bool IsWearable() const {return kind==Wearable;}
    bool IsWearableVM() const {return kind==Viewmodel;}
    int m_iTeamNum() const {return team;}
    struct Handle {Model* value;Model* Get() const{return value;}};
    Handle m_hOwnerEntity() const{return {owner};}
};
int main()
{
    using LocalModel::EnemyPart;
    Model local{Model::Player,2},enemy{Model::Player,3},friendPlayer{Model::Player,2};
    Model weapon{Model::Weapon,3,&enemy},hat{Model::Wearable,3,&enemy};
    Model ownWeapon{Model::Weapon,2,&local},friendHat{Model::Wearable,2,&friendPlayer};
    Model hands{Model::Viewmodel,3,&enemy},dropped{Model::Weapon,3},other{Model::Other,3,&enemy};
    assert(EnemyPart(&enemy,&local,true,0));
    assert(EnemyPart(&weapon,&local,true,0));assert(EnemyPart(&hat,&local,true,0));
    assert(!EnemyPart(&local,&local,true,0));assert(!EnemyPart(&friendPlayer,&local,true,0));
    assert(!EnemyPart(&ownWeapon,&local,true,0));assert(!EnemyPart(&friendHat,&local,true,0));
    assert(!EnemyPart(&hands,&local,true,0));assert(!EnemyPart(&dropped,&local,true,0));
    assert(!EnemyPart(&other,&local,true,0));assert(!EnemyPart(&enemy,&local,false,0));
    assert(!EnemyPart(&enemy,&local,true,1));assert(!EnemyPart<Model>(nullptr,&local,true,0));
    assert(!EnemyPart<Model>(&enemy,nullptr,true,0));
    weapon.owner=&local;assert(!EnemyPart(&weapon,&local,true,0));
    hat.owner=nullptr;assert(!EnemyPart(&hat,&local,true,0));
    enemy.team=2;assert(!EnemyPart(&enemy,&local,true,0));
}
