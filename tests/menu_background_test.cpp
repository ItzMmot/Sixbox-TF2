// tests/menu_background_test.cpp — MenuBackground layout, Gaussian blur and alpha.
#include "../SixBox/src/Features/ImGui/Background/BackgroundPolicy.h"
#include <cassert>
#include <limits>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void* operator new(std::size_t n) {++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace MenuBackground;
static bool InQuad(const Quad& q,V p)
{
    const V a{q.position[1].x-q.position[0].x,q.position[1].y-q.position[0].y};
    const V b{q.position[3].x-q.position[0].x,q.position[3].y-q.position[0].y};
    const V d{p.x-q.position[0].x,p.y-q.position[0].y};
    // Solve the actual float quad basis in double precision. Extremely long
    // rotated tiles are not exactly orthogonal after vertex quantization.
    const double determinant=double(a.x)*b.y-double(a.y)*b.x;
    const double u=(double(d.x)*b.y-double(d.y)*b.x)/determinant;
    const double v=(double(a.x)*d.y-double(a.y)*d.x)/determinant;
    return u>=-.001f&&u<=1.001f&&v>=-.001f&&v<=1.001f;
}
int main()
{
    {
        const unsigned before=allocations;
        constexpr float w=913,h=611;
        for(int mode=0;mode<4;++mode) for(int angle=-180;angle<=180;angle+=15)
            for(auto size:{std::array<int,2>{16,4096},{4096,16},{1024,768},{1,1}})
            {
                Placement p;p.fit=Fit(mode);p.rotation=float(angle);
                std::array<Quad,MaxTiles> quads{};int count=0;
                const int total=Layout(w,h,size[0],size[1],p,[&](const Quad& q){assert(count<MaxTiles);quads[count++]=q;});
                assert(total==count&&count>0&&count<=MaxTiles);
                for(int i=0;i<count;++i) for(const auto& point:quads[i].position) assert(std::isfinite(point.x)&&std::isfinite(point.y));
                if(mode==int(Fit::Cover)||mode==int(Fit::Stretch)||mode==int(Fit::Tile))
                    for(V corner:{V{0,0},{w,0},{w,h},{0,h},{w/2,h/2}})
                    {
                        bool inside=false;for(int i=0;i<count;++i) inside=inside||InQuad(quads[i],corner);
                        assert(inside);
                    }
                if(mode==int(Fit::Contain)) for(const auto& point:quads[0].position)
                    assert(point.x>=-.01f&&point.x<=w+.01f&&point.y>=-.01f&&point.y<=h+.01f);
            }
        Placement p;p.flipX=p.flipY=true;
        Layout(100,100,100,100,p,[](const Quad& q){assert(q.uv[0].x==1&&q.uv[0].y==1&&q.uv[2].x==0&&q.uv[2].y==0);});
        p.x=p.rotation=p.scale=std::numeric_limits<float>::quiet_NaN();
        Layout(100,100,100,100,p,[](const Quad& q){assert(std::isfinite(q.position[0].x));});
        assert(Layout(0,100,100,100,{},[](const Quad&){assert(false);})==0);
        assert(allocations==before); // Layout/render policy never allocates.
    }
    auto never=[] {return false;};
    Image source{9,9,std::vector<uint8_t>(9*9*4,0)};source.naturalWidth=90;source.naturalHeight=90;
    // Hidden blue pixels must not bleed into a red translucent Gaussian edge.
    for(size_t i=0;i<source.bgra.size();i+=4) source.bgra[i]=255;
    const size_t centre=(4*9+4)*4;source.bgra[centre]=0;source.bgra[centre+2]=255;source.bgra[centre+3]=255;
    const auto blurred=Process(source,{1,1,1,1},never);
    assert(ValidImage(blurred)&&blurred.naturalWidth==90&&blurred.naturalHeight==90);
    assert(blurred.bgra[centre+3]>blurred.bgra[centre+4+3]&&blurred.bgra[centre+4+3]>0);
    assert(blurred.bgra[centre+2]>250&&blurred.bgra[centre]==0);
    for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx)
        assert(blurred.bgra[((4+dy)*9+4+dx)*4+3]==blurred.bgra[((4-dy)*9+4-dx)*4+3]);
    Image flat{5,7,std::vector<uint8_t>(5*7*4)};
    for(size_t i=0;i<flat.bgra.size();i+=4) {flat.bgra[i]=27;flat.bgra[i+1]=81;flat.bgra[i+2]=190;flat.bgra[i+3]=128;}
    const auto constant=Process(flat,{4,1,1,1},never);assert(constant.bgra==flat.bgra);
    const auto gray=Process(flat,{0,1,1,0},never);
    for(size_t i=0;i<gray.bgra.size();i+=4) assert(gray.bgra[i]==gray.bgra[i+1]&&gray.bgra[i]==gray.bgra[i+2]&&gray.bgra[i+3]==128);
    int calls=0;const auto cancelled=Process(flat,{16,1,1,1},[&]{return ++calls>3;});assert(cancelled.bgra.empty());
    assert(!ValidImage(Process({}, {},never)));
}
