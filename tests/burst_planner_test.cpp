// tests/burst_planner_test.cpp - complete command schedules and timer boundaries.
#include "../Unibox/src/Features/Aimbot/Shared/BurstPlanner.h"
#include <cassert>
#include <limits>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void* operator new(std::size_t size) {++allocations;if(void* p=std::malloc(size?size:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace Burst;
static Scores Uniform(float value)
{
    Scores scores;
    for(auto& delay:scores) for(auto& sample:delay) sample={value,1,true};
    return scores;
}
static void Check(const Plan& plan,Timing timing)
{
    assert(plan.valid&&plan.count>0&&plan.shots[0]==0&&plan.count<=timing.ammo);
    timing.tickBase+=plan.startDelay;
    for(int shot=1;shot<plan.count;++shot)
    {
        assert(plan.shots[shot]>=timing.NextShot(plan.shots[shot-1]));
        assert(plan.shots[shot]<=timing.extras);
        assert(!plan.Withholds(plan.shots[shot]));
    }
    // Execute every command as Ticks does: hold attack during cooldown,
    // release only withheld commands. The resulting shots must match the plan.
    int next=0,actual=0;
    for(int command=0;command<=plan.extras;++command)
    {
        if(command<next||plan.Withholds(command)||actual>=timing.ammo) continue;
        assert(actual<plan.count&&plan.shots[actual]==command);
        ++actual;next=timing.NextShot(command);
    }
    assert(actual==plan.count);
}
int main()
{
    const unsigned before=allocations;
    Timing timing{.015f,.1f,1000,21,10,0};
    auto scores=Uniform(1);
    auto plan=Schedule(timing,scores);Check(plan,timing);
    assert(plan.count==4&&plan.shots[1]==7&&plan.shots[2]==14&&plan.shots[3]==21);
    assert(plan.startDelay==0&&plan.withhold==0);
    timing.ammo=2;plan=Schedule(timing,scores);Check(plan,timing);assert(plan.count==2);
    timing.ammo=10;timing.extras=12;
    scores=Uniform(.1f);scores[0][0]={1,1,true};scores[0][9]={1,1,true};
    plan=Schedule(timing,scores,0);Check(plan,timing);
    assert(plan.shots[1]==9&&plan.Withholds(7)&&plan.Withholds(8)&&!plan.Withholds(9));
    // Delaying an individual round changes the next cooldown, not just its mask.
    timing.extras=21;scores=Uniform(.3f);scores[0][0]={1,1,true};scores[0][9]={1,1,true};scores[0][16]={1,1,true};
    plan=Schedule(timing,scores,0);Check(plan,timing);assert(plan.shots[1]==9&&plan.shots[2]==16);
    scores=Uniform(0);scores[0][0]={.1f,1,true};
    for(auto& score:scores[1]) score={1,1,true};
    plan=Schedule(timing,scores,1);Check(plan,timing);assert(plan.startDelay==1);
    timing.maximumRelease=.02f;timing.extras=12;
    scores=Uniform(.1f);scores[0][0]={1,1,true};scores[0][12]={1,1,true};
    plan=Schedule(timing,scores,0);Check(plan,timing);assert(plan.count<2||plan.shots[1]<10);
    timing.fireDelay=std::numeric_limits<float>::quiet_NaN();assert(!Schedule(timing,scores).valid);
    timing={.015f,.1f,10000000,21,10,0};plan=Schedule(timing,Uniform(1));Check(plan,timing);
    timing.extras=32;assert(!Schedule(timing,scores).valid);
    // Deterministic coverage of horizon/ammo/cooldown changes and sparse
    // visibility. Invalid samples are never chosen as a scheduled shot.
    unsigned random=13;
    for(int trial=0;trial<500;++trial)
    {
        timing={.015f,.025f+.01f*float(trial%16),100000+trial,trial%23,1+trial%8,0};
        for(auto& delay:scores) for(auto& sample:delay)
        {
            random=random*1664525u+1013904223u;
            sample={float(random%101)/100.f,1,true};
        }
        Check(Schedule(timing,scores),timing);
    }
    timing.tickBase=std::numeric_limits<int>::max();assert(!Schedule(timing,scores).valid);
    assert(allocations==before);
}
