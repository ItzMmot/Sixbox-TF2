#pragma once
#include "SeamGeometry.h"

namespace SplashWindow
{
    using namespace SeamGeometry;
    constexpr float Skin=1.f/32.f;
    struct Window {double low=0,high=0;bool valid=false;};
    struct D
    {
        double x=0,y=0,z=0;
        explicit D(V a):x(a.x),y(a.y),z(a.z) {}
        D(double a,double b,double c):x(a),y(b),z(c) {}
        D operator+(D b) const {return {x+b.x,y+b.y,z+b.z};}
        D operator-(D b) const {return {x-b.x,y-b.y,z-b.z};}
        D operator*(double b) const {return {x*b,y*b,z*b};}
        double Dot(D b) const {return x*b.x+y*b.y+z*b.z;}
    };
    inline void Include(Window& result,double low,double high,double limit)
    {
        low=(std::max)(low,0.0);high=(std::min)(high,limit);
        if(!std::isfinite(low)||!std::isfinite(high)||high<low) return;
        if(!result.valid) result={low,high,true};
        else {result.low=(std::min)(result.low,low);result.high=(std::max)(result.high,high);}
    }
    inline Window Capsule(const Edge& edge,V start,V direction,float radius,float limit)
    {
        Window result;
        if(!Finite(edge.first)||!Finite(edge.last)||!Finite(start)||!Finite(direction)
            ||!std::isfinite(radius)||radius<=0||!std::isfinite(limit)||limit<=0) return result;
        const D origin(start),ray(direction),a(edge.first),b(edge.last);
        const D delta=b-a;const double length=std::sqrt(delta.Dot(delta)),ray2=ray.Dot(ray);
        if(ray2<1e-16) return result;
        auto sphere=[&](D centre) {
            const D offset=origin-centre;const double middle=-offset.Dot(ray)/ray2;
            const D nearest=offset+ray*middle;
            const double spread=(double(radius)*radius-nearest.Dot(nearest))/ray2;
            if(spread>=0) {const double width=std::sqrt(spread);Include(result,middle-width,middle+width,limit);}
        };
        sphere(a);if(length<1e-8) return result;sphere(b);
        const D axis=delta*(1.0/length),offset=origin-a;
        const double along=offset.Dot(axis),rate=ray.Dot(axis);
        const D across=offset-axis*along,drift=ray-axis*rate;
        const double drift2=drift.Dot(drift);
        double low=0,high=limit;
        if(std::abs(rate)<1e-14) {if(along<0||along>length) return result;}
        else {double x=-along/rate,y=(length-along)/rate;if(x>y) std::swap(x,y);low=(std::max)(low,x);high=(std::min)(high,y);}
        if(drift2<1e-16) {if(across.Dot(across)<=double(radius)*radius) Include(result,low,high,limit);return result;}
        const double middle=-across.Dot(drift)/drift2;const D nearest=across+drift*middle;
        const double spread=(double(radius)*radius-nearest.Dot(nearest))/drift2;
        if(spread>=0) {const double width=std::sqrt(spread);Include(result,(std::max)(low,middle-width),(std::min)(high,middle+width),limit);}
        return result;
    }
    // Exact ray interval for segment + axis-aligned projectile hull (Minkowski
    // sum). Clip a bounded polygon in (ray distance, segment fraction), so no
    // assumptions about ray/edge orientation or nearly parallel planes are needed.
    inline Window Hull(const Edge& edge,V start,V direction,V hull,float limit,float skin=Skin)
    {
        Window result;
        if(!Finite(edge.first)||!Finite(edge.last)||!Finite(start)||!Finite(direction)||!Finite(hull)
            ||hull.x<0||hull.y<0||hull.z<0||!std::isfinite(limit)||limit<=0||!std::isfinite(skin)||skin<0) return result;
        if(D(direction).Dot(D(direction))<1e-16) return result;
        struct P {double t,s;};std::array<P,16> polygon{},scratch{};
        polygon[0]={0,0};polygon[1]={limit,0};polygon[2]={limit,1};polygon[3]={0,1};int count=4;
        auto clip=[&](double t,double s,double c) {
            int written=0;
            for(int i=0;i<count;++i)
            {
                const P a=polygon[i],b=polygon[(i+1)%count];
                const double da=t*a.t+s*a.s-c,db=t*b.t+s*b.s-c;
                if(da<=0&&written<int(scratch.size())) scratch[written++]=a;
                if((da<0&&db>0)||(da>0&&db<0))
                {
                    const double fraction=da/(da-db);
                    if(written<int(scratch.size())) scratch[written++]={a.t+(b.t-a.t)*fraction,a.s+(b.s-a.s)*fraction};
                }
            }
            count=written;for(int i=0;i<count;++i) polygon[i]=scratch[i];
        };
        for(int axis=0;axis<3&&count;++axis)
        {
            const double o=double(start.At(axis))-edge.first.At(axis),e=double(edge.last.At(axis))-edge.first.At(axis);
            const double extent=double(hull.At(axis))+skin;
            clip(direction.At(axis),-e,extent-o);
            if(count) clip(-direction.At(axis),e,extent+o);
        }
        if(!count) return result;
        double low=limit,high=0;
        for(int i=0;i<count;++i) {low=(std::min)(low,polygon[i].t);high=(std::max)(high,polygon[i].t);}
        Include(result,low,high,limit);return result;
    }
    inline V Arc(V first,V last,float fraction)
    {
        V result=Add(Mul(first,1.f-fraction),Mul(last,fraction));
        const float length=std::sqrt(Dot(result,result));
        return length>1e-6f?Mul(result,1.f/length):first;
    }
    inline V ClosestSegment(V a,V b,V point)
    {
        const V delta=Sub(b,a);const float squared=Dot(delta,delta);
        return squared>1e-8f?Add(a,Mul(delta,std::clamp(Dot(Sub(point,a),delta)/squared,0.f,1.f))):a;
    }
    inline bool Boundary(const Face& a,const Face& b,V anchor,float reach,Edge& edge)
    {
        if(!Valid(a)||!Valid(b)||!Finite(anchor)||!std::isfinite(reach)||reach<=0) return false;
        struct Choice {int index=-1;float distance=std::numeric_limits<float>::max();};
        auto nearest=[&](const Face& face) {
            std::array<Choice,2> choices{};
            for(int i=0;i<face.count;++i)
            {
                const V point=ClosestSegment(face.vertices[i],face.vertices[(i+1)%face.count],anchor),delta=Sub(point,anchor);
                const float distance=Dot(delta,delta);
                if(distance<choices[0].distance) {choices[1]=choices[0];choices[0]={i,distance};}
                else if(distance<choices[1].distance) choices[1]={i,distance};
            }
            return choices;
        };
        const auto first=nearest(a),second=nearest(b);float best=reach*reach,bestAnchor=std::numeric_limits<float>::max();bool found=false;
        for(const auto x:first) for(const auto y:second)
        {
            if(x.index<0||y.index<0) continue;
            const V p=a.vertices[x.index],q=a.vertices[(x.index+1)%a.count];
            const V r=b.vertices[y.index],s=b.vertices[(y.index+1)%b.count];
            V pa=ClosestSegment(p,q,anchor),pb=ClosestSegment(r,s,pa);
            for(int pass=0;pass<3;++pass) {pa=ClosestSegment(p,q,pb);pb=ClosestSegment(r,s,pa);}
            const V delta=Sub(pa,pb);const float squared=Dot(delta,delta);
            const V anchorPoint=ClosestSegment(p,q,anchor),from=Sub(anchorPoint,anchor);const float anchorDistance=Dot(from,from);
            if(squared>reach*reach||squared>best+1e-6f||(std::abs(squared-best)<=1e-6f&&anchorDistance>=bestAnchor)) continue;
            // Preserve the real boundary; averaging across a gap would invent
            // geometry. The opposing face merely seeds the outward normal arc.
            best=squared;bestAnchor=anchorDistance;edge={p,q,a.normal,b.normal};found=true;
        }
        return found;
    }
    inline bool BlocksPlayer(bool owner,bool selectedTarget,bool sameTeam,float age)
    {
        return !owner&&!selectedTarget&&(!sameTeam||age>=.25f);
    }
    inline bool TickShift(const Window& window,float step,float alongRate,int tick,float& shift)
    {
        if(!window.valid||!std::isfinite(step)||step<=0||!std::isfinite(alongRate)||std::abs(alongRate)<1e-6f||tick<=0) return false;
        const double value=(double(step)*tick-(window.low+window.high)*.5)/alongRate;
        if(!std::isfinite(value)||std::abs(value)>1e6) return false;
        shift=float(value);return true;
    }
}
