// tests/gold_kill_policy_test.cpp — confirmed deaths, identity and one-shot ownership.
#include "../SixBox/src/Features/Visuals/SkinChanger/GoldKillPolicy.h"
#include <cassert>
#include <cstring>
int main()
{
    using namespace GoldKillPolicy;
    assert(Identify(423,false) == Kind::Saxxy);
    assert(Identify(169,false) == Kind::Wrench);
    assert(Identify(1071,false) == Kind::Pan);
    assert(Identify(264,false) == Kind::None);
    assert(Identify(205,true) == Kind::Australium);
    assert(!Icon(Kind::Australium) && !Icon(Kind::None));
    assert(!std::strcmp(Icon(Kind::Wrench),"wrench_golden"));
    Deaths deaths;
    assert(!deaths.Ready(1,10,0.f));
    deaths.Record(1,10,2.f,true);
    assert(!deaths.Ready(1,11,2.f));
    assert(!deaths.Ready(1,10,1.f));
    assert(deaths.Consume(1,10,2.1f));
    assert(!deaths.Consume(1,10,2.1f));
    deaths.Record(1,10,3.f,false);
    assert(deaths.Ready(1,10,3.f) && !deaths.Consume(1,10,3.f));
    deaths.Record(1,10,4.f,true);
    assert(!deaths.Ready(1,10,5.1f));
    deaths.Spawn(1);
    assert(!deaths.Ready(1,10,4.f));
    deaths.Record(128,20,6.f,true);
    deaths.Clear();
    assert(!deaths.Ready(128,20,6.f));
    deaths.Record(0,10,7.f,true);
    deaths.Record(129,10,7.f,true);
    assert(!deaths.Ready(-1,10,7.f) && !deaths.Ready(129,10,7.f));
}
