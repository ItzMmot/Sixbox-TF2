#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Burst
{
    constexpr int MaxCommands=32, MaxDelay=8;
    struct Timing
    {
        float interval=.015f,fireDelay=.1f;
        int tickBase=0,extras=0,ammo=0;
        // Minigun loses its firing state after a long release of the trigger.
        float maximumRelease=0;
        bool Valid() const
        {
            return std::isfinite(interval)&&interval>=.001f&&interval<=.1f
                &&std::isfinite(fireDelay)&&fireDelay>0&&fireDelay<=10
                &&tickBase>=0&&tickBase<=std::numeric_limits<int>::max()-MaxCommands-MaxDelay&&extras>=0&&extras<MaxCommands&&ammo>0
                &&std::isfinite(maximumRelease)&&maximumRelease>=0;
        }
        int NextShot(int previous) const
        {
            if(!Valid()||previous<0||previous>=MaxCommands) return MaxCommands;
            // Use the same absolute float clock as weapon timers, rather than
            // rounding fireDelay once and accumulating that rounded interval.
            const float due=float((double(tickBase)+previous)*interval)+fireDelay;
            for(int offset=previous+1;offset<MaxCommands;++offset)
                if(float((double(tickBase)+offset)*interval)>=due) return offset;
            return MaxCommands;
        }
        bool CanReleaseUntil(int previous,int next) const
        {
            return maximumRelease==0||float(next-previous-1)*interval<maximumRelease;
        }
    };
    struct Score
    {
        float hits=0,open=0;
        bool valid=false;
    };
    using Scores=std::array<std::array<Score,MaxCommands>,MaxDelay+1>;
    struct Plan
    {
        std::array<int,MaxCommands> shots{};
        std::uint32_t withhold=0;
        float value=0;
        int count=0,extras=0,startDelay=0;
        bool valid=false;
        bool Withholds(int offset) const
        {
            return valid&&offset>0&&offset<=extras&&(withhold&(std::uint32_t(1)<<offset));
        }
    };
    inline bool Usable(const Score& score)
    {
        return score.valid&&std::isfinite(score.hits)&&std::isfinite(score.open)
            &&score.hits>=0&&score.open>=score.hits;
    }
    inline Plan Schedule(const Timing& timing,const Scores& scores,int delayLimit=MaxDelay)
    {
        Plan best;
        if(!timing.Valid()) return best;
        delayLimit=std::clamp(delayLimit,0,MaxDelay);
        const int rounds=(std::min)(timing.ammo,MaxCommands);
        // DP state: last firing command and rounds left. The first shot arms
        // the shift; later attacks may move inside its fixed command horizon.
        std::array<std::array<float,MaxCommands+1>,MaxCommands> value{};
        std::array<std::array<int,MaxCommands+1>,MaxCommands> choice{};
        const float penalty=scores[0][0].open*timing.interval/timing.fireDelay;
        if(!Usable(scores[0][0])) return best;
        for(int delay=0;delay<=delayLimit;++delay)
        {
            if(!Usable(scores[delay][0])) continue;
            Timing clock=timing;clock.tickBase+=delay;
            for(auto& row:value) row.fill(0);
            for(auto& row:choice) row.fill(-1);
            for(int remaining=1;remaining<rounds;++remaining)
            {
                for(int previous=timing.extras;previous>=0;--previous)
                {
                    const int due=clock.NextShot(previous);
                    for(int at=due;at<=timing.extras;++at)
                    {
                        if(!clock.CanReleaseUntil(previous,at)) break;
                        if(!Usable(scores[delay][at])) continue;
                        const float candidate=scores[delay][at].hits-penalty*float(at-due)+value[at][remaining-1];
                        if(candidate>value[previous][remaining]+1e-6f)
                        {value[previous][remaining]=candidate;choice[previous][remaining]=at;}
                    }
                }
            }
            const float total=scores[delay][0].hits+value[0][rounds-1]-penalty*float(delay);
            if(best.valid&&total<=best.value+1e-6f) continue;
            Plan plan;plan.valid=true;plan.extras=timing.extras;plan.startDelay=delay;plan.value=total;
            plan.shots[plan.count++]=0;
            int last=0,left=rounds-1;
            while(left>0&&plan.count<MaxCommands)
            {
                const int at=choice[last][left];
                if(at<0) break;
                plan.shots[plan.count++]=at;last=at;--left;
            }
            // Keep holding attack during cooldown. Only release at readiness
            // boundaries where a shot was deliberately moved or omitted.
            int shot=0;
            for(int offset=1;offset<=timing.extras;++offset)
            {
                while(shot+1<plan.count&&plan.shots[shot+1]<=offset) ++shot;
                if(offset>=clock.NextShot(plan.shots[shot]))
                    plan.withhold|=std::uint32_t(1)<<offset;
            }
            best=plan;
        }
        return best;
    }
}
