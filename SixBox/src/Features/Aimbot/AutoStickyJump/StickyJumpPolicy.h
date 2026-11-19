#pragma once
#include <algorithm>
#include <cmath>
#include "../Shared/StickyJumpMath.h"

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
    using StickyJumpMath::LeadDistance;
    inline float CreationTime(float raw,float now,float started,float observed,bool old)
    {
        return std::isfinite(raw)&&raw>0&&raw<=now+.25f&&(old||raw>=started-.5f)?raw:observed;
    }
    using StickyJumpMath::LaunchWindow;
    using StickyJumpMath::FindWindow;
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
