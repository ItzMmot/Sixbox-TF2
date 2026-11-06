#pragma once
#include "../Shared/AimPolicy.h"
#include <array>

// TF2 units: HU, HU/s, seconds. This policy holds no engine/entity references.
namespace SpyKnife
{
    using V=AimPolicy::Vector;
    constexpr int MaxTicks=21,Routes=5;
    inline V Add(V a,V b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
    inline V Sub(V a,V b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
    inline V Mul(V a,float n) {return {a.x*n,a.y*n,a.z*n};}
    inline float Dot(V a,V b) {return a.x*b.x+a.y*b.y;}
    inline float Length(V a) {return std::hypot(a.x,a.y);}
    inline V Unit(V a) {const float n=Length(a);return std::isfinite(n)&&n>1e-6f?V{a.x/n,a.y/n,0}:V{};}
    inline int AvailableTicks(int charged,int choked,int serverLimit)
    {
        if(charged<=0||choked<0||choked>=MaxTicks||serverLimit<=choked+1) return 0;
        return (std::min)({charged,MaxTicks-choked,serverLimit-choked-1});
    }
    inline bool Stable(int start,int command,int ticks,int ownerSpawn,int currentOwnerSpawn,
        int targetSpawn,int currentTargetSpawn,float positionError,float yawError)
    {
        return command>start&&int64_t(command)-start<=ticks&&ownerSpawn==currentOwnerSpawn
            &&targetSpawn==currentTargetSpawn&&std::isfinite(positionError)&&positionError<=16.f
            &&std::isfinite(yawError)&&std::abs(yawError)<=25.f;
    }
    inline V RouteWish(V owner,V target,V targetForward,float radius,int route)
    {
        targetForward=Unit(targetForward);
        if(Length(targetForward)<.5f||!std::isfinite(radius)||radius<1||route<0||route>=Routes) return {};
        const V right{-targetForward.y,targetForward.x,0};
        V goal=Sub(target,Mul(targetForward,radius));
        if(route)
        {
            const float side=route&1?1.f:-1.f;
            const float angle=(route<=2?110.f:150.f)*3.14159265358979323846f/180.f;
            goal=Add(target,Add(Mul(targetForward,std::cos(angle)*radius),Mul(right,side*std::sin(angle)*radius)));
            const V delta=Sub(owner,target);
            // Clear the victim's hull before crossing its facing plane.
            if(Dot(delta,targetForward)>0&&Dot(delta,right)*side<radius*.85f)
                goal=Add(target,Add(Mul(targetForward,(std::max)(Dot(delta,targetForward),8.f)),Mul(right,side*radius)));
        }
        const V delta=Sub(goal,owner);
        return Length(delta)>2.f?Mul(Unit(delta),450.f):V{};
    }
    constexpr int TrickTicks=48,TrickRoutes=7;
    inline V Rotate(V v,float degrees)
    {
        const float a=degrees*3.14159265358979323846f/180.f,c=std::cos(a),s=std::sin(a);
        return {v.x*c-v.y*s,v.x*s+v.y*c,0};
    }
    inline float Safety(float value) {return std::isfinite(value)?std::clamp(value,0.f,1.f):1.f;}
    inline bool SafeGeometry(V separation,V owner,V victim,float safety,float extraYaw=0)
    {
        safety=Safety(safety);owner=Unit(owner);victim=Unit(victim);
        const float yaw=4.f+10.f*safety+std::clamp(std::abs(extraYaw),0.f,12.f);
        const float error=1.f+3.f*safety;
        if(Length(owner)<.5f||Length(victim)<.5f||!std::isfinite(extraYaw)) return false;
        // All corners and both yaw bounds must remain in a padded native cone.
        for(float x:{-error,error}) for(float y:{-error,error})
        {
            const V direction=Unit(Add(separation,{x,y,0}));
            if(Length(direction)<.5f||Dot(direction,owner)<.6f+.08f*safety) return false;
            for(float angle:{-yaw,yaw})
            {
                const V forward=Rotate(victim,angle);
                if(Dot(direction,forward)<.08f+.2f*safety||Dot(owner,forward)<-.15f+.1f*safety) return false;
            }
        }
        return true;
    }
    constexpr int ArrivalTicks=12;
    // Real elapsed time until movement acknowledgement + a fresh attack command.
    // Shifted commands do not advance the victim once per charged tick.
    inline int ArrivalWindow(float latency,float interval)
    {
        if(!std::isfinite(latency)||latency<0||!std::isfinite(interval)||interval<=0) return 0;
        const float ticks=std::ceil(latency/interval)+3.f;
        return ticks<=ArrivalTicks?int(ticks):0;
    }
    template<class Check> bool ArrivalContactWindow(int ticks,Check&& check)
    {
        if(ticks<3||ticks>ArrivalTicks) return false;
        for(int sample:{0,ticks/2,ticks}) if(!check(sample)) return false;
        return true;
    }
    inline float ArrivalReserve(float safety) {return 8.f+4.f*Safety(safety);}
    inline float RearRadius(V ownerHalf,V targetHalf,V forward)
    {
        forward=Unit(forward);
        // Rectangular hull support along facing, rather than circumscribed circles.
        return std::abs(forward.x)*(ownerHalf.x+targetHalf.x)
            +std::abs(forward.y)*(ownerHalf.y+targetHalf.y)+4.f;
    }
    inline V ChaseWish(V owner,V target,V forward,V velocity,float radius)
    {
        if(!std::isfinite(velocity.x)||!std::isfinite(velocity.y)||!std::isfinite(radius)||radius<=0) return {};
        const V goal=Sub(target,Mul(Unit(forward),radius));
        V wish=Add(V{velocity.x,velocity.y,0},Mul(Sub(goal,owner),8.f));
        const float speed=Length(wish);
        if(!std::isfinite(speed)) return {};
        return speed>450.f?Mul(wish,450.f/speed):wish;
    }
    inline bool CalibratedDrift(float position,float yaw,float safety)
    {
        safety=Safety(safety);
        return std::isfinite(position)&&position>=0&&std::isfinite(yaw)
            &&position<=8.f-4.f*safety&&std::abs(yaw)<=15.f-10.f*safety;
    }
    inline bool AckReady(int command,int finish,int acknowledged,int expiry)
    {return command>finish&&command<=expiry&&acknowledged>=finish;}
    inline V TrickWish(V owner,V target,V forward,float radius,int route,int phase)
    {
        if(route<Routes) return RouteWish(owner,target,forward,radius,route);
        if(route>=TrickRoutes) return {};
        const int side=route==5?1:-1;
        if(phase<6)
        {
            forward=Unit(forward);const V right{-forward.y,forward.x,0};
            const V bait=Add(target,Add(Mul(forward,radius*.6f),Mul(right,float(side)*radius)));
            return Mul(Unit(Sub(bait,owner)),450.f);
        }
        return RouteWish(owner,target,forward,radius,side>0?2:1);
    }

}
