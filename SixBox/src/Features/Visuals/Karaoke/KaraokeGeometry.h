#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <ThirdParty/Earcut/earcut.hpp>

namespace Karaoke
{
    struct Point3
    {
        float x=0,y=0,z=0;
        Point3 operator+(Point3 b)const{return {x+b.x,y+b.y,z+b.z};}
        Point3 operator-(Point3 b)const{return {x-b.x,y-b.y,z-b.z};}
        Point3 operator*(float s)const{return {x*s,y*s,z*s};}
    };
    inline float Dot(Point3 a,Point3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
    inline Point3 Cross(Point3 a,Point3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    inline Point3 Unit(Point3 p){return p*(1.f/std::sqrt((std::max)(1e-12f,Dot(p,p))));}
    inline Point3 Rotate(Point3 p,Point3 angles)
    {
        const float sx=std::sin(angles.x),cx=std::cos(angles.x),sy=std::sin(angles.y),cy=std::cos(angles.y),sz=std::sin(angles.z),cz=std::cos(angles.z);
        p={p.x,p.y*cx-p.z*sx,p.y*sx+p.z*cx};p={p.x*cy+p.z*sy,p.y,-p.x*sy+p.z*cy};
        return {p.x*cz-p.y*sz,p.x*sz+p.y*cz,p.z};
    }
    struct Rotation
    {
        Point3 x,y,z;
        explicit Rotation(Point3 a)
        {
            const float sx=std::sin(a.x),cx=std::cos(a.x),sy=std::sin(a.y),cy=std::cos(a.y),sz=std::sin(a.z),cz=std::cos(a.z);
            x={cz*cy,sz*cy,-sy};y={cz*sy*sx-sz*cx,sz*sy*sx+cz*cx,cy*sx};z={cz*sy*cx+sz*sx,sz*sy*cx-cz*sx,cy*cx};
        }
        Point3 Apply(Point3 p)const{return x*p.x+y*p.y+z*p.z;}
    };
    inline Point3 Cylinder(Point3 p,float radius)
    {
        radius=(std::max)(1.f,radius);const float angle=p.x/radius;
        return {(radius+p.z)*std::sin(angle),p.y,(radius+p.z)*std::cos(angle)-radius};
    }
    using OutlinePoint=std::array<float,2>;
    using Outline=std::vector<OutlinePoint>;
    struct MeshFace {std::array<Point3,3> points;Point3 normal;};
    struct GlyphMesh {std::vector<MeshFace> faces;};
    inline double SignedArea(const Outline& ring)
    {
        double area=0;for(size_t i=0,j=ring.size()-1;i<ring.size();j=i++)area+=double(ring[j][0])*ring[i][1]-double(ring[i][0])*ring[j][1];return area*.5;
    }
    inline bool Inside(OutlinePoint p,const Outline& ring)
    {
        bool inside=false;
        for(size_t i=0,j=ring.size()-1;i<ring.size();j=i++)
        {
            const auto a=ring[i],b=ring[j];
            if((a[1]>p[1])!=(b[1]>p[1])&&p[0]<(b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0])inside=!inside;
        }
        return inside;
    }
    // Closed, triangulated front/back caps and contour side walls, including holes.
    // Allocation happens only while caching a glyph, never during Draw().
    inline GlyphMesh Extrude(std::vector<Outline> rings)
    {
        GlyphMesh mesh;std::erase_if(rings,[](auto& r){return r.size()<3||std::abs(SignedArea(r))<1e-7;});
        std::vector<int> parents(rings.size(),-1),depth(rings.size());
        for(size_t i=0;i<rings.size();++i)
        {
            double parentArea=1e100;
            for(size_t j=0;j<rings.size();++j)
                if(i!=j&&std::abs(SignedArea(rings[j]))>std::abs(SignedArea(rings[i]))&&Inside(rings[i][0],rings[j])&&std::abs(SignedArea(rings[j]))<parentArea)
                    {parents[i]=int(j);parentArea=std::abs(SignedArea(rings[j]));}
        }
        for(size_t i=0;i<rings.size();++i)for(int p=parents[i];p>=0;p=parents[p])++depth[i];
        auto triangle=[&](Point3 a,Point3 b,Point3 c)
        {if(Dot(Cross(b-a,c-a),Cross(b-a,c-a))>1e-14f)mesh.faces.push_back({{a,b,c},Unit(Cross(b-a,c-a))});};
        for(size_t i=0;i<rings.size();++i)
        {
            // Outer contours counterclockwise; hole contours clockwise.
            if((SignedArea(rings[i])>0)!=(depth[i]%2==0))std::reverse(rings[i].begin(),rings[i].end());
            const auto& ring=rings[i];
            for(size_t e=0;e<ring.size();++e)
            {
                const auto a=ring[e],b=ring[(e+1)%ring.size()];
                triangle({a[0],a[1],0},{b[0],b[1],0},{b[0],b[1],1});
                triangle({a[0],a[1],0},{b[0],b[1],1},{a[0],a[1],1});
            }
        }
        for(size_t i=0;i<rings.size();++i)if(depth[i]%2==0)
        {
            std::vector<Outline> polygon{rings[i]};
            for(size_t j=0;j<rings.size();++j)if(parents[j]==int(i)&&depth[j]%2)polygon.push_back(rings[j]);
            std::vector<OutlinePoint> points;for(const auto& ring:polygon)points.insert(points.end(),ring.begin(),ring.end());
            const auto indices=mapbox::earcut<uint32_t>(polygon);
            for(size_t t=0;t+2<indices.size();t+=3)
            {
                auto a=points[indices[t]],b=points[indices[t+1]],c=points[indices[t+2]];
                if((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])<0)std::swap(b,c);
                triangle({c[0],c[1],0},{b[0],b[1],0},{a[0],a[1],0});
                triangle({a[0],a[1],1},{b[0],b[1],1},{c[0],c[1],1});
            }
        }
        return mesh;
    }
    inline uint32_t Mix(uint32_t n){n^=n>>16;n*=0x7feb352d;n^=n>>15;n*=0x846ca68b;return n^(n>>16);}
    inline float Random(uint32_t seed){return (Mix(seed)&0xffffff)/16777215.f;}
    struct CrumbleStyle {float stagger=.025f,duration=1.6f,distance=120,spread=70,spin=300,depth=8,gravity=1;int order=0;};
    struct LetterMotion {Point3 displacement,rotation;float alpha=1;};
    inline LetterMotion Crumble(double elapsed,unsigned cluster,unsigned count,uint32_t seed,const CrumbleStyle& style)
    {
        LetterMotion result;if(!count)return result;
        const float rank=style.order==2?Random(seed^Mix(cluster)):float(style.order==1?count-1-(std::min)(cluster,count-1):cluster)/(std::max)(1u,count-1);
        const float window=(std::min)(3.f,(std::max)(0.f,style.stagger)*(count?count-1:0));
        const float t=float(elapsed)-rank*window;if(t<=0)return result;
        const float u=std::clamp(t/(std::max)(.1f,style.duration),0.f,1.f),r=Random(seed^Mix(cluster+371));
        result.displacement={(r-.5f)*style.spread*u,style.distance*style.gravity*u*u,(Random(seed^Mix(cluster+891))-.5f)*style.depth*u};
        const float spin=style.spin*.01745329252f*u;
        result.rotation={(Random(seed^Mix(cluster+29))-.5f)*spin,(Random(seed^Mix(cluster+53))-.5f)*spin,(r-.5f)*spin};
        const float fade=std::clamp((u-.55f)/.45f,0.f,1.f);result.alpha=1-fade*fade*(3-2*fade);return result;
    }
    struct WorldLine {int index=-1;Point3 origin,right,normal;float ground=-100000;bool visible=false;float radius=160,arcLimit=85;};
    struct StayAnchor
    {
        Point3 origin;float yaw=0,offset=0;double born=0,updated=0;uint64_t seen=0;int line=-1;
        bool Update(int index,uint64_t frame,double now,Point3 target,float targetYaw,float lineOffset)
        {
            if(line!=index||seen+1!=frame)
            {line=index;seen=frame;born=updated=now;origin=target;yaw=targetYaw;offset=lineOffset;return true;}
            seen=frame;
            const double until=(std::min)(now,born+.75),dt=until-updated;
            if(dt<=0)return false;
            const float alpha=1-std::exp(-float((std::min)(dt,.1))*8.f);
            origin=origin+(target-origin)*alpha;yaw+=std::remainder(targetYaw-yaw,360.f)*alpha;updated=until;return true;
        }
    };
    inline bool Combining(uint32_t c)
    {return (c>=0x300&&c<=0x36f)||(c>=0x1ab0&&c<=0x1aff)||(c>=0x1dc0&&c<=0x1dff)||(c>=0x20d0&&c<=0x20ff)||(c>=0xfe00&&c<=0xfe0f)||(c>=0xfe20&&c<=0xfe2f)||(c>=0x1f3fb&&c<=0x1f3ff)||c==0x200d;}
    inline uint32_t Decode(const char*& p,const char* end)
    {
        if(p>=end)return 0;
        const auto first=static_cast<unsigned char>(*p++);if(first<0x80)return first;
        const int extra=first>=0xf0?3:first>=0xe0?2:first>=0xc0?1:0;uint32_t value=first&((1u<<(6-extra))-1);
        if(!extra)return 0xfffd;
        for(int i=0;i<extra;++i){if(p>=end||(static_cast<unsigned char>(*p)&0xc0)!=0x80)return 0xfffd;value=(value<<6)|(static_cast<unsigned char>(*p++)&0x3f);}
        return value>0x10ffff||(value>=0xd800&&value<=0xdfff)||(extra==1&&value<0x80)||(extra==2&&value<0x800)||(extra==3&&value<0x10000)?0xfffd:value;
    }
}
