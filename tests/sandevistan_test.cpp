#include "../SixBox/src/Features/Visuals/Sandevistan/TrailPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>
struct Color
{
    unsigned char r,g,b,a;
    Color(unsigned char r,unsigned char g,unsigned char b,unsigned char a):r(r),g(g),b(b),a(a){}
};
int main()
{
    using namespace Sandevistan;
    // Mirrors camera direction, wraps +/-180, ignores cuts and decays to rest.
    CameraMotion left,right,vertical;
    left.Update(1,0,0);right.Update(1,0,0);vertical.Update(1,0,0);
    assert(left.x==0&&left.y==0);
    left.Update(1.02,4,0);right.Update(1.02,-4,0);vertical.Update(1.02,0,4);
    assert(left.x>0&&right.x<0&&std::abs(left.x+right.x)<.001f);
    assert(vertical.y>0&&vertical.x==0);
    const float moving=left.x;
    left.Update(1.04,4,0);assert(left.x<moving);
    CameraMotion wrap;wrap.Update(1,179,0);wrap.Update(1.02,-179,0);assert(wrap.x>0);
    CameraMotion cut;cut.Update(1,0,0);cut.Update(1.02,180,0);assert(cut.x==0);
    cut.Update(2,10,0);assert(cut.x==0&&cut.y==0);
    CameraMotion still;still.Update(1,0,0);still.Update(1.02,0,0);assert(still.x==0&&still.y==0);
    // Eight stationary copies no longer occupy a 4-HU-wide single silhouette.
    const float extension=TrailExtension(8,0,48,4,4);
    assert(extension==112&&extension/7>=16);
    assert(TrailExtension(8,200,48,4,4)==0);
    assert(TrailExtension(8,0,48,0,0)==0);
    for(int i=0;i<8;++i)
    {
        const float rank=float(i)/7;
        assert(BehindOffset(rank,extension)<=0); // ground copies stay behind the player
        assert(BehindOffset(rank,0)==0); // moving airborne palettes remain unshifted
        if(i) assert(BehindOffset(rank,extension)>=BehindOffset(float(i-1)/7,extension));
    }
    assert(BehindOffset(1,extension)==0);
    assert(BehindOffset(0,extension)==-extension);

    assert(TrailExtension(1,0,48,4,4)==0);
    // Predicting one shot/movement tick ahead must not place a whole body
    // around the first person camera. Test the hull, not only its eye point.
    assert(CameraIntersects(0,0,68,-18,-24,0,30,24,82,20));
    assert(CameraIntersects(0,0,68,12,-24,0,60,24,82,20));
    assert(!CameraIntersects(0,0,68,76,-24,0,124,24,82,20));
    assert(!CameraIntersects(0,0,170,-24,-24,0,24,24,82,20));
    // Reproduce the actual render fallback: one engine palette must produce
    // distinct historical bodies, even when prediction supplied no bones.
    using Matrix=float[3][4];
    Matrix live[2]={{{1,0,0,100},{0,1,0,200},{0,0,1,10}},
                    {{1,0,0,100},{0,1,0,200},{0,0,1,80}}};
    Matrix from={{1,0,0,100},{0,1,0,200},{0,0,1,10}};
    Matrix trail[8][2]{};
    const Matrix* queued[8]{};
    for(int copy=0;copy<8;++copy)
    {
        Matrix to={{1,0,0,float(copy*20)},{0,1,0,0},{0,0,1,0}};
        RebaseBones(trail[copy],live,2,from,to);queued[copy]=trail[copy];
        assert(trail[copy][0][0][3]==copy*20);
        assert(trail[copy][0][1][3]==0);
        assert(trail[copy][1][2][3]==70);
        if(copy)assert(trail[copy][0][0][3]!=trail[copy-1][0][0][3]);
    }
    // Consume only after all draws are submitted: deferred rendering must not
    // see eight pointers to one shared scratch palette containing the last pose.
    for(int copy=0;copy<8;++copy)
    {
        assert(queued[copy][0][0][3]==copy*20);
        if(copy)assert(queued[copy]!=queued[copy-1]);
    }
    // Later animation changes must not move an already frozen copy.
    live[1][2][3]=140;assert(trail[0][1][2][3]==70);
    Matrix turn={{0,-1,0,30},{1,0,0,40},{0,0,1,0}};
    Matrix rotated[2]{};live[1][0][3]=110;
    RebaseBones(rotated,live,2,from,turn);
    assert(rotated[1][0][3]==30&&rotated[1][1][3]==50);
    assert(rotated[1][0][0]==0&&rotated[1][1][0]==1);
    assert(live[0][0][3]==100&&live[0][1][3]==200); // Seed body untouched.
    Burst burst;
    burst.Start(100,Warp);
    assert(burst.Append(100,8,10)&&!burst.confirmed&&burst.total==9);
    assert(!burst.Append(100,8,10.01)&&!burst.confirmed);
    assert(!burst.Split(101,Warp,10.01));
    assert(burst.Append(101,7,10.01)&&burst.confirmed&&burst.total==9);
    const auto born=burst.born;
    assert(!burst.Append(99,9,10.02)&&burst.born==born);
    assert(burst.Append(102,6,10.02)&&burst.born==born);
    assert(burst.Split(104,Warp,10.02));
    assert(burst.Split(103,Doubletap,10.02));
    assert(burst.Split(103,Warp,11));
    burst.Start(200,Doubletap);
    assert(!burst.confirmed&&burst.marked==0);
    assert(burst.Append(200,1,11)&&!burst.confirmed);
    assert(burst.Append(201,0,11.01)&&burst.confirmed&&burst.total==2);
    // Crit seed command rewrites must not split one physical burst into several gradients.
    Burst rewritten;rewritten.Start(100,Doubletap);
    for(int i=0;i<22;++i)
    {
        const int wire=i%3==0?50000+i*137:100+i;
        const int logical=InputSequence(100+i,wire);
        if(i>0) assert(!rewritten.Split(logical,Doubletap,20+i*.0001));
        assert(rewritten.Append(logical,21-i,20+i*.0001));
    }
    assert(rewritten.marked==22&&rewritten.total==22&&rewritten.confirmed);
    assert(InputSequence(0,123)==123);
    const Color start(10,250,60,220),end(240,30,180,40);
    auto first=Gradient(start,end,Rank(0,8,2,false));
    auto last=Gradient(start,end,Rank(7,8,2,false));
    assert(first.r==10&&first.g==250&&first.b==60&&first.a==220);
    assert(last.r==240&&last.g==30&&last.b==180&&last.a==40);
    assert(Gradient(start,end,Rank(0,8,1,true)).r==end.r);
    assert(Gradient(start,end,.5f).a==130);
    assert(Rank(1,3,2,false)<Rank(1,3,1,false));
    for(int captured=2;captured<=64;++captured)
    {
        float prior=-1;
        for(int index=0;index<captured;++index)
        {
            const float rank=CapturedRank(index,captured);assert(rank>prior);prior=rank;
            const auto color=Gradient(start,end,rank);
            if(index==0) assert(color.r==start.r);
            if(index==captured-1) assert(color.r==end.r);
        }
    }
    // Every possible burst size: evenly selected copies, no duplicate slots,
    // and both trajectory endpoints even when the renderer draws fewer poses.
    for(int total=1;total<=64;++total)for(int wanted=1;wanted<=MaxCopies;++wanted)
    {
        const int copies=std::min(total,wanted);int selected=0,previous=-1;
        for(int index=0;index<total;++index)
        {
            int slot=Sample(index,total,wanted);
            if(slot<0)continue;
            assert(slot==previous+1);previous=slot;++selected;
        }
        assert(selected==copies);
        assert(Sample(total-1,total,wanted)==copies-1);
        if(copies>1)assert(Sample(0,total,wanted)==0);
    }
    assert(Sample(-1,5,3)==-1&&Sample(5,5,3)==-1&&Sample(0,0,2)==-1);
    assert(Fade(-.01,1,0,1,0,0)==0);
    assert(Fade(1,1,0,1,1,0)==0);
    assert(Fade(100,1,0,1,1,0)==0);
    assert(Fade(std::numeric_limits<double>::quiet_NaN(),1,0,1,1,0)==0);
    assert(Fade(0,1,.1f,1,0,0)==0);
    assert(Fade(.1,1,.1f,1,0,0)>.8f);
    assert(Fade(.5,1,0,1,1,0)>Fade(.75,1,0,1,1,0));
    // Stagger removes the oldest copy first without reversing its gradient.
    assert(Fade(.8,1,0,1,0,.5f)==0);
    assert(Fade(.8,1,0,1,1,.5f)>0);
    std::cout<<"Sandevistan: render palette rebasing/freeze, camera direction/wrap, hull exclusion, trail separation, confirmation/replay, gradient, burst sampling (1536 combinations), lifetime and stagger passed\n";
}
