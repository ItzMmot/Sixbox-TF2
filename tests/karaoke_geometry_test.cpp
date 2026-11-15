// tests/karaoke_geometry_test.cpp — real closed extrusion, holes, transforms, Unicode and retirement.
#include "../SixBox/src/Features/Visuals/Karaoke/KaraokeGeometry.h"
#include <cassert>
#include <string>
int main()
{
    using namespace Karaoke;
    const Outline outer={{0,0},{10,0},{10,10},{0,10}},hole={{3,3},{7,3},{7,7},{3,7}};
    const auto mesh=Extrude({outer,hole});assert(!mesh.faces.empty());
    double front=0,back=0,volume=0;int sides=0;
    for(const auto& f:mesh.faces)
    {
        const auto a=f.points[0],b=f.points[1],c=f.points[2];
        const float area=std::sqrt(Dot(Cross(b-a,c-a),Cross(b-a,c-a)))*.5f;
        if(f.normal.z<-.99f)front+=area;else if(f.normal.z>.99f)back+=area;else ++sides;
        volume+=Dot(a,Cross(b,c))/6.;
        assert(std::abs(Dot(f.normal,f.normal)-1)<1e-5f);
    }
    assert(std::abs(front-84)<1e-4&&std::abs(back-84)<1e-4&&std::abs(volume-84)<1e-4&&sides==16);
    const auto island=Extrude({outer,hole,{{4,4},{6,4},{6,6},{4,6}}});volume=0;
    for(const auto& f:island.faces)volume+=Dot(f.points[0],Cross(f.points[1],f.points[2]))/6.;
    assert(std::abs(volume-88)<1e-4); // nested contours preserve filled islands
    const auto rotated=Rotate({0,0,3},{0,1.5707963268f,0});assert(std::abs(rotated.x-3)<1e-5&&std::abs(rotated.z)<1e-5);
    const Point3 angles{.4f,-.8f,.2f},point{1,2,3};const auto direct=Rotate(point,angles),cached=Rotation(angles).Apply(point);
    assert(std::abs(direct.x-cached.x)<1e-5&&std::abs(direct.y-cached.y)<1e-5&&std::abs(direct.z-cached.z)<1e-5);
    const auto curve=Cylinder({80,4,0},160);
    assert(std::abs(curve.x*curve.x+(curve.z+160)*(curve.z+160)-160*160)<.01f&&curve.y==4);
    const auto leftCurve=Cylinder({-80,4,0},160);assert(std::abs(curve.x+leftCurve.x)<1e-5&&curve.z==leftCurve.z);
    const auto tangent=Rotation({0,.5f,0}).Apply({1,0,0}),radial=Rotation({0,.5f,0}).Apply({0,0,1});
    assert(std::abs(Dot(tangent,radial))<1e-5&&std::abs(Dot(radial,radial)-1)<1e-5);
    StayAnchor anchor;assert(anchor.Update(3,1,10,{0,0,0},0,5));
    assert(anchor.Update(3,2,10.1,{10,0,0},90,5)&&anchor.origin.x>0&&anchor.origin.x<10);
    anchor.Update(3,3,10.8,{20,0,0},90,5);const auto frozen=anchor.origin;const float frozenYaw=anchor.yaw;
    assert(!anchor.Update(3,4,11.5,{100,100,0},180,5)&&anchor.origin.x==frozen.x&&anchor.origin.y==frozen.y&&anchor.yaw==frozenYaw);
    assert(anchor.Update(3,6,12,{50,0,0},0,7)&&anchor.origin.x==50&&anchor.offset==7); // reappearance spawns again
    assert(anchor.Update(4,7,13,{60,0,0},0,8)&&anchor.line==4&&anchor.origin.x==60);
    CrumbleStyle style;style.stagger=.1f;
    const auto before=Crumble(-1,0,4,17,style);assert(before.alpha==1&&before.displacement.y==0);
    const auto early=Crumble(.15,0,4,17,style),late=Crumble(.15,3,4,17,style);
    assert(early.displacement.y>0&&late.displacement.y==0);
    style.order=1;assert(Crumble(.15,0,4,17,style).displacement.y==0&&Crumble(.15,3,4,17,style).displacement.y>0);
    style.order=2;const auto a=Crumble(.9,1,4,23,style),b=Crumble(.9,1,4,23,style);
    assert(a.displacement.x==b.displacement.x&&a.rotation.y==b.rotation.y);
    assert(Crumble(50,2,4,17,style).alpha==0);assert(Crumble(1,0,0,17,style).alpha==1);
    const std::string text="AПриветé";const char* p=text.data();const char* end=p+text.size();int count=0;
    while(p<end){assert(Decode(p,end)!=0xfffd);++count;}assert(count==8);
    assert(Combining(0x301)&&Combining(0x200d)&&!Combining(0x41));
}
