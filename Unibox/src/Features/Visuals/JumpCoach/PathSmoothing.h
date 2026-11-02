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
        void Update(const std::array<Point,N>& target,int size,Point origin,float seconds,float strength,bool reset)
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
                const Point previous=points[i]+delta;
                points[i]=alpha==1.f?desired:previous+(desired-previous)*alpha;
            }
            // Never leave the beginning trailing behind the player's current hull.
            points[0]=target[0];anchor=origin;count=Samples;
        }
    };
}
