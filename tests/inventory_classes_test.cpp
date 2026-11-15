// tests/inventory_classes_test.cpp — class unions, universal items and tools.
#include "../SixBox/src/Features/Visuals/SkinChanger/InventoryClasses.h"
#include <cassert>
#include <cstring>
int main()
{
    using namespace InventoryClasses;
    assert(!Matches(Classes,0));assert(!Matches(0,0));
    assert(Matches(0,Other));assert(!Matches(0,Classes));
    assert(Matches(Classes,AllClass));assert(!Matches(3,AllClass));
    for(unsigned cls=0;cls<9;++cls)
    {
        const auto mask=Entry(Names[cls],"1");
        assert(mask==(1u<<cls));
        assert(Entry(Names[cls],"primary")==mask);
        assert(Entry(Names[cls],"secondary")==mask);
        assert(Entry(Names[cls],"melee")==mask);
        assert(Entry(Names[cls],"0")==0&&Entry(Names[cls],"")==0);
        assert(Matches(mask,mask));assert(Matches(Classes,mask));
        assert(!Matches(mask,Classes^mask));
    }
    assert(Entry("Demoman","1")==1u<<3);
    assert(Entry("SPY","melee")==1u<<8);
    assert(Entry("soldier","false")==0&&Entry("soldier","-1")==0);
    assert(Entry("unknown","1")==0);
    // Shotguns/shared cosmetics match either selected owner, not just both.
    const unsigned shotgun=(1u<<1)|(1u<<2)|(1u<<4)|(1u<<5);
    assert(Matches(shotgun,(1u<<0)|(1u<<1)));
    assert(!Matches(shotgun,(1u<<0)|(1u<<8)));
    for(unsigned item=0;item<=Classes;++item)
    {
        assert(Matches(item,All));
        for(unsigned cls=0;cls<11;++cls)
            assert(Matches(item,(1u<<cls)|AllClass)==(Matches(item,1u<<cls)||Matches(item,AllClass)));
    }
    assert(!Matches(Classes,1u<<31));
}
