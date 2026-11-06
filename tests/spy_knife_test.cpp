// tests/spy_knife_test.cpp — SpyKnife finite warp budgets, routing and cancellation.
#include "../Unibox/src/Features/Aimbot/SpyKnife/KnifePolicy.h"
#include <cassert>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void* operator new(std::size_t size) {++allocations;if(auto* p=std::malloc(size?size:1)) return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace SpyKnife;
int main()
{
    const unsigned before=allocations;
    assert(SafeGeometry({60,0,0},{1,0,0},{1,0,0},.9f));
    assert(!SafeGeometry({-60,0,0},{-1,0,0},{1,0,0},0));
    // A native edge stab is deliberately rejected by the calibrated window.
    assert(AimPolicy::BackstabGeometry({1,60,0},Unit({1,60,0}),{1,0,0}));
    assert(!SafeGeometry({1,60,0},Unit({1,60,0}),{1,0,0},.9f));
    assert(!SafeGeometry({60,0,0},{1,0,0},{1,0,0},.9f,std::numeric_limits<float>::quiet_NaN()));
    for(int i=0;i<10000;++i)
    {
        const float angle=float(i)*.137f,view=float(i)*.341f;
        const V separation=Mul(Rotate({1,0,0},angle),65.f);
        const V owner=Rotate({1,0,0},view),victim=Rotate({1,0,0},float(i)*.613f);
        if(SafeGeometry(separation,owner,victim,.9f))
        {
            assert(AimPolicy::BackstabGeometry(separation,owner,victim));
            assert(SafeGeometry(separation,owner,victim,0));
        }
    }
    assert(CalibratedDrift(4,5,1));
    assert(!CalibratedDrift(4.1f,5,1));
    assert(!CalibratedDrift(4,5.1f,1));
    assert(CalibratedDrift(8,15,0));
    assert(!CalibratedDrift(8.1f,0,0));
    assert(!CalibratedDrift(0,16,0));
    assert(!AckReady(100,100,100,110)); // Never stab in the approach command.
    assert(!AckReady(101,100,99,110));  // Movement packet still unacknowledged.
    assert(AckReady(101,100,100,110));
    assert(!AckReady(111,100,110,110)); // Expired approach cannot authorize a hit.
    const V bait=TrickWish({60,0,0},{},{1,0,0},72,5,0);
    const V cut=TrickWish({60,0,0},{},{1,0,0},72,5,6);
    assert(bait.y>0&&cut.y<0);
    assert(TrickWish({60,0,0},{},{1,0,0},72,6,0).y<0);
    assert(Length(TrickWish({60,0,0},{},{1,0,0},72,7,0))==0);

    for(int charged=0;charged<=32;++charged)
        for(int choked=0;choked<=24;++choked)
            for(int server:{8,16,24})
            {
                const int ticks=AvailableTicks(charged,choked,server);
                assert(ticks>=0&&ticks<=charged&&ticks<=MaxTicks);
                if(ticks) {assert(ticks+choked<=21);assert(ticks+choked+1<=server);}
            }
    assert(AvailableTicks(22,0,24)==21);
    assert(AvailableTicks(22,-1,24)==0);
    assert(AvailableTicks(22,0,0)==0);
    assert(AvailableTicks(-1,0,24)==0);
    assert(AvailableTicks(10,20,24)==1);
    assert(AvailableTicks(22,21,24)==0);
    assert(AvailableTicks(22,0,8)==7);
    assert(Stable(100,101,6,1,1,2,2,0,0));
    assert(Stable(100,106,6,1,1,2,2,16,25));
    assert(!Stable(100,107,6,1,1,2,2,0,0));
    assert(!Stable(100,100,6,1,1,2,2,0,0));
    assert(!Stable(100,101,6,1,2,2,2,0,0));
    assert(!Stable(100,101,6,1,1,2,3,0,0));
    assert(!Stable(100,101,6,1,1,2,2,16.01f,0));
    assert(!Stable(100,101,6,1,1,2,2,0,25.01f));
    assert(!Stable(100,101,6,1,1,2,2,std::numeric_limits<float>::quiet_NaN(),0));
    const V target{},facing{1,0,0};
    const V left=RouteWish({60,0,0},target,facing,72,1);
    const V right=RouteWish({60,0,0},target,facing,72,2);
    assert(left.y>0&&right.y<0&&std::abs(left.x-right.x)<.001f);
    assert(std::abs(Length(left)-450.f)<.001f);
    // A side approach can cross into the rear cone without touching the expanded
    // target hull. A head-on route into that hull must be rejected by a sweep.
    V owner{0,75,0};bool rear=false;
    for(int tick=0;tick<21;++tick)
    {
        const V wish=RouteWish(owner,target,facing,72,1);
        const V next=Add(owner,Mul(Unit(wish),320.f/66.f));
        const V delta=Sub(next,owner);
        const float obstruction=AimPolicy::RayBox(owner,delta,{-48,-48,-100},{48,48,100},1.f);
        assert(obstruction<0);
        owner=next;
        const V aim=Unit(Sub(target,owner));
        if(AimPolicy::BackstabGeometry(Sub(target,owner),aim,facing)) {rear=true;break;}
    }
    assert(rear);
    const V direct=RouteWish({60,0,0},target,facing,72,0);
    assert(AimPolicy::RayBox({60,0,0},Mul(Unit(direct),30.f),{-48,-48,-100},{48,48,100},1.f)>=0);
    assert(Length(RouteWish({0,0,0},target,{},72,1))==0);
    assert(allocations==before);
}
