#pragma once
#include "AimPolicy.h"
#include <cstdint>

// Remote observations, Source HU/seconds. Replays contain no world references.
namespace AnimationTimeline
{
    using V=AimPolicy::Vector;
    inline V Add(V a,V b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
    inline V Sub(V a,V b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
    inline V Scale(V a,float b) {return {a.x*b,a.y*b,a.z*b};}
    inline float Length2(V a) {return a.x*a.x+a.y*a.y+a.z*a.z;}
    inline bool Finite(V a) {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
    struct Observation
    {
        V origin{},mins{},maxs{},velocity{},angles{};
        int tick=0,handle=0,spawn=0,model=0,playerClass=0,parent=0,interpolation=0,flags=0,water=0;
        float scale=1;
        bool valid=false;
    };
    constexpr int OnGround=1,Ducking=2,MaxTicks=24;
    inline bool Valid(const Observation& a)
    {
        return a.valid&&a.tick>=0&&a.tick<std::numeric_limits<int>::max()-MaxTicks
            &&std::isfinite(a.scale)&&a.scale>0&&Finite(a.origin)
            &&Finite(a.mins)&&Finite(a.maxs)&&Finite(a.velocity)&&Finite(a.angles)
            &&a.mins.x<=a.maxs.x&&a.mins.y<=a.maxs.y&&a.mins.z<=a.maxs.z;
    }
    template<int Capacity>
    struct LayerOrder
    {
        struct Layer {int sequence=-1,order=Capacity;bool weighted=false,finished=false;};
        std::array<Layer,Capacity> layers{};
        void BeginTick()
        {
            for(auto& layer:layers)
            {
                if(!layer.finished) continue;
                const int removed=layer.order;
                for(auto& other:layers) if(other.order>removed&&other.order<Capacity) --other.order;
                layer.order=Capacity;layer.finished=false;
            }
        }
        int Observe(int index,int sequence,bool weighted,bool restarted=false)
        {
            if(index<0||index>=Capacity) return Capacity;
            auto& layer=layers[index];
            if(weighted&&(!layer.weighted||layer.sequence!=sequence||restarted))
            {
                const int old=layer.order;
                for(auto& other:layers) if(other.order>old&&other.order<Capacity) --other.order;
                int order=0;
                for(int i=0;i<Capacity;++i) if(i!=index&&layers[i].order<Capacity) order=(std::max)(order,layers[i].order+1);
                layer.order=(std::min)(order,Capacity-1);layer.finished=false;
            }
            else if(!weighted&&layer.weighted) layer.finished=true;
            layer.sequence=sequence;layer.weighted=weighted;return layer.order;
        }
    };
    inline bool SameLife(const Observation& a,const Observation& b)
    {
        return Valid(a)&&Valid(b)&&a.handle==b.handle&&a.spawn==b.spawn&&a.model==b.model
            &&a.playerClass==b.playerClass&&a.parent==b.parent&&a.interpolation==b.interpolation&&a.scale==b.scale;
    }
    inline bool Continuous(const Observation& a,const Observation& b,float interval)
    {
        if(!SameLife(a,b)||b.tick<=a.tick||b.tick-a.tick>MaxTicks||!std::isfinite(interval)||interval<.001f||interval>.1f) return false;
        const float reach=64.f+std::sqrt((std::max)(Length2(a.velocity),Length2(b.velocity)))*float(b.tick-a.tick)*interval;
        return Length2(Sub(b.origin,a.origin))<=reach*reach;
    }
    struct Sample {V origin{},mins{},maxs{},velocity{},angles{};int tick=0,flags=0,water=0;};
    struct Plan
    {
        Observation from{},to{};
        float interval=.015f,gravity=800;
        int first=0,count=0,jump=-1,land=-1,airDash=-1;
        bool continuous=false;
        static constexpr float JumpSpeed=268.3281573f;
        float HeightAt(int tick,float* velocity=nullptr) const
        {
            const int command=tick-from.tick;
            if(command<=0) {if(velocity) *velocity=from.velocity.z;return from.origin.z;}
            const int jumpAt=jump>=0?jump-from.tick:std::numeric_limits<int>::max();
            const int dashAt=airDash>=0?airDash-from.tick:std::numeric_limits<int>::max();
            float z=from.origin.z,v=from.flags&OnGround?0.f:from.velocity.z;
            for(int step=1;step<=command;++step)
            {
                if(step==jumpAt||step==dashAt) v=JumpSpeed;
                const bool grounded=(from.flags&OnGround)&&step<jumpAt&&step<dashAt;
                if(grounded) continue;
                z+=v*interval-.5f*gravity*interval*interval;v-=gravity*interval;
            }
            if(velocity) *velocity=v;
            return z;
        }
        Sample At(int tick) const
        {
            Sample result;result.tick=std::clamp(tick,first,to.tick);
            const int gap=continuous?to.tick-from.tick:1;
            const float fraction=continuous?float(result.tick-from.tick)/float(gap):1.f;
            auto lerp=[&](V a,V b){return Add(a,Scale(Sub(b,a),fraction));};
            result.origin=lerp(from.origin,to.origin);result.mins=lerp(from.mins,to.mins);result.maxs=lerp(from.maxs,to.maxs);
            result.angles=lerp(from.angles,to.angles);
            result.angles.y=std::remainder(from.angles.y+std::remainder(to.angles.y-from.angles.y,360.f)*fraction,360.f);
            const int movement=OnGround|Ducking;
            result.flags=result.tick==to.tick?to.flags:(to.flags&~movement)|(from.flags&movement);
            result.water=result.tick==to.tick?to.water:from.water;
            if(jump>=0) {if(result.tick>=jump) result.flags&=~OnGround;else if(result.tick==jump-1) result.flags|=OnGround;}
            if(land>=0&&result.tick>=land) result.flags|=OnGround;
            result.velocity=continuous?Scale(Sub(to.origin,from.origin),1.f/(gap*interval)):to.velocity;
            if(continuous&&(jump>=0||airDash>=0))
            {
                // Fit unknown impulse timing, then anchor the model to both
                // observations so attribute/quantization error cannot move the endpoint.
                const float residual=to.origin.z-HeightAt(to.tick);
                const int impulse=jump>=0?jump:airDash;
                const float duration=float(to.tick-impulse+1)*interval;
                const float correction=float((std::max)(result.tick-impulse+1,0))/float(to.tick-impulse+1);
                result.origin.z=HeightAt(result.tick,&result.velocity.z)+residual*correction;
                if(result.tick>=impulse) result.velocity.z+=residual/duration;
                if(airDash>=0&&result.tick>=airDash) result.flags&=~OnGround;
            }
            else if(continuous&&!(from.flags&OnGround)&&!(to.flags&OnGround)&&jump<0)
            {
                // Endpoint-constrained parabola. Duck hull-height changes are
                // removed from the motion estimate instead of becoming speed.
                const float duration=gap*interval;
                const float hullChange=(to.maxs.z-to.mins.z)-(from.maxs.z-from.mins.z);
                result.velocity.z+=hullChange/duration+gravity*duration*(.5f-fraction);
                result.origin.z+=.5f*gravity*duration*duration*fraction*(1.f-fraction);
            }
            if(result.flags&OnGround) result.velocity.z=0;
            // The received endpoint must remain bit-for-bit the observation.
            if(result.tick==to.tick) {result.origin=to.origin;result.mins=to.mins;result.maxs=to.maxs;result.angles=to.angles;result.flags=to.flags;result.water=to.water;}
            if(result.flags&OnGround) result.velocity.z=0;
            return result;
        }
    };
    inline Plan Build(Observation from,Observation to,float interval,float gravity,int jumpHint=-1,bool doubleJump=false)
    {
        Plan plan;plan.from=from;plan.to=to;plan.interval=interval;plan.gravity=std::isfinite(gravity)&&gravity>=0?gravity:800;
        if(!Valid(to)||!std::isfinite(interval)||interval<.001f||interval>.1f) return plan;
        plan.continuous=Continuous(from,to,interval);
        plan.first=plan.continuous?from.tick+1:to.tick;plan.count=to.tick-plan.first+1;
        if(!plan.continuous) {plan.from=to;return plan;}
        if(jumpHint>=plan.first&&jumpHint<=to.tick) plan.jump=jumpHint;
        else if((from.flags&OnGround)&&!(to.flags&OnGround))
        {
            float best=std::numeric_limits<float>::max();
            // Fit the observed endpoint to each possible launch command.
            for(int tick=plan.first;tick<=to.tick;++tick)
            {
                Plan candidate=plan;candidate.jump=tick;
                if(doubleJump&&tick+2<=to.tick) candidate.airDash=tick+2;
                const float error=std::abs(candidate.HeightAt(to.tick)-to.origin.z);
                if(error<best) {best=error;plan.jump=tick;}
            }
        }
        if(doubleJump)
        {
            float best=std::numeric_limits<float>::max();
            const int earliest=plan.jump>=0?(std::min)(plan.jump+2,to.tick):plan.first;
            for(int tick=earliest;tick<=to.tick;++tick)
            {
                Plan candidate=plan;candidate.airDash=tick;
                const float error=std::abs(candidate.HeightAt(to.tick)-to.origin.z);
                if(error<best) {best=error;plan.airDash=tick;}
            }
        }
        if(!(from.flags&OnGround)&&(to.flags&OnGround))
        {
            plan.land=to.tick;
            for(int tick=plan.first;tick<=to.tick;++tick)
            {
                const float duration=float(tick-from.tick)*interval;
                const float z=from.origin.z+from.velocity.z*duration-.5f*plan.gravity*duration*duration;
                if(z<=to.origin.z+.03125f) {plan.land=tick;break;}
            }
        }
        return plan;
    }
    struct Identity
    {
        std::uint32_t revision=0;
        int handle=0,spawn=0,hypothesis=-1;
        bool operator==(const Identity&) const=default;
        bool Valid() const {return revision!=0;}
    };
    // Do not punish a newer hypothesis/life for an earlier shot's result.
    inline bool FeedbackApplies(const Identity& shot,const Identity& current)
    {return shot.Valid()&&shot==current;}
}
