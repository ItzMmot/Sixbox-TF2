#include "../SixBox/src/Features/Aimbot/AutoStickyJump/StickyJumpPolicy.h"
#include "../SixBox/src/Features/Aimbot/AutoRocketJump/JumpPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>
int main()
{
    using namespace StickyJump;
    assert(OwnsCommand(12,12)&&!OwnsCommand(12,13)&&!OwnsCommand(-1,-1));
    const float step=1.f/66.f;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    // Real launcher attributes/runes, rather than a fixed stock delay.
    for(float attribute:{.25f,1.f,1.5f,2.f}) for(int rune=0;rune<3;++rune)
    {
        const float duration=ArmTime(.8f,attribute,rune);
        assert(duration>=0);
        assert(!Armed(10+duration,10,duration,step));
        assert(!Armed(10+duration+step*.5f,10,duration,step));
        assert(Armed(10+duration+step*2,10,duration,step));
    }
    assert(ArmTime(nan,1,0)<0&&ArmTime(.8f,-1,0)<0);
    assert(!Armed(100,0,.8f,step)&&!Armed(100,1,nan,step)&&!Armed(100,1,.8f,0));
    // A shot is a press followed by a different command's release, with the
    // native charge actually started. Camera smoothing must finish first.
    assert(!ReleaseCharge(11,11,1,true));
    assert(!ReleaseCharge(10,11,1,true));
    assert(!ReleaseCharge(12,11,0,true));
    assert(!ReleaseCharge(12,11,1,false));
    assert(ReleaseCharge(12,11,1,true));
    assert(!ReleaseCharge(12,11,nan,true));
    // Never claim two simultaneous shots if the real cooldown exceeds a packet.
    assert(BurstTicks(.6f,step,22)==22);
    assert(BurstTicks(.6f,step,50)==42);
    assert(BurstTicks(.1f,step,22)==9);
    assert(BurstTicks(.6f,step,1)==0&&BurstTicks(nan,step,22)==0&&BurstTicks(.6f,0,22)==0);
    // Lead grows with velocity, arm time and the second shot's delay.
    const float one=LeadDistance(280,280,2800,.8f+.3f,96);
    const float two=LeadDistance(280,280,2800,.8f+.6f+.3f,96);
    assert(one>=308.f-.001f&&two>one+160.f);
    assert(LeadDistance(600,280,2800,1,96)>=600);
    assert(LeadDistance(0,280,2800,1,96)>250);
    assert(LeadDistance(nan,280,2800,1,96)<0);
    // Regression: stale first-shot charge must never count as a second shot.
    FireGate fire;
    assert(fire.Ready(10,9,0,step));
    fire.Release(10,.6f,8);
    assert(!fire.Cleared(10+step,10.6f,10,7,1,1,step));
    assert(!fire.Ready(10.7f,10.6f,10,step));
    assert(fire.Cleared(10+2*step,10.6f,0,7,1,1,step));
    assert(!fire.Ready(10.6f,10.6f,0,step));
    assert(fire.Ready(10.6f+2*step,10.6f,0,step));
    fire.Release(10.65f,.6f,7);
    assert(!fire.Cleared(10.66f,10.6f,0,7,1,2,step));
    assert(fire.Cleared(10.7f,11.25f,0,6,2,2,step));
    // A real blast corridor remains usable even 24 HU away from the old marker.
    // Test an entire moving approach, not only a single hand-picked position.
    for(float speed:{120.f,280.f,600.f,1000.f}) for(float lateral:{0.f,24.f,40.f})
    for(auto mode:{RocketJump::Mode::Horizontal,RocketJump::Mode::Vertical})
    {
        bool launched=false;
        for(int tick=0;tick<300;++tick)
        {
            const float elapsed=tick*step,origin=-200.f+elapsed*speed;
            const auto window=FindWindow(step,(std::max)(0.f,.2f-elapsed),[&](float time,float delay) {
                const float x=origin+speed*(time+delay);
                const float nx=(std::max)(0.f,std::abs(x)-24.f),ny=(std::max)(0.f,std::abs(lateral)-24.f);
                return RocketJump::BoostScore(mode,x,lateral,40.f,std::sqrt(nx*nx+ny*ny+25.f),146,1,0);
            });
            if(window.Now(step)) {launched=true;assert(elapsed+step*2>=.2f-1e-5f);break;}
            if(origin>200) break;
        }
        assert(launched);
    }
    assert(!FindWindow(step,1.f,[](float,float){return 0.f;}).Now(step));
    assert(!FindWindow(step,1.f,[](float,float){return 1.f;}).Now(step));
    assert(!FindWindow(nan,0.f,[](float,float){return 1.f;}).valid);
    // A missing timestamp cannot silently look like an already armed sticky.
    assert(CreationTime(0,100,99,100,false)==100);
    assert(CreationTime(nan,100,99,100,false)==100);
    assert(CreationTime(20,100,99,100,false)==100);
    assert(CreationTime(99.5f,100,99,100,false)==99.5f);
    assert(CreationTime(20,100,99,100,true)==20);
    assert(!Armed(100.1f,CreationTime(0,100,99,100,false),.8f,step));
    FlushGate fresh;assert(fresh.Done(0,0,1));
    FlushGate flushing;
    assert(!flushing.Done(2,2,1));assert(!flushing.Done(0,1,2));
    assert(!flushing.Done(0,0,3));assert(!flushing.Done(0,0,3));
    assert(flushing.Done(0,0,4));
    FlushGate stale;
    assert(!stale.Done(0,1,1));assert(!stale.Done(0,0,2));
    assert(!stale.Done(1,1,3));assert(!stale.Done(0,0,4));assert(stale.Done(0,0,5));
    assert(!DetonateWindow(0,0,7)&&!DetonateWindow(nan,1,7));
    assert(!DetonateWindow(.5f,1,1)&&DetonateWindow(.99f,1,1)&&DetonateWindow(.5f,1,7));
    // Directed placement scores distinguish forward launch from a backwards push.
    const auto horizontal=RocketJump::Mode::Horizontal,vertical=RocketJump::Mode::Vertical;
    assert(RocketJump::BoostScore(horizontal,80,0,38,60,146,1,0)>
           RocketJump::BoostScore(horizontal,-80,0,38,60,146,1,0));
    assert(RocketJump::BoostScore(vertical,0,0,38,10,146,1,0)>
           RocketJump::BoostScore(vertical,80,0,38,60,146,1,0));
    std::cout<<"Sticky jump: weapon arm times, release edges, packet limits, momentum lead, stale-charge reset, observed-contact gates and launch windows passed\n";
}
