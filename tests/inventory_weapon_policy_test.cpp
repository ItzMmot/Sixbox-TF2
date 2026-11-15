// tests/inventory_weapon_policy_test.cpp — class-dependent shared melee selection.
#include "../SixBox/src/Features/Visuals/SkinChanger/InventoryWeaponPolicy.h"
#include <cassert>
int main()
{
    using namespace InventoryWeaponPolicy;
    constexpr int melee[] = {0,3,6,1,8,5,2,4,7};
    for (int cls = 1; cls <= 9; ++cls)
    {
        assert(Resolve(423, true, cls, 2) == melee[cls-1]);
        assert(Resolve(1071, true, cls, 2) == melee[cls-1]);
        assert(Resolve(423, true, cls, 0) == -1);
        assert(Resolve(423, true, cls, 1) == -1);
        assert(Resolve(194, false, cls, 2) == 194);
    }
    for (int cls=1;cls<=9;++cls)
    {
        assert(PanAllowed(cls,0)==(cls<=7));
        assert(PanAllowed(cls,ClassMask(cls)));
        assert(!PanAllowed(cls,0x1ffu^ClassMask(cls)));
    }
    assert(OwnedRank(1071,264,false,true)==0);
    assert(OwnedRank(1071,264,false,false)==-1);
    assert(OwnedRank(1071,196,true,true)==1);
    assert(OwnedRank(1071,1071,false,true)==-1);
    assert(OwnedRank(423,264,false,true)==-1);
    assert(Stock(1,2) == 0); // Scout's base bat is not an invalid definition.
    assert(Stock(8,2) == 4); // Spy uses the knife, never a wrench or bat.
    assert(Resolve(423,true,0,2) == -1 && Resolve(423,true,10,2) == -1);
    assert(Stock(8,1) == -1 && Stock(9,3) == -1);
    assert(IsWeapon("saxxy") && IsWeapon("tf_weapon_knife"));
    assert(!IsWeapon("tf_wearable") && !IsWeapon("tool"));
}
