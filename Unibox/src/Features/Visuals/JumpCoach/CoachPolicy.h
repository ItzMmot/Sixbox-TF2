#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Source units: HU, HU/s, seconds, degrees. All storage is bounded; no entities.
namespace JumpCoach
{
    constexpr int MaxTicks=512, MaxPlanes=5;
    constexpr float Pi=3.14159265358979323846f;
    struct Vector
    {
        float x=0,y=0,z=0;
        Vector operator+(Vector b) const {return {x+b.x,y+b.y,z+b.z};}
        Vector operator-(Vector b) const {return {x-b.x,y-b.y,z-b.z};}
        Vector operator*(float n) const {return {x*n,y*n,z*n};}
        float Dot(Vector b) const {return x*b.x+y*b.y+z*b.z;}
        float Squared() const {return Dot(*this);}
        float Horizontal() const {return std::sqrt(x*x+y*y);}
        bool Finite() const {return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);}
    };
    inline Vector Cross(Vector a,Vector b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    inline Vector Unit(Vector v) {const float n=v.Squared();return n>1e-8f&&std::isfinite(n)?v*(1.f/std::sqrt(n)):Vector{};}
    inline float Wrap(float angle) {return std::isfinite(angle)?std::remainder(angle,360.f):0.f;}
    struct Physics
    {
        float interval=.015f,gravity=800.f,airAcceleration=10.f,airCap=30.f;
        float wishSpeed=240.f,friction=1.f,maxVelocity=3500.f;
        bool Valid() const
        {
            return std::isfinite(interval)&&interval>=.001f&&interval<=.1f && std::isfinite(gravity)&&gravity>=0&&gravity<=10000
                && std::isfinite(airAcceleration)&&airAcceleration>=0&&airAcceleration<=10000
                && std::isfinite(airCap)&&airCap>0&&airCap<=10000 && std::isfinite(wishSpeed)&&wishSpeed>0&&wishSpeed<=10000
                && std::isfinite(friction)&&friction>=0&&friction<=10 && std::isfinite(maxVelocity)&&maxVelocity>0&&maxVelocity<=100000;
        }
    };
    inline Vector Accelerate(Vector velocity,Vector wish,float wishSpeed,const Physics& p)
    {
        if(wish.Squared()<.5f || wishSpeed<=0) return velocity;
        const float cap=(std::min)(p.airCap,wishSpeed);
        const float available=cap-velocity.Dot(wish);
        const float gain=(std::max)(0.f,(std::min)(available,p.airAcceleration*wishSpeed*p.interval*p.friction));
        return velocity+wish*gain;
    }
    struct Strafe
    {
        Vector wish;
        float wishYaw=0,cameraYaw=0,gain=0,efficiency=0,actualGain=0;
        int side=1; // +1: left/A; -1: right/D, in Source world yaw.
        bool valid=false;
    };
    inline Strafe Guide(Vector velocity,Vector input,float inputSpeed,float viewYaw,int side,const Physics& p)
    {
        Strafe result;result.side=side<0?-1:1;
        const float speed=velocity.Horizontal();
        if(!p.Valid() || !velocity.Finite() || !input.Finite() || !std::isfinite(viewYaw) || !std::isfinite(inputSpeed) || speed<1.f) return result;
        const float acceleration=p.airAcceleration*p.wishSpeed*p.interval*p.friction;
        if(acceleration<=0) return result;
        const float cap=(std::min)(p.airCap,p.wishSpeed);
        // Maximum one-tick XY speed: projected velocity is cap-A when A<cap,
        // otherwise zero. Negative projection loses speed versus a perpendicular wish.
        const float projection=(std::max)(0.f,cap-acceleration);
        const float cosine=std::clamp(projection/speed,0.f,1.f);
        const float sine=std::sqrt((std::max)(0.f,1.f-cosine*cosine));
        const Vector forward={velocity.x/speed,velocity.y/speed,0};
        result.wish=forward*cosine+Vector{-forward.y,forward.x,0}*(sine*result.side);
        result.wishYaw=std::atan2(result.wish.y,result.wish.x)*180.f/Pi;
        result.cameraYaw=Wrap(result.wishYaw-result.side*90.f);
        result.gain=Accelerate(velocity,result.wish,p.wishSpeed,p).Horizontal()-speed;
        result.actualGain=Accelerate(velocity,input,std::clamp(inputSpeed,0.f,p.wishSpeed),p).Horizontal()-speed;
        result.valid=std::isfinite(result.gain)&&result.gain>1e-5f;
        if(result.valid) result.efficiency=std::clamp(result.actualGain/result.gain,0.f,1.f);
        return result;
    }
    struct Body {Vector origin,velocity,mins,maxs;};
    struct Trace {float fraction=1;Vector normal;bool solid=false,sky=false;};
    using TraceFn=Trace(*)(void*,Vector,Vector,Vector,Vector);
    enum class End {Invalid,Grounded,Landed,Horizon,Blocked,Budget};
    struct Sample {Vector origin,velocity;float time=0;};
    struct Plan
    {
        std::array<Sample,MaxTicks+1> points{};
        Vector landing,normal;
        End end=End::Invalid;
        int count=0,traces=0;
        bool collision=false;
    };
    inline Vector Clip(Vector velocity,Vector normal)
    {
        Vector out=velocity-normal*(std::min)(0.f,velocity.Dot(normal));
        if(std::abs(out.x)<.1f) out.x=0;
        if(std::abs(out.y)<.1f) out.y=0;
        if(std::abs(out.z)<.1f) out.z=0;
        return out;
    }
    inline Vector SurfacePoint(const Body& body,Vector normal)
    {
        return body.origin+Vector{normal.x>.001f?body.mins.x:normal.x<-.001f?body.maxs.x:0,
            normal.y>.001f?body.mins.y:normal.y<-.001f?body.maxs.y:0,body.mins.z};
    }
    inline void Predict(Body body,Vector input,float inputSpeed,const Physics& physics,int ticks,int budget,
        TraceFn trace,void* context,bool optimal,int side,Plan& plan)
    {
        plan.count=plan.traces=0;plan.end=End::Invalid;plan.collision=false;plan.landing=plan.normal={};
        if(!physics.Valid() || !trace || !body.origin.Finite() || !body.velocity.Finite() || !body.mins.Finite() || !body.maxs.Finite()
            || body.mins.x>body.maxs.x || body.mins.y>body.maxs.y || body.mins.z>body.maxs.z
            || !input.Finite() || !std::isfinite(inputSpeed) || inputSpeed<0) return;
        plan.points[plan.count++]={body.origin,body.velocity,0};
        auto cast=[&](Vector end,Trace& hit) {
            if(plan.traces>=budget) {plan.end=End::Budget;return false;}
            ++plan.traces;hit=trace(context,body.origin,end,body.mins,body.maxs);
            const bool valid=std::isfinite(hit.fraction)&&hit.fraction>=0&&hit.fraction<=1&&hit.normal.Finite()
                && !hit.solid && (hit.fraction>=1 || (hit.normal.Squared()>.9f&&hit.normal.Squared()<1.1f));
            if(!valid) plan.end=End::Blocked;
            return valid;
        };
        auto land=[&](Vector normal,float time) {
            body.velocity=Clip(body.velocity,normal);body.velocity.z=0;
            plan.normal=normal;plan.landing=SurfacePoint(body,normal);plan.end=End::Landed;
            plan.points[plan.count++]={body.origin,body.velocity,time};
        };
        ticks=std::clamp(ticks,1,MaxTicks);budget=std::clamp(budget,0,16384);
        for(int tick=0;tick<ticks;++tick)
        {
            body.velocity.z-=physics.gravity*physics.interval*.5f;
            const Vector wish=optimal?Guide(body.velocity,{},0,0,side,physics).wish:input;
            body.velocity=Accelerate(body.velocity,wish,optimal?physics.wishSpeed:inputSpeed,physics);
            for(float* v:{&body.velocity.x,&body.velocity.y,&body.velocity.z}) *v=std::clamp(*v,-physics.maxVelocity,physics.maxVelocity);
            float remaining=physics.interval,elapsed=0;
            const Vector primal=body.velocity;
            std::array<Vector,MaxPlanes> planes{};int planeCount=0;
            for(int bump=0;bump<4 && remaining>1e-6f;++bump)
            {
                const Vector destination=body.origin+body.velocity*remaining;
                Trace hit;
                if(!cast(destination,hit)) return;
                body.origin=body.origin+(destination-body.origin)*hit.fraction;
                elapsed+=remaining*hit.fraction;remaining*=1-hit.fraction;
                if(hit.fraction>=1) break;
                plan.collision=true;
                if(hit.sky) {plan.end=End::Blocked;return;}
                if(hit.normal.z>=.7f && body.velocity.z<=0) {land(hit.normal,tick*physics.interval+elapsed);return;}
                if(planeCount==MaxPlanes) {plan.end=End::Blocked;return;}
                planes[planeCount++]=hit.normal;
                const Vector original=body.velocity;
                bool found=false;
                for(int i=0;i<planeCount;++i)
                {
                    const Vector candidate=Clip(original,planes[i]);
                    bool clear=true;
                    for(int j=0;j<planeCount;++j) if(j!=i && candidate.Dot(planes[j])<-.01f) {clear=false;break;}
                    if(clear) {body.velocity=candidate;found=true;break;}
                }
                if(!found)
                {
                    if(planeCount==2) {const Vector crease=Unit(Cross(planes[0],planes[1]));body.velocity=crease*crease.Dot(original);}
                    else body.velocity={};
                }
                if(body.velocity.Dot(primal)<=0) body.velocity={};
                if(body.velocity.Squared()<1e-6f) {remaining=0;break;}
            }
            if(remaining>1e-6f && elapsed<physics.interval-1e-6f && body.velocity.Squared()>1e-6f)
            {plan.end=End::Blocked;return;}
            body.velocity.z-=physics.gravity*physics.interval*.5f;
            body.velocity.z=std::clamp(body.velocity.z,-physics.maxVelocity,physics.maxVelocity);
            if(!body.origin.Finite() || !body.velocity.Finite()) {plan.end=End::Invalid;return;}
            if(body.velocity.z<=140.f)
            {
                Trace ground;
                if(!cast(body.origin+Vector{0,0,-2},ground)) return;
                if(ground.fraction<1 && !ground.sky && ground.normal.z>=.7f)
                {body.origin.z-=2*ground.fraction;land(ground.normal,(tick+1)*physics.interval);return;}
            }
            plan.points[plan.count++]={body.origin,body.velocity,(tick+1)*physics.interval};
        }
        plan.end=End::Horizon;
    }
    inline bool AtTime(const Plan& plan,float time,Sample& sample)
    {
        if(plan.count<1 || !std::isfinite(time) || time<0 || time>plan.points[plan.count-1].time+1e-5f) return false;
        for(int i=1;i<plan.count;++i)
            if(time<=plan.points[i].time)
            {
                const auto& a=plan.points[i-1];const auto& b=plan.points[i];
                const float span=b.time-a.time;
                const float f=span>1e-6f?std::clamp((time-a.time)/span,0.f,1.f):0.f;
                sample={a.origin+(b.origin-a.origin)*f,a.velocity+(b.velocity-a.velocity)*f,time};return true;
            }
        sample=plan.points[0];return true;
    }
    inline float BlastDistance(Vector blast,Vector origin,Vector mins,Vector maxs)
    {
        const Vector nearest={std::clamp(blast.x,origin.x+mins.x,origin.x+maxs.x),
            std::clamp(blast.y,origin.y+mins.y,origin.y+maxs.y),std::clamp(blast.z,origin.z+mins.z,origin.z+maxs.z)};
        return std::sqrt((nearest-blast).Squared());
    }
    inline bool Aligned(Vector first,Vector second,float degrees=6.f)
    {
        if(!first.Finite() || !second.Finite() || first.Squared()<1e-6f || second.Squared()<1e-6f
            || !std::isfinite(degrees) || degrees<0 || degrees>180) return false;
        return Unit(first).Dot(Unit(second))>=std::cos(degrees*Pi/180.f);
    }
    struct Shot
    {
        float error=1e9f,delay=0,impact=0,travel=0;
        Vector point;
        bool found=false;
    };
    inline void ConsiderShot(Shot& best,float fireDelay,float travel,float targetImpact,float landing,float interval,Vector point)
    {
        const float impact=fireDelay+interval+travel;
        if(!std::isfinite(impact)||!std::isfinite(targetImpact)||!point.Finite() || fireDelay<0 || travel<=0
            || interval<=0 || impact>landing || impact<0) return;
        const float error=std::abs(impact-targetImpact);
        if(error<best.error) best={error,fireDelay,impact,travel,point,true};
    }
}
