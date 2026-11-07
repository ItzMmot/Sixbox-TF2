// tests/spy_knife_test.cpp — SpyKnife finite warp budgets, routing and cancellation.
#include "../SixBox/src/Features/Aimbot/SpyKnife/KnifePolicy.h"
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
    constexpr float dt=1.f/66.f;
    assert(ArrivalWindow(0,dt)==3);
    assert(ArrivalWindow(3.f*dt,dt)==6);
    assert(ArrivalWindow(1,dt)==0); // Do not silently truncate an unmodelled wait.
    assert(ArrivalWindow(-1,dt)==0);
    assert(ArrivalWindow(0,0)==0);
    assert(ArrivalWindow(std::numeric_limits<float>::quiet_NaN(),dt)==0);
    assert(std::abs(RearRadius({24,24,0},{24,24,0},{1,0,0})-52.f)<.001f);
    assert(ArrivalReserve(1)>ArrivalReserve(0));
    int checked=0;
    assert(!ArrivalContactWindow(3,[&](int tick){++checked;return tick!=1;}));
    assert(checked==2); // A valid endpoint cannot hide a blocked middle sample.
    assert(!ArrivalContactWindow(13,[](int){return true;}));
    // A ray/hull contact at the old first reachable pixel is lost while an
    // escaping victim advances. Reserve + arrival samples require a deeper end.
    for(float speed:{133.f,300.f,400.f}) for(float angle:{0.f,45.f,90.f,180.f})
    {
        const V forward=Rotate({1,0,0},angle);
        const V velocity=Mul(forward,speed);
        const float range=48.f-(2.f+4.f*.9f),hull=18.f-(1.f+2.f*.9f);
        auto contact=[&](V owner,int tick,float reserve) {
            // Axis aligned in target-local coordinates, native ray before hull.
            const float gap=-Dot(owner,forward)+speed*dt*tick;
            const V start{-gap,0,36},end{range-reserve,0,0};
            float hit=AimPolicy::RayBox(start,end,{-24,-24,0},{24,24,82},1.f);
            if(hit<0) hit=AimPolicy::RayBox(start,end,{-24-hull,-24-hull,-hull},{24+hull,24+hull,82+hull},1.f);
            return hit>=0;
        };
        V legacy{},robust{};bool oldFound=false,newFound=false;
        for(int tick=1;tick<=MaxTicks;++tick)
        {
            const V owner=Mul(forward,-150.f+320.f*dt*tick);
            if(!oldFound&&contact(owner,0,0)) {legacy=owner;oldFound=true;}
            if(!newFound&&ArrivalContactWindow(3,[&](int sample){return contact(owner,sample,ArrivalReserve(.9f));}))
                {robust=owner;newFound=true;}
        }
        assert(oldFound&&!contact(legacy,3,0));
        assert(newFound&&contact(robust,3,ArrivalReserve(.9f)));
        assert(Dot(Sub(robust,legacy),forward)>8.f);
        // At the desired rear offset the follower retains forward momentum.
        const V follow=ChaseWish(Mul(forward,-52.f),{},forward,velocity,52.f);
        assert(Dot(follow,forward)>0&&Length(follow)<=450.001f);
        const V advanced=Add(Mul(forward,-52.f),Mul(follow,dt));
        const V advancedTarget=Mul(velocity,dt);
        assert(std::abs(Length(Sub(advancedTarget,advanced))-52.f)<.001f);
    }
    assert(Length(ChaseWish({-52,0,0},{},{1,0,0},{},52))==0);
    assert(Length(ChaseWish({-10000,0,0},{},{1,0,0},{400,0,0},52))<=450.001f);
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
