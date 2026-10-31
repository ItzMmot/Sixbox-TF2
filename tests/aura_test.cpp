// tests/aura_test.cpp - coating controls, stable motion and bounded render commands.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
// Regression: NetVars includes Windows.h before Types.h without NOMINMAX.
#define min(a,b) windows_min_macro_must_not_expand(a,b)
#include "../Unibox/src/Features/Visuals/Aura/AuraPolicy.h"
#undef min
static size_t allocations=0;
void* operator new(size_t size) {++allocations;if(auto* p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
int main()
{
    static_assert(std::size(Aura::ModeNames)==14);
    assert(Aura::UpgradeMode(1,1)==5); // Smoke -> Aurora
    assert(Aura::UpgradeMode(7,1)==5 && Aura::UpgradeMode(8,1)==5 && Aura::UpgradeMode(12,1)==5);
    assert(Aura::UpgradeMode(10,1)==0 && Aura::UpgradeMode(11,1)==4);
    for(int i=0;i<Aura::Modes;++i) assert(Aura::UpgradeMode(i,2)==i);
    assert(!Aura::DefaultAdditive(12) && Aura::DefaultAdditive(8));
    Aura::Settings bad;bad.Mode=999;bad.Thickness=std::numeric_limits<float>::quiet_NaN();bad.Coverage=-1;
    bad.Velocity=std::numeric_limits<float>::infinity();bad.Sanitize();
    assert(bad.Mode==13 && bad.Thickness==16.f && bad.Coverage==0 && bad.Velocity==24.f);
    assert(Aura::Wrap(359.f)==-1.f && Aura::Wrap(-359.f)==1.f);
    assert(Aura::Wrap(std::numeric_limits<float>::infinity())==0.f);
    Aura::MotionFilter sixty,oneTwenty;
    const auto before=allocations;
    for(int i=0;i<60;++i)sixty.Update(.8f,-.4f,1.f/60.f,.15f);
    for(int i=0;i<120;++i)oneTwenty.Update(.8f,-.4f,1.f/120.f,.15f);
    assert(std::abs(sixty.x-oneTwenty.x)<1e-5 && std::abs(sixty.y-oneTwenty.y)<1e-5);
    sixty.Update(100,-100,.016f,.01f);assert(sixty.x<=1.f&&sixty.y>=-1.f);
    sixty.Update(1,1,1.f,.15f);assert(sixty.x==0&&sixty.y==0);
    sixty.Update(std::numeric_limits<float>::quiet_NaN(),0,.016f,.15f);assert(sixty.x==0);
    Aura::CommandSlots<8> pool;std::array<int,8> slots{};
    for(int& slot:slots) {slot=pool.Acquire();assert(slot>=0);}
    assert(pool.Busy() && pool.Acquire()==-1);
    pool.Release(slots[3]);assert(pool.Acquire()==slots[3]);
    for(int slot:slots)pool.Release(slot);
    assert(!pool.Busy() && allocations==before);
    // Main/render thread handoff: a pending slot must never have two owners.
    std::array<std::atomic<int>,8> users{};
    auto work=[&] {
        for(int iteration=0;iteration<20000;++iteration)
        {
            int slot=-1;while((slot=pool.Acquire())<0)std::this_thread::yield();
            assert(users[slot].fetch_add(1)==0);
            assert(users[slot].fetch_sub(1)==1);
            pool.Release(slot);
        }
    };
    std::thread a(work),b(work),c(work),d(work);
    a.join();b.join();c.join();d.join();assert(!pool.Busy());
    std::puts("Aura coating: settings, motion smoothing, macro compatibility, allocation and concurrent command ownership checks passed");
}
