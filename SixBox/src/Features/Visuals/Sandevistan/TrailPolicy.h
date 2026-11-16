#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace Sandevistan
{
    constexpr int MaxEvents=8,MaxGhosts=96,MaxCommands=256,MaxCopies=24;
    enum Kind {None=0,Warp=1,Doubletap=2};
    // A burst is visible only after two distinct consecutive commands.
    // Re-prediction of one command must not confirm it a second time.
    struct Burst
    {
        int first=0,last=0,total=0,marked=0,kind=0;
        double born=0,tagged=0;
        bool confirmed=false;
        bool Split(int number,int nextKind,double now) const
        {return kind!=nextKind||number>last+1||now-tagged>.25;}
        void Start(int number,int nextKind)
        {*this={};first=number;last=number-1;kind=nextKind;}
        bool Append(int number,int remaining,double now)
        {
            if(number<=last)return false;
            last=number;total=std::clamp(number-first+remaining+1,1,64);
            tagged=now;++marked;
            if(!confirmed&&marked>=2){confirmed=true;born=now;}
            return true;
        }
    };
    struct CameraMotion
    {
        double time=0;
        float yaw=0,pitch=0,x=0,y=0;
        bool valid=false;
        void Update(double now,float nextYaw,float nextPitch)
        {
            const double dt=now-time;
            if(!valid||dt<=0||dt>.25)
            {valid=true;time=now;yaw=nextYaw;pitch=nextPitch;x=y=0;return;}
            const float dy=std::remainder(nextYaw-yaw,360.f),dp=nextPitch-pitch;
            const float weight=1.f-std::exp(-float(dt)/.035f);
            // Teleports/snaps do not throw the weapon across the screen.
            const bool snap=std::abs(dy)>70||std::abs(dp)>70;
            const float tx=snap?0.f:std::clamp(dy/float(dt)*.01f,-4.f,4.f);
            const float ty=snap?0.f:std::clamp(dp/float(dt)*.01f,-4.f,4.f);
            x+=(tx-x)*weight;y+=(ty-y)*weight;
            time=now;yaw=nextYaw;pitch=nextPitch;
        }
    };
    inline float BehindOffset(float rank,float extension)
    {
        if(!std::isfinite(rank)||!std::isfinite(extension)) return 0;
        return -(1.f-std::clamp(rank,0.f,1.f))*std::max(0.f,extension);
    }
    inline float TrailExtension(int copies,float travel,float width,float spacing,float spread)
    {
        if(copies<=1)return 0;
        // Spread is extra separation beyond a fraction of the body's width.
        // With both controls at zero the real trajectory remains untouched.
        const float step=std::max(std::max(0.f,spacing),spread>0?width*.25f+spread:0.f);
        return std::max(0.f,(copies-1)*step-std::max(0.f,travel));
    }
    inline bool CameraIntersects(float x,float y,float z,float minX,float minY,float minZ,
                                 float maxX,float maxY,float maxZ,float padding)
    {
        const float dx=std::max({minX-x,0.f,x-maxX});
        const float dy=std::max({minY-y,0.f,y-maxY});
        const float dz=std::max({minZ-z,0.f,z-maxZ});
        return dx*dx+dy*dy+dz*dz<=padding*padding;
    }
    // Freeze an engine-prepared palette at a historical model transform.
    // Matrix is a 3x4 rigid transform; no SDK pointers are retained.
    template<class Matrix> void RebaseBones(Matrix* dest,const Matrix* bones,int count,
                                           const Matrix& from,const Matrix& to)
    {
        float delta[3][3]{};
        for(int r=0;r<3;++r)for(int c=0;c<3;++c)
            for(int k=0;k<3;++k)delta[r][c]+=to[r][k]*from[c][k];
        for(int bone=0;bone<count;++bone)for(int r=0;r<3;++r)
        {
            for(int c=0;c<3;++c)
            {
                dest[bone][r][c]=0;
                for(int k=0;k<3;++k)dest[bone][r][c]+=delta[r][k]*bones[bone][k][c];
            }
            dest[bone][r][3]=to[r][3];
            for(int k=0;k<3;++k)dest[bone][r][3]+=delta[r][k]*(bones[bone][k][3]-from[k][3]);
        }
    }
    inline int Sample(int index,int total,int copies)
    {
        if(total<1||index<0||index>=total||copies<1)return -1;
        copies=std::min(copies,total);
        if(copies==1)return index==total-1?0:-1;
        const int slot=int(std::lround(double(index)*(copies-1)/(total-1)));
        return int(std::lround(double(slot)*(total-1)/(copies-1)))==index?slot:-1;
    }
    inline int InputSequence(int original,int wire) {return original>0?original:wire;}
    inline float CapturedRank(int index,int count)
    {return count>1?float(std::clamp(index,0,count-1))/float(count-1):0.f;}
    inline float Rank(int slot,int copies,float bias,bool reverse)
    {
        float rank=copies>1?float(slot)/float(copies-1):0.f;
        rank=std::pow(std::clamp(rank,0.f,1.f),std::clamp(bias,.25f,4.f));
        return reverse?1.f-rank:rank;
    }
    inline float Fade(double age,float duration,float fadeIn,float power,float rank,float stagger)
    {
        const float life=std::max(.05f,duration*(1.f-std::clamp(stagger,0.f,.8f)*(1.f-rank)));
        if(!std::isfinite(age)||age<0||age>=life)return 0;
        const float enter=fadeIn>0?std::clamp(float(age)/fadeIn,0.f,1.f):1.f;
        return enter*std::pow(std::clamp(1.f-float(age)/life,0.f,1.f),std::clamp(power,.25f,5.f));
    }
    template<class Color> Color Gradient(Color start,Color end,float rank)
    {
        rank=std::clamp(rank,0.f,1.f);
        auto lerp=[&](auto a,auto b){return static_cast<unsigned char>(std::clamp(std::lround(a+(b-a)*rank),0l,255l));};
        return Color(lerp(start.r,end.r),lerp(start.g,end.g),lerp(start.b,end.b),lerp(start.a,end.a));
    }
}
