// tests/seam_geometry_test.cpp - finite collision-face intersections.
#include "../SixBox/src/Features/Aimbot/Shared/SeamGeometry.h"
#include <cassert>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void* operator new(std::size_t size) {++allocations;if(void* p=std::malloc(size?size:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace SeamGeometry;
static Face Floor(float z)
{
    Face face;face.normal={0,0,1};face.count=4;
    face.vertices[0]={-10,-20,z};face.vertices[1]={10,-20,z};
    face.vertices[2]={10,20,z};face.vertices[3]={-10,20,z};return face;
}
int main()
{
    const unsigned before=allocations;
    const Face floor=Floor(0);
    auto nearest=Nearest(floor,V{0,0,10});assert(nearest.x==0&&nearest.y==0&&nearest.z==0);
    nearest=Nearest(floor,V{30,30,10});assert(nearest.x==10&&nearest.y==20&&nearest.z==0);
    Face wall;wall.count=4;wall.normal={1,0,0};
    wall.vertices[0]={0,-5,-10};wall.vertices[1]={0,5,-10};
    wall.vertices[2]={0,5,10};wall.vertices[3]={0,-5,10};
    Edge edge;assert(Intersect(floor,wall,edge));
    assert(std::abs(edge.first.y-edge.last.y)==10);
    V point=Nearest(edge,{4,10,3});assert(std::abs(point.y-5)<.001f&&point.x==0&&point.z==0);
    // Displacement triangles and reversed winding use the same finite clipping.
    Face triangle=floor;triangle.count=3;
    triangle.vertices[0]={-10,-10,0};triangle.vertices[1]={10,-10,0};triangle.vertices[2]={0,10,0};
    assert(Intersect(triangle,wall,edge));
    std::swap(wall.vertices[0],wall.vertices[3]);std::swap(wall.vertices[1],wall.vertices[2]);
    assert(Intersect(triangle,wall,edge));
    assert(!Intersect(floor,Floor(1),edge));
    for(int i=0;i<wall.count;++i) wall.vertices[i].x=30;
    assert(!Intersect(floor,wall,edge)); // Infinite planes cross; finite faces do not.
    wall.vertices[0].x=std::numeric_limits<float>::quiet_NaN();assert(!Intersect(floor,wall,edge));
    wall.count=MaxVertices+1;assert(!Intersect(floor,wall,edge));
    Face degenerate;degenerate.count=3;degenerate.normal={0,0,1};
    assert(!Valid(degenerate));
    // Oblique brush/displacement boundary: x=z cuts the finite floor at x=0.
    wall={};wall.count=4;wall.normal={.70710678f,0,-.70710678f};
    wall.vertices[0]={-10,-5,-10};wall.vertices[1]={-10,5,-10};
    wall.vertices[2]={10,5,10};wall.vertices[3]={10,-5,10};
    assert(Intersect(floor,wall,edge));assert(std::abs(edge.first.x)<.001f);
    assert(std::abs(edge.last.z)<.001f&&std::abs(edge.first.y-edge.last.y)==10);
    assert(allocations==before);
}
