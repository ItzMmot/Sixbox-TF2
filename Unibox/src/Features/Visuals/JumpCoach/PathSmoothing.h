#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace JumpCoach
{
    // Presentation only: bounded storage, exponential response in seconds.
    // Normalized resampling keeps history compatible when flight duration changes.
    template<class Point>
    struct SmoothedPath
    {
        static constexpr int Samples=129;
        std::array<Point,Samples> points{};
        int count=0;
        Point anchor{};

        void Clear() {count=0;}

        template<std::size_t N>
        void Update(const std::array<Point,N>& target,int size,Point origin,float seconds,float strength,bool reset,bool fixedEnd=false)
        {
            size=std::clamp(size,0,int(N));
            if(size<2) {Clear();return;}
            const Point delta=origin-anchor;
            const float distanceSquared=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
            reset=reset || count!=Samples || !std::isfinite(distanceSquared) || distanceSquared>128.f*128.f;
            strength=std::isfinite(strength)?std::clamp(strength,0.f,1.f):0.f;
            const float alpha=reset || strength==0.f?1.f:
                1.f-std::exp(-(std::max)(0.f,seconds)/(.25f*strength));
            for(int i=0;i<Samples;++i)
            {
                const float index=float(i)*float(size-1)/float(Samples-1);
                const int first=(std::min)(int(index),size-1),next=(std::min)(first+1,size-1);
                const Point desired=target[first]+(target[next]-target[first])*(index-float(first));
                const Point previous=points[i]+delta*(fixedEnd?1.f-float(i)/float(Samples-1):1.f);
                points[i]=alpha==1.f?desired:previous+(desired-previous)*alpha;
            }
            // Never leave the beginning trailing behind the player's current hull.
            points[0]=target[0];anchor=origin;count=Samples;
        }
    };
    template<class Point>
    struct SmoothedLanding
    {
        Point point{},normal{};
        bool valid=false;
        void Clear() {valid=false;}
        void Update(Point target,Point surface,float seconds,float strength,bool reset)
        {
            const float length=std::sqrt(surface.x*surface.x+surface.y*surface.y+surface.z*surface.z);
            if(!std::isfinite(length)||length<.5f) {Clear();return;}
            surface=surface*(1.f/length);
            const float alignment=normal.x*surface.x+normal.y*surface.y+normal.z*surface.z;
            const Point difference=target-point;
            const float plane=difference.x*surface.x+difference.y*surface.y+difference.z*surface.z;
            reset=reset||!valid||alignment<.7f||std::abs(plane)>32.f;
            strength=std::isfinite(strength)?std::clamp(strength,0.f,1.f):0.f;
            const float alpha=reset||strength==0?1.f:1.f-std::exp(-(std::max)(seconds,0.f)/(.25f*strength));
            point=point+(target-point)*alpha;
            // Remain on the current contact plane rather than smoothing below a floor.
            const Point offset=target-point;
            point=point+surface*(offset.x*surface.x+offset.y*surface.y+offset.z*surface.z);
            normal=surface;valid=true;
        }
    };

}
