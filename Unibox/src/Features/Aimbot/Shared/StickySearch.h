#pragma once
#include "AimPolicy.h"
#include <cstdint>

namespace StickySearch
{
    using V=AimPolicy::Vector;
    constexpr int MaxCandidates=96,MaxTicks=512,MaxRefinements=6;
    inline float Distance2(V a,V b) {const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;}
    inline bool Finite(V v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    struct Key
    {
        int target=0,weapon=0,spawn=0,owner=0,ownerSpawn=0,model=0,command=0;
        std::uint32_t map=0,settings=0;
        V eye{},origin{},mins{},maxs{};
        float speed=0,gravity=0,radius=0,arm=0,charge=0,latency=0;
        bool Valid() const
        {
            return command>=0&&Finite(eye)&&Finite(origin)&&Finite(mins)&&Finite(maxs)
                &&std::isfinite(speed)&&speed>0&&std::isfinite(gravity)&&gravity>=0
                &&std::isfinite(radius)&&radius>=0&&std::isfinite(arm)&&arm>=0&&std::isfinite(charge)&&std::isfinite(latency)&&latency>=0
                &&mins.x<=maxs.x&&mins.y<=maxs.y&&mins.z<=maxs.z;
        }
    };
    inline bool Compatible(const Key& seed,const Key& current,const Key& last)
    {
        return seed.Valid()&&current.Valid()&&current.command>=last.command&&current.command-last.command<=16
            &&seed.target==current.target&&seed.weapon==current.weapon&&seed.spawn==current.spawn
            &&seed.owner==current.owner&&seed.ownerSpawn==current.ownerSpawn&&seed.model==current.model
            &&seed.map==current.map&&seed.settings==current.settings&&seed.charge==current.charge
            &&seed.gravity==current.gravity&&seed.radius==current.radius&&seed.arm==current.arm
            &&std::abs(last.latency-current.latency)<=.03f
            &&Distance2(seed.eye,current.eye)<=32.f*32.f&&Distance2(seed.origin,current.origin)<=64.f*64.f
            &&Distance2(seed.mins,current.mins)<.001f&&Distance2(seed.maxs,current.maxs)<.001f
            &&std::abs(seed.speed-current.speed)<=seed.speed*.02f;
    }
    enum class Stage {Seed,Flight,Pitch,Yaw,Apply,Verify,Air,Exhausted};
    struct Cursor
    {
        Key seed{},last{};
        Stage stage=Stage::Seed;
        int candidate=0,arc=0,roll=0,refinement=0,airTick=0;
        bool valid=false;
        // Returns true when the caller must regenerate its candidate pool.
        bool Prepare(const Key& current)
        {
            if(!current.Valid()) {*this={};return true;}
            if(!valid||!Compatible(seed,current,last)) {*this={};seed=last=current;valid=true;return true;}
            const bool rebase=Distance2(last.eye,current.eye)>.0625f||Distance2(last.origin,current.origin)>.25f
                ||std::abs(last.speed-current.speed)>last.speed*.001f||std::abs(last.latency-current.latency)>.001f;
            last=current;
            // Keep the candidate/arc/refinement/air scan, but a flight from a
            // different muzzle or target observation is only an angle seed.
            if(rebase&&stage!=Stage::Seed&&stage!=Stage::Exhausted) stage=Stage::Flight;
            return false;
        }
        void Next(int candidates,int rolls)
        {
            stage=Stage::Seed;refinement=0;airTick=0;
            if(++roll<rolls) return;
            roll=0;if(++arc<2) return;
            arc=0;if(++candidate>=candidates) stage=Stage::Exhausted;
        }
    };
    struct Work
    {
        int steps=32768,flights=60,checks=512;
        bool Flight(int horizon,int count=1)
        {
            if(horizon<=0||horizon>MaxTicks||count<=0||count>2||flights<count||steps<count*(horizon+1)) return false;
            flights-=count;steps-=count*(horizon+1);return true;
        }
        bool Check() {if(checks<=0) return false;--checks;return true;}
    };
    // Damped two-variable least squares, 3D miss vector in HU; derivatives HU/degree.
    inline bool Refine(V error,V dx,V dy,float& pitch,float& yaw)
    {
        if(!Finite(error)||!Finite(dx)||!Finite(dy)) return false;
        auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
        const float a=dot(dx,dx)+.01f,b=dot(dx,dy),c=dot(dy,dy)+.01f;
        const float det=a*c-b*b;
        if(!std::isfinite(det)||det<1e-8f) return false;
        const float x=dot(error,dx),y=dot(error,dy);
        const float dp=std::clamp((b*y-c*x)/det,-4.f,4.f),dq=std::clamp((b*x-a*y)/det,-4.f,4.f);
        if(!std::isfinite(dp)||!std::isfinite(dq)) return false;
        pitch=std::clamp(pitch+dp,-89.f,89.f);yaw=std::remainder(yaw+dq,360.f);return true;
    }
}
