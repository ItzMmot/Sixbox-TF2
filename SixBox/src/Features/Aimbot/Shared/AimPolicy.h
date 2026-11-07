#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// Independent SixBox implementation. Units: Source HU and seconds.
namespace AimPolicy
{
    struct Vector
    {
        float x=0,y=0,z=0;
        float At(int axis) const {return axis==0?x:axis==1?y:z;}
    };
    inline bool BackstabGeometry(Vector separation,Vector ownerForward,Vector victimForward)
    {
        auto normalize=[](Vector& v) {
            const float size=std::hypot(v.x,v.y);
            if(!std::isfinite(size)||size<=1e-6f) return false;
            v.x/=size;v.y/=size;v.z=0;return true;
        };
        if(!normalize(separation)||!normalize(ownerForward)||!normalize(victimForward)) return false;
        const float behind=separation.x*victimForward.x+separation.y*victimForward.y;
        const float facing=separation.x*ownerForward.x+separation.y*ownerForward.y;
        const float aligned=ownerForward.x*victimForward.x+ownerForward.y*victimForward.y;
        return behind>0.f&&facing>.5f&&aligned>-.3f;
    }
    // A ballistic estimate opens a small contact window; its first feasible
    // tick is not proof that a native projectile already reaches the target.
    // Bound both the number of native candidates and the extra time (seconds).
    inline bool DirectWindowOpen(float flight,float simulated,float bounds,float interval,int samples)
    {
        return std::isfinite(flight)&&std::isfinite(simulated)&&std::isfinite(bounds)
            &&std::isfinite(interval)&&interval>0&&samples<4
            &&simulated<flight+(std::min)((std::max)(bounds,interval),3.f*interval);
    }
    inline float LargerQuadraticRoot(float a,float b,float c)
    {
        const float discriminant=b*b-4.f*a*c;
        if(!std::isfinite(a)||a==0||!std::isfinite(discriminant)||discriminant<0)
            return std::numeric_limits<float>::quiet_NaN();
        return (-b+std::copysign(std::sqrt(discriminant),a))/(2.f*a);
    }
    inline float RayBox(Vector origin,Vector direction,Vector mins,Vector maxs,float limit)
    {
        float enter=0,leave=limit;
        if(!std::isfinite(limit)||limit<0) return -1;
        for(int i=0;i<3;++i)
        {
            const float o=origin.At(i),d=direction.At(i),lo=mins.At(i),hi=maxs.At(i);
            if(!std::isfinite(o)||!std::isfinite(d)||!std::isfinite(lo)||!std::isfinite(hi)||lo>hi) return -1;
            if(std::abs(d)<1e-8f) {if(o<lo||o>hi) return -1;continue;}
            float a=(lo-o)/d,b=(hi-o)/d;
            if(a>b) std::swap(a,b);
            enter=(std::max)(enter,a);leave=(std::min)(leave,b);
            if(enter>leave) return -1;
        }
        return enter;
    }
    template<class Matrix>
    float RayOBB(Vector eye,Vector direction,Vector mins,Vector maxs,const Matrix& matrix,float limit)
    {
        const float delta[3]={eye.x-matrix[0][3],eye.y-matrix[1][3],eye.z-matrix[2][3]};
        float local[3]{},ray[3]{};
        // Source player bone bases are orthogonal; inverse also accounts for model scale.
        for(int axis=0;axis<3;++axis)
        {
            float length=0;
            for(int row=0;row<3;++row) length+=matrix[row][axis]*matrix[row][axis];
            if(!std::isfinite(length)||length<1e-8f) return -1;
            for(int row=0;row<3;++row)
            {
                local[axis]+=delta[row]*matrix[row][axis]/length;
                ray[axis]+=direction.At(row)*matrix[row][axis]/length;
            }
        }
        return RayBox({local[0],local[1],local[2]},{ray[0],ray[1],ray[2]},mins,maxs,limit);
    }
    // acceleration is already in HU/s^2, not a multiplier of sv_gravity.
    inline Vector FlightVelocity(Vector launch,float acceleration,float seconds)
    {
        return {launch.x,launch.y,launch.z-acceleration*seconds};
    }
    struct ArrowBox {Vector mins,maxs;const float (*matrix)[4]=nullptr;};
    template<class Boxes>
    int ArrowContactHitbox(Vector contact,Vector velocity,float interval,int count,Boxes&& boxes)
    {
        const float speed=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z);
        if(!std::isfinite(speed)||speed<=0||!std::isfinite(interval)||interval<=0||count<=0) return -1;
        const Vector ray{velocity.x/speed,velocity.y/speed,velocity.z/speed};
        float nearest=speed*interval;int hit=-1;
        for(int i=0;i<count;++i)
        {
            const auto box=boxes(i);if(!box.matrix) continue;
            const float entry=RayOBB(contact,ray,box.mins,box.maxs,box.matrix,nearest);
            if(entry>=0&&(hit<0||entry<nearest)) {nearest=entry;hit=i;}
        }
        if(hit>=0) return hit;
        // ArrowTouch falls back to the nearest bone-aligned box from a point
        // 16 HU beyond the player-hull contact when its finite trace misses.
        const Vector sample{contact.x+ray.x*16,contact.y+ray.y*16,contact.z+ray.z*16};
        nearest=std::numeric_limits<float>::max();
        for(int i=0;i<count;++i)
        {
            const auto box=boxes(i);if(!box.matrix) continue;
            const Vector bone{box.matrix[0][3],box.matrix[1][3],box.matrix[2][3]};
            const Vector delta{bone.x-sample.x,bone.y-sample.y,bone.z-sample.z};
            const float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
            if(!std::isfinite(length)) continue;
            float distance=0;
            if(length>1e-6f)
            {
                const float entry=RayBox(sample,{delta.x/length,delta.y/length,delta.z/length},
                    {bone.x+box.mins.x,bone.y+box.mins.y,bone.z+box.mins.z},
                    {bone.x+box.maxs.x,bone.y+box.maxs.y,bone.z+box.maxs.z},length);
                distance=entry>=0?entry:length;
            }
            if(distance<nearest) {nearest=distance;hit=i;}
        }
        return hit;
    }
    struct SwingDeferral
    {
        int target=0,lastCommand=-1;bool spent=false;
        void Reset() {*this={};}
        bool Wait(int entity,int command)
        {
            if(target!=entity||command<lastCommand||command-lastCommand>2) {target=entity;spent=false;}
            const bool wait=!spent;spent=true;lastCommand=command;return wait;
        }
    };
    inline float BoxDistance(Vector point,Vector mins,Vector maxs)
    {
        float squared=0;
        for(int i=0;i<3;++i)
        {
            if(!std::isfinite(point.At(i))||!std::isfinite(mins.At(i))||!std::isfinite(maxs.At(i))||mins.At(i)>maxs.At(i))
                return std::numeric_limits<float>::infinity();
            const float delta=point.At(i)-std::clamp(point.At(i),mins.At(i),maxs.At(i));
            squared+=delta*delta;
        }
        return std::sqrt(squared);
    }
    inline float BlastScore(float distance,float radius,float time,float uncertainty=0)
    {
        if(!std::isfinite(distance)||!std::isfinite(radius)||!std::isfinite(time)||!std::isfinite(uncertainty)
            ||distance<0||radius<=0||time<0||uncertainty<0||distance>=radius) return 0;
        // Falloff estimate and remaining geometric tolerance; not exact server damage.
        const float falloff=1.f-.5f*distance/radius;
        const float tolerance=std::clamp((radius-distance)/(radius+uncertainty),0.f,1.f);
        return falloff*(.75f+.25f*tolerance)/(1.f+.05f*time);
    }
    inline bool PreferWindow(float margin,float nextMargin,float drift)
    {
        return std::isfinite(margin)&&std::isfinite(nextMargin)&&std::isfinite(drift)&&margin>0
            && !(drift>0&&margin<drift&&nextMargin>margin+1.f);
    }
    struct Budget
    {
        int remaining=0;
        bool Take(int cost=1) {if(cost<0||cost>remaining) return false;remaining-=cost;return true;}
    };
    inline float Confidence(float speed,float seconds,float radius,float observedTurnRate)
    {
        if(!std::isfinite(speed)||!std::isfinite(seconds)||!std::isfinite(radius)||!std::isfinite(observedTurnRate)
            ||speed<0||seconds<0||radius<=0) return 0;
        const float uncertainty=speed*seconds*seconds*(.15f+std::abs(observedTurnRate)*.5f);
        return 100.f*radius/(radius+uncertainty);
    }
    inline int BestHeadCell(const std::array<bool,49>& safe)
    {
        int best=-1,bestMargin=-1,bestOffset=100;
        for(int i=0;i<49;++i)
        {
            if(!safe[i]) continue;
            const int x=i%7,y=i/7;
            int margin=(std::min)({x,6-x,y,6-y});
            for(int other=0;other<49;++other) if(!safe[other])
                margin=(std::min)(margin,(std::max)(std::abs(x-other%7),std::abs(y-other/7))-1);
            const int offset=(x-3)*(x-3)+(y-3)*(y-3);
            if(margin>bestMargin||(margin==bestMargin&&offset<bestOffset)) {best=i;bestMargin=margin;bestOffset=offset;}
        }
        return best;
    }
    // 1 accepts a candidate, 0 rejects its geometry, -1 defers its timing.
    // Rank the cheap mask first; only a few cells run the native muzzle solver.
    template<class Solve>
    int RefineHeadCell(std::array<bool,49> safe,Solve&& solve)
    {
        for(int attempt=0;attempt<4;++attempt)
        {
            const int best=BestHeadCell(safe);
            if(best<0) return -1;
            const int result=solve(best);
            if(result>0) return best;
            if(result<0) return -1;
            safe[best]=false;
        }
        return -1;
    }
    struct FeetYaw
    {
        float goal=0,current=0;
        void Step(float eye,float dt,bool moving)
        {
            if(!std::isfinite(eye)||!std::isfinite(dt)||dt<=0||dt>.1f) return;
            if(!std::isfinite(goal)||!std::isfinite(current)) goal=current=eye;
            eye=std::remainder(eye,360.f);
            if(moving) goal=eye;
            else
            {
                const float error=std::remainder(goal-eye,360.f);
                if(std::abs(error)>45.f) goal-=std::copysign(45.f,error);
            }
            goal=std::remainder(goal,360.f);
            const float difference=std::remainder(goal-current,360.f);
            const float turn=720.f*dt*std::clamp(std::abs(difference)/60.f,.01f,1.f);
            current=std::remainder(current+std::clamp(difference,-turn,turn),360.f);
        }
    };
    struct PendingShot
    {
        int target=0,weapon=0,command=0;
        float deadline=0;
        bool head=false;
        int entity=0,spawn=0,hypothesis=-1;
        std::uint32_t revision=0;
    };
    struct ShotLedger
    {
        std::array<PendingShot,512> shots{};
        void Clear() {shots.fill({});}
        void Add(PendingShot value)
        {
            if(value.target<=0||value.command<0||!std::isfinite(value.deadline)||value.deadline<0) return;
            PendingShot* slot=nullptr;
            for(auto& shot:shots)
            {
                if(shot.target==value.target&&shot.command==value.command&&shot.weapon==value.weapon) return;
                if(!shot.target) {if(!slot||slot->target) slot=&shot;}
                else if(!slot||(slot->target&&shot.deadline<slot->deadline)) slot=&shot;
            }
            if(slot) *slot=value;
        }
        bool Hit(int target,int weapon,bool head,PendingShot* matched=nullptr)
        {
            PendingShot* oldest=nullptr;
            for(auto& shot:shots)
                if(shot.target==target&&shot.weapon==weapon&&(!shot.head||head)
                    &&(!oldest||shot.command<oldest->command)) oldest=&shot;
            if(!oldest) return false;
            if(matched) *matched=*oldest;
            *oldest={};return true;
        }
        template<class Callback>
        void ExpireDetailed(float now,int ack,Callback&& miss)
        {
            for(auto& shot:shots)
            {
                if(!shot.target) continue;
                if(now>shot.deadline+2.f) shot={}; // Stale session/ack: do not learn a miss.
                else if(shot.deadline<now&&shot.command<=ack) {const auto expired=shot;shot={};miss(expired);}
            }
        }
        template<class Callback>
        void Expire(float now,int ack,Callback&& miss)
        {
            ExpireDetailed(now,ack,[&](const PendingShot& shot){miss(shot.target);});
        }
    };

}
