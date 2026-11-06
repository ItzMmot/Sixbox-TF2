// tests/splash_window_test.cpp - rounded contact, finite hull windows and clock alignment.
#include "../Unibox/src/Features/Aimbot/Shared/SplashWindow.h"
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
using namespace SplashWindow;
int main()
{
    const unsigned before=allocations;
    assert(!BlocksPlayer(true,false,false,1));assert(!BlocksPlayer(false,true,false,1));
    assert(BlocksPlayer(false,false,false,0));assert(!BlocksPlayer(false,false,true,.249f));
    assert(BlocksPlayer(false,false,true,.25f));
    Edge edge{{0,-5,0},{0,5,0},{1,0,0},{0,0,1}};
    auto capsule=Capsule(edge,{-10,0,0},{1,0,0},Skin,100);
    assert(capsule.valid&&std::abs(capsule.low-(10-Skin))<1e-6&&std::abs(capsule.high-(10+Skin))<1e-6);
    assert(!Capsule(edge,{-10,8,0},{1,0,0},Skin,100).valid);
    auto parallel=Capsule(edge,{.01f,-10,0},{0,1,0},Skin,100);assert(parallel.valid&&parallel.low<5&&parallel.high>15);
    auto cap=Capsule(edge,{-10,5.01f,0},{1,0,0},Skin,100);assert(cap.valid&&cap.low>capsule.low);
    edge.first.x=edge.last.x=16384;auto far=Capsule(edge,{0,0,0},{1,0,0},Skin,20000);
    assert(far.valid&&std::abs(far.high-far.low-2*Skin)<1e-6);edge.first.x=edge.last.x=0;
    auto hull=Hull(edge,{-10,0,0},{1,0,0},{2,1,3},100,0);
    assert(hull.valid&&hull.low==8&&hull.high==12);
    auto endpoint=Hull(edge,{-10,6,0},{1,0,0},{2,1,3},100,0);assert(endpoint.valid);
    assert(!Hull(edge,{-10,6.1f,0},{1,0,0},{2,1,3},100,0).valid);
    auto inside=Hull(edge,{0,0,0},{0,1,0},{1,1,1},100,0);assert(inside.valid&&inside.low==0&&inside.high==6);
    auto oblique=Hull(edge,{-10,0,-10},{1,0,1},{2,1,3},100,0);assert(oblique.valid&&oblique.low==8&&oblique.high==12);
    assert(!Hull(edge,{},{},{1,1,1},100).valid);
    assert(!Hull(edge,{},{1,0,0},{-1,1,1},100).valid);
    Edge point{{0,0,0},{0,0,0},{1,0,0},{0,0,1}};assert(Capsule(point,{-10,0,0},{1,0,0},Skin,100).valid);
    float shift=0;assert(TickShift({99,101,true},15,.5f,7,shift)&&shift==10);
    assert(!TickShift({99,101,true},15,0,7,shift));
    Face left,right;left.count=right.count=4;left.normal=right.normal={0,0,1};
    left.vertices[0]={-5,-5,0};left.vertices[1]={0,-5,0};left.vertices[2]={0,5,0};left.vertices[3]={-5,5,0};
    right.vertices[0]={.02f,-5,0};right.vertices[1]={5,-5,0};right.vertices[2]={5,5,0};right.vertices[3]={.02f,5,0};
    assert(!Intersect(left,right,edge));assert(Boundary(left,right,{},.1f,edge));
    assert(edge.first.x==0&&edge.last.x==0); // Retain real collision boundary.
    assert(!Boundary(left,right,{},.001f,edge));
    // Compare the 2D clipping solver with an independent segment/AABB slab
    // test at sampled ray positions, including the computed interval centre.
    unsigned random=17;
    auto number=[&]() {random=random*1664525u+1013904223u;return float(int(random%2001)-1000)/16.f;};
    for(int trial=0;trial<500;++trial)
    {
        Edge probe{{number(),number(),number()},{number(),number(),number()},{1,0,0},{0,1,0}};
        V start{number(),number(),number()},direction{number(),number(),number()};
        const float length=std::sqrt(Dot(direction,direction));if(length<1e-6f) continue;
        direction=Mul(direction,1.f/length);
        V extent{float(trial%7)*.5f,float(trial%5)*.7f,float(trial%9)*.3f};
        const auto window=Hull(probe,start,direction,extent,200);
        auto oracle=[&](double distance) {
            const V point=Add(start,Mul(direction,float(distance))),pad=Add(extent,{Skin,Skin,Skin});
            return AimPolicy::RayBox(probe.first,Sub(probe.last,probe.first),Sub(point,pad),Add(point,pad),1)>=0;
        };
        if(window.valid&&window.high-window.low>.001) assert(oracle((window.low+window.high)*.5));
        for(int sample=0;sample<=200;++sample)
            if(oracle(sample)) assert(window.valid&&sample>=window.low-.0001&&sample<=window.high+.0001);
    }
    assert(allocations==before);
}
