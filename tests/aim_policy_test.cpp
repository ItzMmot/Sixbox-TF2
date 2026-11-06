// tests/aim_policy_test.cpp - AimPolicy geometry, ranking and shot accounting.
#include "../Unibox/src/Features/Aimbot/Shared/AimPolicy.h"
#include <cassert>
#include <limits>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void* operator new(std::size_t size) {++allocations;if(void* p=std::malloc(size?size:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace AimPolicy;
int main()
{
    const unsigned before=allocations;
    {
        constexpr float tick=1.f/66.f;
        assert(DirectWindowOpen(.07246f,4*tick,.04435f,tick,1));
        assert(DirectWindowOpen(.07246f,5*tick,.04435f,tick,2));
        assert(!DirectWindowOpen(.07246f,8*tick,.04435f,tick,2));
        assert(!DirectWindowOpen(.07246f,5*tick,.04435f,tick,4));
        assert(!DirectWindowOpen(1.f,2.f,100.f,tick,1));
        assert(!DirectWindowOpen(1.f,1.f,1.f,0.f,1));
        assert(!DirectWindowOpen(std::numeric_limits<float>::quiet_NaN(),1.f,1.f,tick,1));
        // The moved target is beyond a wall at fraction .5: wall must win.
        const float target=RayBox({0,0,40},{100,0,0},{68,-25,-1},{118,25,83},1.f);
        assert(target>.67f&&target<.69f);
        assert(!(target<.5f));
        assert(target<1.f);
        assert(RayBox({0,0,100},{100,0,0},{68,-25,-1},{118,25,83},1.f)<0);
    }
    {
        assert(BackstabGeometry({32,0,0},{1,0,0},{1,0,0}));
        assert(!BackstabGeometry({32,0,0},{1,0,0},{-1,0,0}));
        assert(!BackstabGeometry({32,0,0},{-1,0,0},{1,0,0}));
        assert(!BackstabGeometry({32,0,0},{0,1,0},{1,0,0}));
        assert(BackstabGeometry({32,0,10000},{1,0,-100},{1,0,100}));
        assert(BackstabGeometry({32,0,0},{1,1,0},{1,0,0}));
        assert(!BackstabGeometry({},{1,0,0},{1,0,0}));
        // Side/corner windows use the native thresholds, without an unrelated
        // extrapolated target yaw being required to pass a second time.
        assert(BackstabGeometry({32,0,0},{1,0,0},{.1f,1,0}));
        assert(!BackstabGeometry({32,0,0},{1,0,0},{-.1f,1,0}));
        const float nan=std::numeric_limits<float>::quiet_NaN();
        assert(!BackstabGeometry({nan,0,0},{1,0,0},{1,0,0}));
    }

    {
        // GetInfo already supplies gravity in HU/s^2, including sv_gravity.
        const auto velocity=FlightVelocity({1800,0,100},400,.25f);
        assert(velocity.x==1800&&velocity.z==0);
        assert(FlightVelocity({2600,0,0},80,.5f).z==-40);
        const float head[3][4]={{1,0,0,24},{0,1,0,0},{0,0,1,70}};
        const float body[3][4]={{1,0,0,24},{0,1,0,0},{0,0,1,35}};
        std::array<ArrowBox,2> boxes{{{{-4,-4,-4},{4,4,4},head},{{-10,-10,-15},{10,10,15},body}}};
        auto shape=[&](int i){return boxes[i];};
        assert(ArrowContactHitbox({0,0,70},{2000,0,0},.015f,2,shape)==0);
        assert(ArrowContactHitbox({0,0,35},{2000,0,0},.015f,2,shape)==1);
        // Finite trace misses: server-like fallback still picks the nearby head.
        assert(ArrowContactHitbox({0,0,70},{1000,0,0},.015f,2,shape)==0);
        const float farHead[3][4]={{1,0,0,100},{0,1,0,0},{0,0,1,70}};
        const float nearBody[3][4]={{1,0,0,20},{0,1,0,8},{0,0,1,70}};
        boxes[0].matrix=farHead;boxes[1]={ {-4,-4,-4},{4,4,4},nearBody };
        // An infinite ray would predict head; the actual finite touch picks body.
        assert(ArrowContactHitbox({0,0,70},{1200,0,0},.015f,2,shape)==1);
        assert(ArrowContactHitbox({},{},.015f,2,shape)==-1);
        SwingDeferral defer;
        assert(defer.Wait(55,100));
        for(int command=101;command<120;++command) assert(!defer.Wait(55,command));
        assert(defer.Wait(56,120));
        defer.Reset();assert(defer.Wait(55,121));
    }

    // A valid head grid must not perform 49 native muzzle solves. A timing
    // deferral retains the candidate for the next tick instead of scanning all.
    std::array<bool,49> headMask{};headMask.fill(true);
    int solves=0;
    assert(RefineHeadCell(headMask,[&](int cell){++solves;assert(cell==24);return 1;})==24&&solves==1);
    solves=0;
    assert(RefineHeadCell(headMask,[&](int){++solves;return -1;})==-1&&solves==1);
    solves=0;
    assert(RefineHeadCell(headMask,[&](int){++solves;return 0;})==-1&&solves==4);
    solves=0;
    assert(RefineHeadCell(headMask,[&](int){return ++solves==2?1:0;})>=0&&solves==2);
    headMask.fill(false);solves=0;
    assert(RefineHeadCell(headMask,[&](int){++solves;return 1;})==-1&&solves==0);

    assert(LargerQuadraticRoot(1,-3,2)==2);
    assert(LargerQuadraticRoot(-1,3,-2)==2);
    assert(std::isnan(LargerQuadraticRoot(1,0,1)));
    assert(std::isnan(LargerQuadraticRoot(0,1,1)));
    const Vector lo{10,-2,-2},hi{20,2,2};
    assert(RayBox({},{1,0,0},lo,hi,100)==10);
    assert(RayBox({},{-1,0,0},lo,hi,100)<0);
    assert(RayBox({0,3,0},{1,0,0},lo,hi,100)<0);
    assert(RayBox({},{1,0,0},lo,hi,9)<0);
    assert(RayBox({15,0,0},{1,0,0},lo,hi,100)==0);
    assert(RayBox({},{1,0,0},hi,lo,100)<0);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(RayBox({nan,0,0},{1,0,0},lo,hi,100)<0);

    const float rotated[3][4]={{0,-1,0,20},{1,0,0,0},{0,0,1,0}};
    assert(RayOBB({},{1,0,0},{-2,-4,-2},{2,4,2},rotated,100)==16);
    const float scaled[3][4]={{2,0,0,20},{0,2,0,0},{0,0,2,0}};
    assert(RayOBB({},{1,0,0},{-2,-2,-2},{2,2,2},scaled,100)==16);
    assert(RayOBB({},{1,0,0},{-2,-2,-2},{2,2,2},scaled,15)<0);
    assert(RayOBB({},{1,0,0},{-2,-2,-2},{2,2,2},scaled,100)<RayBox({},{1,0,0},{18,-2,-2},{22,2,2},100));
    assert(BoxDistance({},lo,hi)==10);
    assert(BoxDistance({15,0,0},lo,hi)==0);
    assert(BlastScore(0,100,.5f)>BlastScore(80,100,.5f));
    assert(BlastScore(50,100,.5f)>BlastScore(50,100,2));
    assert(BlastScore(100,100,1)==0&&BlastScore(nan,100,1)==0);
    assert(BlastScore(50,100,1,0)>BlastScore(50,100,1,100));
    assert(Confidence(300,.2f,24,0)>Confidence(300,1,24,0));
    assert(Confidence(300,.5f,24,0)>Confidence(300,.5f,24,2));
    assert(Confidence(300,.5f,50,0)>Confidence(300,.5f,4,0));
    assert(Confidence(300,.5f,0,0)==0);
    assert(PreferWindow(8,9,2));
    assert(!PreferWindow(1,5,2));
    assert(PreferWindow(1,.5f,2));
    assert(!PreferWindow(0,5,2));
    Budget budget{2};assert(budget.Take()&&budget.Take()&&!budget.Take());assert(!budget.Take(-1));
    FeetYaw feet{0,0};feet.Step(90,.015f,true);assert(feet.goal==90&&feet.current>0&&feet.current<90);
    feet={179,179};feet.Step(-179,.015f,true);assert(std::abs(std::remainder(feet.current-179,360.f))<3);
    feet={0,0};feet.Step(90,.015f,false);assert(feet.goal==45);
    std::array<bool,49> mask{};
    assert(BestHeadCell(mask)==-1);
    mask.fill(true);assert(BestHeadCell(mask)==24);
    mask.fill(false);mask[24]=true;assert(BestHeadCell(mask)==24);
    mask.fill(false);
    for(int y=0;y<3;++y) for(int x=0;x<3;++x) mask[y*7+x]=true;
    mask[24]=true;assert(BestHeadCell(mask)==8); // Wider patch beats the isolated centre.
    ShotLedger ledger;
    ledger.Add({1,10,100,1,false});ledger.Add({2,10,101,1,true});
    assert(!ledger.Hit(1,11,false));assert(ledger.Hit(1,10,false));
    assert(!ledger.Hit(2,10,false));assert(ledger.Hit(2,10,true));
    // Duplicate detection must still work when a slot before the shot was freed.
    ledger.Add({1,10,100,1,false});ledger.Add({2,10,101,1,true});
    ledger.Hit(1,10,false);ledger.Add({2,10,101,1,true});
    int pending=0;for(const auto& shot:ledger.shots) pending+=shot.target!=0;assert(pending==1);
    int misses=0;
    ledger.Expire(1.1f,100,[&](int){++misses;});assert(misses==0);
    ledger.Expire(1.1f,101,[&](int target){assert(target==2);++misses;});assert(misses==1);
    ledger.Expire(2,101,[&](int){++misses;});assert(misses==1);
    ledger.Add({3,10,200,3,false});ledger.Add({4,11,201,3,false});
    assert(ledger.Hit(3,10,false));
    ledger.Expire(3.1f,201,[&](int target){assert(target==4);++misses;});assert(misses==2);
    ledger.Add({5,10,300,4,false});ledger.Expire(7,299,[&](int){++misses;});assert(misses==2);
    ledger.Add({0,10,301,nan,false});
    ledger.Clear();
    for(int i=0;i<600;++i) ledger.Add({i+1,10,i,float(i+1),false});
    pending=0;for(const auto& shot:ledger.shots) pending+=shot.target!=0;assert(pending==512);
    int expired=0;ledger.Expire(601,601,[&](int){++expired;});assert(expired==2); // Old acknowledgements cannot turn a whole queue into learned misses.
    assert(allocations==before);
}
