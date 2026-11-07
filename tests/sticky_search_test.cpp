// tests/sticky_search_test.cpp - continuation, invalidation and bounded flight work.
#include "../SixBox/src/Features/Aimbot/Shared/StickySearch.h"
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
using namespace StickySearch;
static Key Input(int command)
{
    Key key;key.target=10;key.weapon=20;key.owner=30;key.spawn=2;key.ownerSpawn=3;key.model=4;
    key.map=5;key.settings=6;key.speed=1000;key.gravity=800;key.radius=146;key.arm=.8f;
    key.command=command;key.maxs={24,24,82};return key;
}
int main()
{
    const unsigned before=allocations;
    Cursor cursor;auto key=Input(100);assert(cursor.Prepare(key)&&cursor.valid);
    cursor.candidate=5;cursor.arc=1;cursor.roll=2;cursor.refinement=3;cursor.airTick=47;cursor.stage=Stage::Air;
    ++key.command;assert(!cursor.Prepare(key));assert(cursor.stage==Stage::Air&&cursor.airTick==47);
    key.eye.x=1;++key.command;assert(!cursor.Prepare(key));
    assert(cursor.stage==Stage::Flight&&cursor.candidate==5&&cursor.arc==1&&cursor.roll==2&&cursor.refinement==3&&cursor.airTick==47);
    // Small latency jitter invalidates the flight seed without discarding work.
    cursor.stage=Stage::Air;key.latency=.005f;++key.command;
    assert(!cursor.Prepare(key)&&cursor.stage==Stage::Flight&&cursor.airTick==47);
    for(int change=0;change<15;++change)
    {
        Cursor candidate;auto current=Input(100);assert(candidate.Prepare(current));
        ++current.command;
        switch(change)
        {
        case 0:++current.target;break;case 1:++current.weapon;break;case 2:++current.spawn;break;
        case 3:++current.owner;break;case 4:++current.ownerSpawn;break;case 5:++current.model;break;
        case 6:++current.map;break;case 7:++current.settings;break;case 8:current.charge=1;break;
        case 9:current.speed=1021;break;case 10:current.gravity=400;break;case 11:current.radius=100;break;
        case 12:current.eye.x=33;break;case 13:current.origin.x=65;break;case 14:current.latency=.1f;break;
        }
        assert(candidate.Prepare(current)&&candidate.candidate==0&&candidate.refinement==0&&candidate.airTick==0);
    }
    Cursor order;key=Input(1);order.Prepare(key);
    int visited=0;
    while(order.stage!=Stage::Exhausted)
    {
        assert(order.candidate==visited/8&&order.arc==(visited/4)%2&&order.roll==visited%4);
        order.Next(3,4);++visited;
    }
    assert(visited==24);
    Work shared;
    // All six configured targets can perform four probes, two double
    // verification flights and a double direct fallback without one target consuming the entire command.
    for(int target=0;target<6;++target)
    {
        for(int probe=0;probe<4;++probe) assert(shared.Flight(512));
        assert(shared.Flight(512,2)&&shared.Flight(512,2)&&shared.Flight(512,2));
    }
    assert(shared.flights==0&&!shared.Flight(1));
    Work work;work.steps=513;work.flights=1;
    assert(work.Flight(512)&&work.steps==0&&work.flights==0&&!work.Flight(1));
    work={};assert(!work.Flight(513));work.steps=511;assert(!work.Flight(512)&&work.steps==511);
    work={};work.flights=1;const int remaining=work.steps;
    assert(!work.Flight(512,2)&&work.flights==1&&work.steps==remaining);
    // An exhausted command defers the exact flight; it does not advance the
    // candidate/refinement/air cursor. The following command can reserve it.
    cursor.stage=Stage::Yaw;const int saved=cursor.refinement;
    work.steps=0;assert(!work.Flight(64));assert(cursor.stage==Stage::Yaw&&cursor.refinement==saved);
    work={};assert(work.Flight(64)&&cursor.stage==Stage::Yaw);
    float pitch=0,yaw=0;
    assert(Refine({4,6,0},{2,0,0},{0,3,0},pitch,yaw));
    assert(std::abs(pitch+2)<.01f&&std::abs(yaw+2)<.01f);
    pitch=yaw=0;assert(Refine({1000,1000,0},{1,0,0},{0,1,0},pitch,yaw));assert(pitch==-4&&yaw==-4);
    assert(!Refine({std::numeric_limits<float>::quiet_NaN(),0,0},{1,0,0},{0,1,0},pitch,yaw));
    // Exercise the same finite-difference update on a coupled nonlinear launch
    // model. The bounded six-pass iteration must reduce the physical miss.
    auto model=[](float p,float y) {
        constexpr float radians=.0174532925199433f;
        return V{500.f*std::sin(p*radians)+100.f*std::sin(y*radians),
            400.f*std::sin(y*radians)+60.f*std::sin(p*radians),30.f*std::sin((p+y)*radians)};
    };
    const V wanted=model(12,-8);pitch=yaw=0;
    auto subtract=[](V a,V b){return V{a.x-b.x,a.y-b.y,a.z-b.z};};
    for(int pass=0;pass<MaxRefinements;++pass)
    {
        const V current=model(pitch,yaw),error=subtract(current,wanted);
        V dx=subtract(model(pitch+.25f,yaw),current),dy=subtract(model(pitch,yaw+.25f),current);
        dx={dx.x*4,dx.y*4,dx.z*4};dy={dy.x*4,dy.y*4,dy.z*4};
        assert(Refine(error,dx,dy,pitch,yaw));
    }
    assert(Distance2(model(pitch,yaw),wanted)<.001f);
    key=Input(0);key.speed=0;assert(cursor.Prepare(key)&&!cursor.valid);
    assert(allocations==before);
}
