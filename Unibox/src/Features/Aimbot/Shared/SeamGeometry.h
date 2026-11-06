#pragma once
#include "AimPolicy.h"

// Finite intersections of convex collision faces, in Source HU.
// All work buffers are caller-owned. Polygon winding may be either direction.
namespace SeamGeometry
{
    using V=AimPolicy::Vector;
    inline V Add(V a,V b) {return {a.x+b.x,a.y+b.y,a.z+b.z};}
    inline V Sub(V a,V b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
    inline V Mul(V a,float b) {return {a.x*b,a.y*b,a.z*b};}
    inline float Dot(V a,V b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
    inline V Cross(V a,V b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    inline bool Finite(V a) {return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
    constexpr int MaxVertices=64,MaxFaces=96;
    struct Face
    {
        std::array<V,MaxVertices> vertices{};
        V normal{};
        int count=0;
    };
    struct Edge {V first{},last{},normal{},wall{};};
    inline bool Valid(const Face& face)
    {
        if(face.count<3||face.count>MaxVertices||!Finite(face.normal)||Dot(face.normal,face.normal)<.5f) return false;
        V area{};
        for(int i=0;i<face.count;++i)
        {
            if(!Finite(face.vertices[i])) return false;
            if(i>1) area=Add(area,Cross(Sub(face.vertices[i-1],face.vertices[0]),Sub(face.vertices[i],face.vertices[0])));
            if(std::abs(Dot(face.normal,Sub(face.vertices[i],face.vertices[0])))>.1f) return false;
        }
        return std::abs(Dot(area,face.normal))>1e-5f;
    }
    inline V Nearest(const Face& face,V point)
    {
        if(!Valid(face)||!Finite(point)) return point;
        const float squared=Dot(face.normal,face.normal);
        const V projected=Sub(point,Mul(face.normal,Dot(face.normal,Sub(point,face.vertices[0]))/squared));
        V centre{};for(int i=0;i<face.count;++i) centre=Add(centre,face.vertices[i]);
        centre=Mul(centre,1.f/face.count);bool inside=true;
        V best=face.vertices[0];float distance=std::numeric_limits<float>::max();
        for(int i=0;i<face.count;++i)
        {
            const V a=face.vertices[i],direction=Sub(face.vertices[(i+1)%face.count],a);
            V inward=Cross(face.normal,direction);
            if(Dot(inward,Sub(centre,a))<0) inward=Mul(inward,-1);
            if(Dot(inward,Sub(projected,a))<-.001f) inside=false;
            const float length=Dot(direction,direction);
            const V nearest=Add(a,Mul(direction,length>1e-8f?std::clamp(Dot(Sub(projected,a),direction)/length,0.f,1.f):0.f));
            const V delta=Sub(nearest,point);const float candidate=Dot(delta,delta);
            if(candidate<distance) {distance=candidate;best=nearest;}
        }
        return inside?projected:best;
    }
    inline bool Clip(const Face& face,V origin,V direction,float& low,float& high)
    {
        V centre{};
        for(int i=0;i<face.count;++i) centre=Add(centre,face.vertices[i]);
        centre=Mul(centre,1.f/float(face.count));
        for(int i=0;i<face.count;++i)
        {
            const V vertex=face.vertices[i];
            V inward=Cross(face.normal,Sub(face.vertices[(i+1)%face.count],vertex));
            if(Dot(inward,Sub(centre,vertex))<0) inward=Mul(inward,-1);
            const float length=std::sqrt(Dot(inward,inward));
            if(length<1e-6f) continue;
            inward=Mul(inward,1.f/length);
            const float at=Dot(inward,Sub(origin,vertex)),slope=Dot(inward,direction);
            if(std::abs(slope)<1e-7f) {if(at<-.01f) return false;continue;}
            const float boundary=-at/slope;
            if(slope>0) low=(std::max)(low,boundary);else high=(std::min)(high,boundary);
            if(low>high+.001f) return false;
        }
        return low<=high;
    }
    inline bool Intersect(const Face& a,const Face& b,Edge& edge)
    {
        if(!Valid(a)||!Valid(b)) return false;
        const V cross=Cross(a.normal,b.normal);
        const float squared=Dot(cross,cross);
        if(squared<1e-6f) return false;
        const V origin=Mul(Add(Mul(Cross(b.normal,cross),Dot(a.normal,a.vertices[0])),
            Mul(Cross(cross,a.normal),Dot(b.normal,b.vertices[0]))),1.f/squared);
        const V direction=Mul(cross,1.f/std::sqrt(squared));
        float low=-1e8f,high=1e8f;
        if(!Clip(a,origin,direction,low,high)||!Clip(b,origin,direction,low,high)||high-low<.01f) return false;
        edge={Add(origin,Mul(direction,low)),Add(origin,Mul(direction,high)),a.normal,b.normal};
        return Finite(edge.first)&&Finite(edge.last);
    }
    inline V Nearest(const Edge& edge,V point)
    {
        const V direction=Sub(edge.last,edge.first);
        const float squared=Dot(direction,direction);
        return squared>1e-8f?Add(edge.first,Mul(direction,std::clamp(Dot(Sub(point,edge.first),direction)/squared,0.f,1.f))):edge.first;
    }
}
