#pragma once
#include <algorithm>
#include <cmath>

namespace StickyJump
{
    enum class Phase {Plan,Shoot,Wait,Approach,Jump,Blast,Air,Done,Failed,Flush};
    inline bool OwnsCommand(int planned,int original) {return planned>=0&&planned==original;}
    inline float ArmTime(float base,float attribute,int rune)
    {
        if(!std::isfinite(base)||!std::isfinite(attribute)||base<0||attribute<0) return -1;
        const float result=base*attribute*(rune==1?.5f:rune==2?.75f:1.f);
        return std::isfinite(result)?result:-1.f;
    }
    inline bool Armed(float now,float created,float duration,float interval)
    {
        return std::isfinite(now)&&std::isfinite(created)&&std::isfinite(duration)&&std::isfinite(interval)
            &&created>0&&duration>=0&&interval>0&&now>=created+duration+interval;
    }
    inline int BurstTicks(float rate,float interval,int available)
    {
        if(!std::isfinite(rate)||!std::isfinite(interval)||rate<=0||interval<=0||available<2) return 0;
        return (std::min)(available,int(std::ceil((std::min)(rate/interval,128.f)))+2);
    }
    inline bool ReleaseCharge(int command,int pressedCommand,float chargeBegin,bool aligned)
    {
        return pressedCommand>=0&&command>pressedCommand&&std::isfinite(chargeBegin)&&chargeBegin>0&&aligned;
    }
    inline bool DetonateWindow(float nowScore,float futureScore,int ageTicks)
    {
        return std::isfinite(nowScore)&&std::isfinite(futureScore)&&nowScore>0
            &&(nowScore>=futureScore*.98f||ageTicks>=7);
    }
    inline float LeadDistance(float speed,float runSpeed,float acceleration,float delay,float minimum)
    {
        if(!std::isfinite(speed)||!std::isfinite(runSpeed)||!std::isfinite(acceleration)||!std::isfinite(delay)||!std::isfinite(minimum)
            ||speed<0||runSpeed<=0||acceleration<=0||delay<0) return -1;
        // Acceleration only; the planner never requests a lower ground speed.
        const float ramp=std::clamp((runSpeed-speed)/acceleration,0.f,delay);
        const float distance=speed*ramp+.5f*acceleration*ramp*ramp+(std::max)(speed,runSpeed)*(delay-ramp);
        return (std::max)(minimum,distance);
    }
    inline float CreationTime(float raw,float now,float started,float observed,bool old)
    {
        return std::isfinite(raw)&&raw>0&&raw<=now+.25f&&(old||raw>=started-.5f)?raw:observed;
    }
    struct LaunchWindow
    {
        float time=0,score=0,current=0;
        bool valid=false;
        bool Now(float interval) const {return valid&&time<=interval&&current>0&&current>=score*.94f;}
    };
    // Search continuous blast geometry, not a seven-HU distance to a fixed marker.
    // Half-tick samples cover a window crossed between two commands.
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
    struct FlushGate
    {
        bool hadStickies=false;
        int emptyCommands=0,lastCommand=-1;
        bool Done(int visible,int native,int command)
        {
            if(visible>0||native>0) {hadStickies=true;emptyCommands=0;lastCommand=command;return false;}
            if(!hadStickies) return true;
            if(command!=lastCommand) {++emptyCommands;lastCommand=command;}
            return emptyCommands>=2;
        }
    };
    struct FireGate
    {
        bool released=false;
        float releaseTime=0,next=0;
        int clip=0;
        void Release(float now,float rate,int ammunition)
        {released=true;releaseTime=now;next=now+(std::max)(0.f,rate);clip=ammunition;}
        bool Cleared(float now,float nativeNext,float charge,int ammunition,int seen,int sent,float interval)
        {
            if(!released) return true;
            // The old charge may survive a few predicted commands after release.
            // Do not let it masquerade as the next sticky's charge.
            const bool consumed=ammunition<clip||seen>=sent||nativeNext>releaseTime+interval;
            if(charge<=0&&consumed&&now>releaseTime) {released=false;next=(std::max)(next,nativeNext);return true;}
            return false;
        }
        bool Ready(float now,float nativeNext,float charge,float interval) const
        {return !released&&charge<=0&&now+1e-5f>=(std::max)(next,nativeNext)+interval;}
    };
    inline const char* Label(Phase phase)
    {
        switch(phase)
        {
        case Phase::Plan:return "Finding launch point";
        case Phase::Flush:return "Flushing existing stickies";
        case Phase::Shoot:return "Placing stickies";
        case Phase::Wait:return "Waiting for sticky confirmation";
        case Phase::Approach:return "Approach / arming";
        case Phase::Jump:return "Crouch jump";
        case Phase::Blast:return "Detonating";
        case Phase::Air:return "Airborne";
        case Phase::Done:return "Finished - toggle to repeat";
        default:return "Cancelled - toggle to retry";
        }
    }
}
