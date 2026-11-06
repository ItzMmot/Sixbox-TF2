// tests/animation_timeline_test.cpp - clock, state transitions and feedback identity.
#include "../Unibox/src/Features/Aimbot/Shared/AnimationTimeline.h"
#include <cassert>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void* operator new(std::size_t size) {++allocations;if(void* p=std::malloc(size?size:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace AnimationTimeline;
static Observation Player(int tick)
{
    Observation value;value.valid=true;value.tick=tick;value.handle=123;value.spawn=4;
    value.model=7;value.playerClass=1;value.maxs={24,24,82};return value;
}
int main()
{
    const unsigned before=allocations;
    auto from=Player(100),to=Player(104);from.origin={0,0,0};to.origin={40,0,0};
    from.velocity=to.velocity={660,0,0};from.angles.y=179;to.angles.y=-179;
    auto plan=Build(from,to,.015f,800);assert(plan.continuous&&plan.count==4&&plan.first==101);
    assert(plan.At(102).origin.x==20&&std::abs(plan.At(102).angles.y)==180);
    assert(plan.At(104).angles.y==-179&&plan.At(104).origin.x==40);
    for(int tick=plan.first;tick<=to.tick;++tick) assert(plan.At(tick).tick==tick);
    from.flags=OnGround;to.flags=0;to.origin={0,0,11.5f};to.velocity.z=220;
    plan=Build(from,to,.015f,800,102);assert(plan.jump==102);
    assert(plan.At(101).flags&OnGround);assert(!(plan.At(102).flags&OnGround));
    from.flags=0;to.flags=OnGround;from.origin.z=5;from.velocity.z=-200;to.origin.z=0;
    plan=Build(from,to,.015f,800);assert(plan.land==102&&plan.At(102).flags&OnGround);
    auto airFrom=Player(100),airTo=Player(104);
    airFrom.origin.z=20;airFrom.velocity.z=-100;airTo.origin.z=29.67477f;
    auto dash=Build(airFrom,airTo,.015f,800,-1,true);
    assert(dash.airDash==102&&std::abs(dash.At(101).origin.z-18.41f)<.01f);
    assert(dash.At(102).velocity.z>200&&dash.At(104).origin.z==airTo.origin.z);
    from.flags=OnGround;to.flags=OnGround|Ducking;to.water=2;to.maxs.z=62;
    plan=Build(from,to,.015f,800);assert(!(plan.At(103).flags&Ducking));assert(plan.At(104).flags&Ducking);
    assert(plan.At(103).water==0&&plan.At(104).water==2);
    for(int change=0;change<7;++change)
    {
        auto other=to;
        switch(change) {case 0:++other.handle;break;case 1:++other.spawn;break;case 2:++other.model;break;
            case 3:++other.parent;break;case 4:++other.playerClass;break;case 5:++other.interpolation;break;case 6:other.scale=2;break;}
        assert(!Build(from,other,.015f,800).continuous);
    }
    to.origin.x=10000;assert(!Build(from,to,.015f,800).continuous);
    to=Player(125);assert(!Build(from,to,.015f,800).continuous);
    to=Player(100);assert(!Build(from,to,.015f,800).continuous);
    Identity old{1,123,4,2},now=old;assert(FeedbackApplies(old,now));
    ++now.revision;assert(!FeedbackApplies(old,now));now=old;++now.spawn;assert(!FeedbackApplies(old,now));
    now=old;++now.hypothesis;assert(!FeedbackApplies(old,now));
    assert(!FeedbackApplies({},{}));
    // Endpoint fitting must not lift a grounded player before a delayed jump.
    auto lateFrom=Player(100),lateTo=Player(104);lateFrom.flags=OnGround;lateTo.origin.z=15;
    auto late=Build(lateFrom,lateTo,.015f,800,103);
    assert(late.At(101).origin.z==0&&late.At(102).origin.z==0);
    assert(late.At(104).origin.z==15&&late.At(103).velocity.z>0);
    // The full supported observation gap is replayed, rather than repeating
    // one endpoint 24 times under one clock value.
    auto longFrom=Player(100),longTo=Player(124);longTo.origin={24,0,0};
    auto longPlan=Build(longFrom,longTo,.015f,800);assert(longPlan.count==24);
    for(int tick=longPlan.first;tick<=longTo.tick;++tick)
        assert(std::abs(longPlan.At(tick).origin.x-float(tick-100))<.0001f);
    auto invalid=Player(std::numeric_limits<int>::max());assert(!Valid(invalid));
    invalid=Player(101);invalid.mins.z=100;assert(!Valid(invalid));
    LayerOrder<7> layers;
    assert(layers.Observe(2,10,true)==0);assert(layers.Observe(0,11,true)==1);
    assert(layers.Observe(2,10,false)==0); // Completed layer retains order until next command.
    layers.BeginTick();assert(layers.layers[2].order==7&&layers.layers[0].order==0);
    assert(layers.Observe(4,12,true)==1);
    assert(layers.Observe(0,13,true,true)==1&&layers.layers[4].order==0);
    // Misses from an older record must not rotate the current resolver candidate.
    AimPolicy::ShotLedger ledger;
    ledger.Add({1,2,100,1.f,true,123,4,2,1});
    ledger.Add({1,2,101,1.f,true,123,4,2,2});
    now={2,123,4,2};int misses=0;
    ledger.ExpireDetailed(1.1f,101,[&](const AimPolicy::PendingShot& shot) {
        if(FeedbackApplies({shot.revision,shot.entity,shot.spawn,shot.hypothesis},now)) {++misses;++now.revision;}
    });
    assert(misses==1);
    ledger.Add({1,2,102,2.f,true,123,4,2,2});
    AimPolicy::PendingShot matched;
    assert(ledger.Hit(1,2,true,&matched)&&matched.revision==2&&matched.command==102);
    assert(allocations==before);
}
