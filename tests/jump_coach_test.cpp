// tests/jump_coach_test.cpp - analytic worlds, coaching bounds and shot scheduling.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <limits>
#define min(a,b) windows_min_must_not_expand(a,b)
#define max(a,b) windows_max_must_not_expand(a,b)
#include "../Unibox/src/Features/Visuals/JumpCoach/CoachPolicy.h"
#undef min
#undef max
static size_t allocations=0;
void* operator new(size_t n) {++allocations;if(auto* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
using namespace JumpCoach;
struct World {bool floor=true,wall=false,corner=false,ceiling=false,invalid=false,sky=false;float slope=0;int calls=0;};
static Trace Cast(void* context,Vector from,Vector to,Vector mins,Vector maxs)
{
    auto& world=*static_cast<World*>(context);++world.calls;
    if(world.invalid)return {std::numeric_limits<float>::quiet_NaN(),{},false,false};
    Trace best;
    auto plane=[&](Vector normal,float distance) {
        const Vector support={normal.x>=0?mins.x:maxs.x,normal.y>=0?mins.y:maxs.y,normal.z>=0?mins.z:maxs.z};
        const float start=normal.Dot(from+support)-distance,end=normal.Dot(to+support)-distance;
        if(start<-.005f){best={0,normal,true,false};return;}
        if(end<0 && end<start-1e-6f)
        {
            const float fraction=std::clamp(start/(start-end),0.f,1.f);
            if(fraction<best.fraction) best={fraction,normal,false,world.sky};
        }
    };
    if(world.floor)plane(Unit({-world.slope,0,1}),0);
    if(world.wall)plane({-1,0,0},-100);
    if(world.corner)plane({0,-1,0},-100);
    if(world.ceiling)plane({0,0,-1},-150);
    return best;
}
static Plan plan;
static Body BodyAt(Vector origin,Vector velocity) {return {origin,velocity,{-24,-24,0},{24,24,82}};}
int main()
{
    Physics p;World world;
    const auto before=allocations;
    Predict(BodyAt({0,0,120},{400,0,-40}),{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Landed&&plan.count>1&&plan.count<=MaxTicks+1);
    auto last=plan.points[plan.count-1];
    assert(std::abs(last.time-.5f)<.025f&&std::abs(last.origin.x-200)<10);
    assert(std::abs(plan.landing.z)<.01f&&last.velocity.Horizontal()==400);
    Sample sample;assert(AtTime(plan,last.time,sample));assert(!AtTime(plan,last.time+.1f,sample));
    assert(std::abs(sample.origin.x-last.origin.x)<.001f);
    world={};world.floor=false;
    Predict(BodyAt({0,0,1000},{250,75,300}),{},0,p,20,2048,Cast,&world,false,1,plan);
    const float t=20*p.interval;last=plan.points[plan.count-1];
    assert(plan.end==End::Horizon&&std::abs(last.origin.z-(1000+300*t-.5f*800*t*t))<.005f);
    assert(std::abs(last.origin.x-250*t)<.001f&&std::abs(last.origin.y-75*t)<.001f);
    world={};world.wall=true;
    Predict(BodyAt({0,0,100},{300,220,0}),{},0,p,240,2048,Cast,&world,false,1,plan);
    last=plan.points[plan.count-1];
    assert(plan.end==End::Landed&&plan.collision&&std::abs(last.origin.x-76)<.02f);
    assert(last.origin.y>100&&last.velocity.Horizontal()>219&&last.velocity.Horizontal()<221);
    world.corner=true;
    Predict(BodyAt({0,0,100},{300,300,0}),{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Landed&&plan.points[plan.count-1].origin.x<=76.01f&&plan.points[plan.count-1].origin.y<=76.01f);
    world={};world.ceiling=true;
    Predict(BodyAt({0,0,0},{100,0,350}),{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Landed&&plan.collision);
    for(int i=0;i<plan.count;++i)assert(plan.points[i].origin.z<=68.01f);
    world={};world.slope=.25f;
    Predict(BodyAt({0,0,180},{200,0,-70}),{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Landed&&std::abs(plan.landing.z-.25f*plan.landing.x)<.02f);
    world={};
    auto duck=BodyAt({0,0,100},{200,0,-30});duck.mins.z=-24;duck.maxs.z=24;
    Predict(duck,{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Landed&&std::abs(plan.landing.z)<.01f);
    Predict(BodyAt({0,0,-10},{0,0,-30}),{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Blocked&&plan.count==1);
    world={};world.invalid=true;
    Predict(BodyAt({0,0,100},{100,0,0}),{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Blocked&&plan.traces==1);
    world={};
    Predict(BodyAt({0,0,100},{100,0,0}),{},0,p,240,1,Cast,&world,false,1,plan);
    assert(plan.end==End::Budget&&plan.traces==1);
    world.sky=true;
    Predict(BodyAt({0,0,10},{0,0,-200}),{},0,p,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Blocked);
    world={};world.floor=false;
    Physics zero=p;zero.gravity=0;
    Predict(BodyAt({0,0,100},{600,0,0}),{},0,zero,99999,8192,Cast,&world,true,1,plan);
    assert(plan.end==End::Horizon&&plan.count==MaxTicks+1&&plan.traces<=8192);
    for(int i=0;i<plan.count;++i)assert(plan.points[i].origin.Finite()&&plan.points[i].velocity.Finite());
    for(float speed:{20.f,100.f,800.f,3000.f})for(float accel:{1.f,5.f,10.f,100.f})
    {
        Physics physics=p;physics.airAcceleration=accel;
        const Vector velocity{speed,0,0};
        const auto guide=Guide(velocity,{},0,0,1,physics);
        assert(guide.valid&&guide.wish.Finite()&&guide.wish.y>=0);
        for(int angle=0;angle<720;++angle)
        {
            const float a=angle*Pi/360.f;
            const float gain=Accelerate(velocity,{std::cos(a),std::sin(a),0},physics.wishSpeed,physics).Horizontal()-speed;
            assert(guide.gain+.001f>=gain);
        }
        const auto perfect=Guide(velocity,guide.wish,physics.wishSpeed,0,1,physics);
        assert(perfect.efficiency>.999f);
        const auto right=Guide(velocity,{},0,0,-1,physics);
        assert(right.wish.y<=0&&std::abs(right.cameraYaw+guide.cameraYaw)<.001f);
    }
    assert(Wrap(359)==-1&&Wrap(-359)==1);
    const Vector blast{0,0,0};assert(BlastDistance(blast,{0,0,20},{-24,-24,0},{24,24,82})==20);
    assert(Aligned({0,0,-1},{.01f,0,-1})); // Near-vertical aim does not depend on an arbitrary yaw.
    assert(!Aligned({1,0,0},{0,1,0}));
    assert(!Aligned({},{}));
    Shot shot;
    ConsiderShot(shot,0,.065f,.08f,.15f,.015f,{0,0,0});assert(shot.found&&shot.error<1e-6f&&shot.delay==0);
    ConsiderShot(shot,.1f,.065f,.08f,.15f,.015f,{1,0,0});assert(shot.point.x==0); // too late
    ConsiderShot(shot,0,std::numeric_limits<float>::quiet_NaN(),.08f,.15f,.015f,{});assert(shot.point.x==0);
    Physics bad=p;bad.interval=0;
    Predict(BodyAt({0,0,100},{100,0,0}),{},0,bad,240,2048,Cast,&world,false,1,plan);
    assert(plan.end==End::Invalid&&plan.count==0);
    assert(allocations==before);
    std::puts("Jump Coach: analytic flight, slopes, ceiling, wall/corner sliding, budgets, optimal gain, shot timing and zero-allocation checks passed");
}
