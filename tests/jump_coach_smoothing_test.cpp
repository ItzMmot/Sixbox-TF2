// tests/jump_coach_smoothing_test.cpp — JumpCoach::SmoothedPath
#include "../Unibox/src/Features/Visuals/JumpCoach/CoachPolicy.h"
#include "../Unibox/src/Features/Visuals/JumpCoach/PathSmoothing.h"
#include <cassert>
#include <limits>

using JumpCoach::Vector;
using Path=JumpCoach::SmoothedPath<Vector>;
static bool Near(float a,float b) {return std::abs(a-b)<.001f;}

int main()
{
    const std::array<Vector,2> initial{{{0,0,0},{100,0,0}}};
    const std::array<Vector,2> turn{{{0,0,0},{100,100,0}}};
    Path slow,fast;
    slow.Update(initial,2,{},0,1,true);fast=slow;
    for(int i=0;i<30;++i) slow.Update(turn,2,{},1.f/30,1,false);
    for(int i=0;i<144;++i) fast.Update(turn,2,{},1.f/144,1,false);
    assert(Near(slow.points.back().y,fast.points.back().y));
    assert(Near(slow.points.back().y,100.f*(1.f-std::exp(-4.f))));

    Path stronger,weaker;
    stronger.Update(initial,2,{},0,1,true);weaker=stronger;
    stronger.Update(turn,2,{},.015f,1,false);
    weaker.Update(turn,2,{},.015f,.25f,false);
    assert(stronger.points.back().y>0 && stronger.points.back().y<weaker.points.back().y);
    stronger.Update(turn,2,{},.015f,0,false);
    assert(Near(stronger.points.back().y,100));

    Path moving;
    moving.Update(initial,2,{},0,1,true);
    const std::array<Vector,2> translated{{{10,20,0},{110,20,0}}};
    moving.Update(translated,2,{10,20,0},.015f,1,false);
    assert(Near(moving.points.front().x,10) && Near(moving.points.back().y,20));
    assert(Near(moving.points.back().x,110));

    const std::array<Vector,3> resized{{{10,20,0},{60,20,0},{110,40,0}}};
    moving.Update(resized,3,{10,20,0},.015f,1,false);
    assert(moving.count==Path::Samples && moving.points.back().y>20 && moving.points.back().y<40);
    moving.Update(resized,3,{10,20,0},0,1,true);
    assert(Near(moving.points.back().y,40));
    moving.Update(turn,2,{1000,0,0},.015f,1,false);
    assert(Near(moving.points.back().y,100)); // Teleport discards stale history.
    moving.Update(initial,0,{},.015f,1,false);
    assert(moving.count==0);
    moving.Update(turn,2,{},0,1,false);
    assert(Near(moving.points.back().y,100));
    moving.Update(initial,2,{},.015f,std::numeric_limits<float>::quiet_NaN(),false);
    assert(Near(moving.points.back().y,0));
}
