#pragma once
#include <algorithm>
#include <cmath>

// Calculation routines shared with the original Auto Sticky Jump planner.
// Neither controller, settings nor command ownership are shared.
namespace StickyJumpMath
{
    inline float LeadDistance(float speed,float runSpeed,float acceleration,float delay,float minimum)
    {
        if(!std::isfinite(speed)||!std::isfinite(runSpeed)||!std::isfinite(acceleration)||!std::isfinite(delay)||!std::isfinite(minimum)
            ||speed<0||runSpeed<=0||acceleration<=0||delay<0) return -1;
        const float ramp=std::clamp((runSpeed-speed)/acceleration,0.f,delay);
        const float distance=speed*ramp+.5f*acceleration*ramp*ramp+(std::max)(speed,runSpeed)*(delay-ramp);
        return (std::max)(minimum,distance);
    }
    // Low ballistic arc from the real muzzle; 200 HU/s is the launcher's
    // upward grenade impulse. The engine adapter still verifies the flight.
    inline bool LowArc(float distance,float height,float velocity,float gravity,float& pitch)
    {
        if(!std::isfinite(distance)||!std::isfinite(height)||!std::isfinite(velocity)||!std::isfinite(gravity)
            ||distance<1.f||velocity<=0||gravity<=0) return false;
        const float speed=std::hypot(velocity,200.f),squared=speed*speed;
        const float disc=squared*squared-gravity*(gravity*distance*distance+2.f*height*squared);
        if(disc<0||!std::isfinite(disc)) return false;
        pitch=(-std::atan((squared-std::sqrt(disc))/(gravity*distance))+std::atan2(200.f,velocity))*180.f/3.141592654f;
        pitch=std::clamp(pitch,-89.f,89.f);
        return std::isfinite(pitch);
    }
    struct LaunchWindow
    {
        float time=0,score=0,current=0;
        bool valid=false;
        bool Now(float interval) const {return valid&&time<=interval&&current>0&&current>=score*.94f;}
    };
    template<class Score> LaunchWindow FindWindow(float interval,float armRemaining,Score&& score)
    {
        LaunchWindow result;
        if(!std::isfinite(interval)||interval<=0||!std::isfinite(armRemaining)) return result;
        const float delay=interval*2.f;
        const float horizon=std::clamp(armRemaining+.2f,.5f,2.f);
        const int steps=(std::min)(512,int(std::ceil(horizon/(interval*.5f))));
        for(int i=0;i<=steps;++i)
        {
            const float time=i*interval*.5f;
            if(time+delay+1e-5f<armRemaining) continue;
            const float value=score(time,delay);
            if(i==0&&std::isfinite(value)) result.current=value;
            if(std::isfinite(value)&&value>result.score)
                {result.time=time;result.score=value;result.valid=value>0;}
        }
        return result;
    }
}
