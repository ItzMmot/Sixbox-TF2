#include "../SixBox/src/Features/Aimbot/AutoStickyJump/JumpController.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <string_view>

using namespace StickyJump;

static void Physics()
{
    Bomb bomb; bomb.position={0,0,1};
    Body standing, ducked;
    ducked.ducked=true; ducked.maxs.z=62;
    const auto up=BlastImpulse(bomb,standing,true);
    const auto crouched=BlastImpulse(bomb,ducked,true);
    assert(up.z>790 && up.z<815); // 0.75 self damage, base 120, force scale 9.
    assert(crouched.z==1000); // Per-explosion force cap precedes the attribute.
    assert(BlastImpulse(bomb,standing,false).Length()==0);
    standing.origin={200,0,0};
    assert(BlastImpulse(bomb,standing,true).Length()==0);
    standing.origin={170,0,0}; // Nearest hull point, rather than center radius.
    assert(BlastImpulse(bomb,standing,true).x>0);
    bomb.pushMultiplier=2;
    assert(BlastImpulse(bomb,ducked,true).z==2000);
    bomb.pushMultiplier=1;
    const auto two=BlastImpulse(bomb,ducked,true)+BlastImpulse(bomb,ducked,true);
    assert(two.z==2000); // Do not cap a whole multi-sticky jump at 1000.
    const auto invalid=std::numeric_limits<float>::quiet_NaN();
    bomb.position.x=invalid;
    assert(BlastImpulse(bomb,ducked,true).Length()==0);
    bomb.position.x=0; bomb.falloff=invalid;
    assert(BlastImpulse(bomb,ducked,true).Length()==0);

    for (auto direction:{Direction::Horizontal,Direction::Diagonal,Direction::Vertical})
        for (float yaw:{0.f,90.f,180.f,-90.f})
        {
            const auto wish=DesiredDirection(direction,yaw);
            assert(std::abs(wish.Length()-1)<.001f);
            assert(MeetsTarget(wish*500,wish,500));
            assert(!MeetsTarget(wish*-500,wish,500));
            assert(!MeetsTarget(wish*700,wish,500));
        }
    assert(!MeetsTarget({},DesiredDirection(Direction::Vertical,0),0));
    assert(!MeetsTarget({invalid,0,0},{0,0,1},500));
    assert(SupportedLauncher(true,0)); // Stock/reskins, Jumper, Quickiebomb.
    assert(!SupportedLauncher(true,1)); // Scottish's view-dependent detonation.
    assert(!SupportedLauncher(false,0));
    Bomb armed; armed.created=10;
    assert(!Armed(armed,10.7f) && Armed(armed,10.81f));
    armed.armTime=.2f;
    assert(Armed(armed,10.21f) && !Armed(armed,9));
    armed.ageKnown=false;
    assert(!Armed(armed,100));
}

static void Normalization()
{
    Search search;
    const auto wish=DesiredDirection(Direction::Diagonal,0);
    Candidate sample;
    sample.valid=true; sample.delta=wish*1000.f;
    search.Add(sample,wish);
    sample.delta=wish*500.f;
    search.Add(sample,wish);
    sample.delta=wish*-10000.f;
    search.Add(sample,wish);
    sample.valid=false; sample.delta=wish*2000.f;
    search.Add(sample,wish);
    assert(std::abs(search.Maximum()-1000)<.001f);
    search.Clear(); assert(search.Maximum()==0);
}

struct FakeWorld
{
    double time=0, quantum=.000001;
    int generations=0, samples=0, jumpProbes=0, matchingJumpTick=0;
    double readyCost=0, jumpCost=0;
    bool ready=false, jump=false;
    float measured=500, liveBoost=500;
    std::array<float,3> autoMaximum={800,1000,1200};
    Direction probeDirection=Direction::Diagonal;
    double Now() { time+=quantum; return time; }
    void BeginSearch(const Frame&,const Request&) { ++generations; }
    SampleResult NextCandidate(int cursor,const Frame&,const Request& r,Candidate& c)
    {
        ++samples;
        if (cursor>=(r.automatic ? 6 : 2)) return SampleResult::Done;
        const auto direction=r.automatic ? Direction(cursor/2) : r.direction;
        const float maximum=r.automatic ? autoMaximum[int(direction)] : 1000.f;
        c.valid=maximum>0;
        c.delta=DesiredDirection(direction,r.yaw)*maximum*(cursor%2 ? 1.f : .5f);
        return SampleResult::Sample;
    }
    bool Ready(const Frame& f,const Request& r,float target,Vec& delta)
    {
        time+=readyCost;
        probeDirection=r.direction;
        delta=r.Wish()*liveBoost;
        if (!LaunchArmed(f)) return false;
        return ready && MeetsTarget(delta,r.Wish(),target);
    }
    bool CanBeginJump(const Frame&,const Request& r,const Candidate& c,float)
    {
        probeDirection=r.direction;
        ++jumpProbes; time+=jumpCost;
        return jump && (!matchingJumpTick || matchingJumpTick==c.jumpTicks);
    }
    void RecordBaseline(const Frame&) {}
    float MeasuredBoost(const Body&,const Frame&) { return measured; }
};

static Frame Initial()
{
    Frame f;
    f.command=1; f.weapon=42; f.gameTime=f.projectileTime=10; f.wallTime=1;
    f.enabled=f.eligible=f.grounded=f.pressed=f.canDetonate=true;
    return f;
}
static void Tick(Frame& f)
{
    ++f.command; f.pressed=false; f.wallTime+=.015; f.gameTime+=.015f; f.projectileTime+=.015f;
}
static Bomb Sticky(uint32_t id,float created=1)
{
    Bomb b; b.id=id; b.created=created;
    return b;
}
static bool Status(const Controller& controller,std::string_view text)
{ return std::string_view(controller.Snapshot().status).find(text)!=std::string_view::npos; }

static void Cancellations()
{
    Request request;
    for (int cause=0;cause<8;++cause)
    {
        Controller controller; FakeWorld world;
        auto f=Initial(); f.bombs={Sticky(1)};
        controller.Run(f,request,world); assert(controller.Busy());
        Tick(f);
        switch(cause)
        {
        case 0: f.enabled=false; break;
        case 1: f.eligible=false; break; // death/class/focus/weapon support.
        case 2: ++f.weapon; break; // handle serial is part of identity.
        case 3: f.manualCombat=true; break;
        case 4: f.dodge=true; break;
        case 5: f.shifting=true; break;
        case 6: f.pressed=true; break;
        case 7: f.wallTime+=9; break;
        }
        const auto output=controller.Run(f,request,world);
        assert(!output.controls && !output.detonate && !controller.Busy());
        assert(f.bombs.size()==1); // Cancellation does not flush existing traps.
    }
    for (int cause=0;cause<4;++cause)
    {
        Controller controller; FakeWorld world; auto f=Initial();
        if (cause==0) f.grounded=false;
        if (cause==1) f.manualCombat=true;
        if (cause==2) f.eligible=false;
        if (cause==3) request.power=0;
        assert(!controller.Run(f,request,world).controls && !controller.Busy());
    }
}

static void AssistAndReplanning()
{
    Controller controller; FakeWorld world; Request request;
    auto f=Initial(); f.grounded=false; f.bombs={Sticky(1)};
    auto output=controller.Run(f,request,world);
    assert(output.controls);
    // The launch request freezes its azimuth and normalization across replans.
    f.yaw=90; request.power=100;
    f.bombs.push_back(Sticky(2)); Tick(f);
    output=controller.Run(f,request,world);
    assert(world.generations==1 && controller.Snapshot().target<501);
    world.ready=true; Tick(f); output=controller.Run(f,request,world);
    assert(output.detonate && !output.jump);
    assert(output.duck);

    Controller blocked; FakeWorld wall;
    f=Initial(); f.bombs={Sticky(1)};
    blocked.Run(f,{},wall);
    for (int i=0;i<8;++i) { Tick(f); blocked.Run(f,{},wall); }
    assert(blocked.Busy());
    f.wallTime+=8; Tick(f); blocked.Run(f,{},wall);
    assert(!blocked.Busy() && Status(blocked,"8 seconds"));

    Controller rolling; FakeWorld clock;
    f=Initial(); f.bombs={Sticky(1)};
    rolling.Run(f,{},clock); Tick(f); f.gameTime-=1;
    output=rolling.Run(f,{},clock);
    assert(rolling.Busy() && !output.detonate); // Tickbase rewind is not a wall timeout.
    auto old=f; old.command--;
    assert(!rolling.Run(old,{},clock).controls);
    rolling.Reset(); assert(!rolling.Busy());
}

static void FailureAndBudget()
{
    Request request;
    Controller lost; FakeWorld world;
    auto f=Initial(); f.bombs={Sticky(1)};
    lost.Run(f,request,world);
    f.bombs.clear(); Tick(f); lost.Run(f,request,world);
    assert(!lost.Busy() && !lost.Current().detonate && Status(lost,"disappeared"));

    Controller bounded; FakeWorld slow; slow.quantum=.0004;
    f=Initial(); f.bombs={Sticky(1)};
    bounded.Run(f,request,slow);
    assert(slow.samples<=2 && bounded.Busy());
    const int initial=slow.samples;
    Tick(f); bounded.Run(f,request,slow);
    assert(slow.samples>initial); // Work continues on a subsequent command.
}

static void LiveLaunch()
{
    Controller controller; FakeWorld world;
    auto f=Initial(); f.bombs={Sticky(1)};
    world.jump=true;
    auto output=controller.Run(f,{},world);
    assert(!output.jump && !output.detonate);
    Tick(f); output=controller.Run(f,{},world);
    assert(output.duck && output.jump && !output.detonate && controller.State()==Phase::Launch);
    f.body.origin.z=12; f.grounded=false;
    Tick(f); output=controller.Run(f,{},world);
    assert(output.duck && !output.jump && controller.Busy()); // Takeoff is no longer a walking approach.
    world.ready=true; world.liveBoost=900; // Stale plan no longer matches the live impulse.
    Tick(f); output=controller.Run(f,{},world);
    assert(!output.detonate);
    world.liveBoost=500;
    Tick(f); output=controller.Run(f,{},world);
    assert(output.detonate && !output.jump);
    world.measured=0; f.bombs.clear(); // Destruction without a velocity boost is not success.
    Tick(f); controller.Run(f,{},world);
    assert(controller.Busy());
    f.wallTime+=1; Tick(f); controller.Run(f,{},world);
    assert(!controller.Busy() && Status(controller,"not confirmed"));

}

static void ArmingClocks()
{
    for (float armTime:{.8f,.2f})
        for (int count:{1,2})
        {
            Controller controller; FakeWorld world;
            // Deliberately let the movement adapter offer an early jump. The
            // controller must still wait for the youngest projectile to arm.
            world.jump=true;
            auto f=Initial(); f.gameTime+=1.f;
            f.bombs={Sticky(1,f.projectileTime)};
            if (count==2) f.bombs.insert(f.bombs.begin(),Sticky(2,1));
            for (auto& bomb:f.bombs) bomb.armTime=armTime;
            auto output=controller.Run(f,{},world);
            int waited=0;
            while (!LaunchArmed(f))
            {
                assert(controller.Busy() && controller.State()==Phase::Approach);
                assert(!output.jump && !output.duck && !output.detonate);
                assert(Status(controller,"arming"));
                assert(!controller.Run(f,{},world).jump); // Replay cannot bypass the wait.
                Tick(f); output=controller.Run(f,{},world);
                assert(++waited<100);
            }
            assert(waited>int(armTime/f.interval)); // Includes the boundary margin.
            assert(!output.jump && !output.detonate);
            Tick(f); output=controller.Run(f,{},world);
            assert(output.jump && output.duck && !output.detonate);
            f.grounded=false; f.body.origin.z=12; world.ready=true;
            Tick(f); output=controller.Run(f,{},world);
            assert(output.detonate && output.duck && !output.jump);
            Tick(f); output=controller.Run(f,{},world);
            assert(!output.detonate);
        }

    Controller cooldown; FakeWorld world; world.jump=true;
    auto f=Initial(); f.bombs={Sticky(1)}; f.canDetonate=false;
    auto output=cooldown.Run(f,{},world);
    assert(!output.jump && !output.detonate && cooldown.State()==Phase::Approach);
    f.canDetonate=true; Tick(f); output=cooldown.Run(f,{},world);
    assert(!output.jump && !output.detonate);
    Tick(f); output=cooldown.Run(f,{},world);
    assert(output.jump && !output.detonate);

    Controller assist; FakeWorld ready; ready.ready=true;
    Request request;
    f=Initial(); f.gameTime+=1; f.bombs={Sticky(1,f.projectileTime)};
    output=assist.Run(f,request,ready);
    assert(!output.jump && !output.detonate);
    f.projectileTime+=1; Tick(f); output=assist.Run(f,request,ready);
    assert(output.detonate);
}

static void ExpensiveLaunchChecks()
{
    for (float power:{50.f,75.f,100.f})
    {
        Controller controller; FakeWorld world;
        world.readyCost=.002; world.jumpCost=.002; world.jump=true;
        Request request; request.power=power;
        auto f=Initial(); f.bombs={Sticky(100)};
        auto output=controller.Run(f,request,world);
        for (int tick=0;tick<8 && !output.jump;++tick)
        {
            Tick(f); output=controller.Run(f,request,world);
        }
        assert(world.jumpProbes>0 && output.jump && output.duck);
        assert(!output.detonate && controller.State()==Phase::Launch);
        const int probes=world.jumpProbes;
        assert(controller.Run(f,request,world).jump && world.jumpProbes==probes);
        f.grounded=false; f.body.origin.z=12;
        Tick(f); output=controller.Run(f,request,world);
        assert(output.duck && !output.jump && !output.detonate);
        world.ready=true; world.liveBoost=controller.Snapshot().target;
        Tick(f); output=controller.Run(f,request,world);
        assert(output.detonate && output.duck && !output.jump);
    }

    // A few units of approach error can move a valid window to another tick.
    // Live preflight must try other timings without spending a jump first.
    for (int timing:{2,7,16})
    {
        Controller controller; FakeWorld world; world.jump=true;
        world.matchingJumpTick=timing; world.readyCost=world.jumpCost=.002;
        auto f=Initial(); f.bombs={Sticky(1)};
        auto output=controller.Run(f,{},world);
        for (int tick=0;tick<34 && !output.jump;++tick)
        {
            assert(!output.detonate && controller.State()==Phase::Approach);
            Tick(f); output=controller.Run(f,{},world);
        }
        assert(output.jump && controller.Snapshot().plan.jumpTicks==timing);
    }

    Controller invalid; FakeWorld noWindow;
    auto f=Initial(); f.bombs={Sticky(1)};
    invalid.Run(f,{},noWindow);
    for (int tick=0;tick<50 && invalid.Busy();++tick)
    {
        Tick(f); const auto output=invalid.Run(f,{},noWindow);
        assert(!output.jump && !output.detonate);
    }
    assert(invalid.Busy()); // An early failed scan must not reject a later window.
    f.wallTime+=8; Tick(f); invalid.Run(f,{},noWindow);
    assert(!invalid.Busy() && Status(invalid,"8 seconds"));

    Controller airborne; FakeWorld slow; slow.jump=true;
    f=Initial(); f.bombs={Sticky(1)};
    auto output=airborne.Run(f,{},slow);
    for (int tick=0;tick<3 && !output.jump;++tick)
    { Tick(f); output=airborne.Run(f,{},slow); }
    assert(output.jump);
    f.grounded=false; f.body.origin.z=12; slow.readyCost=.002;
    for (int tick=0;tick<5;++tick)
    {
        Tick(f); output=airborne.Run(f,{},slow);
        assert(output.duck && !output.jump && !output.detonate);
    }
    slow.ready=true;
    Tick(f); output=airborne.Run(f,{},slow);
    assert(output.duck && output.detonate);
}

static void AssistWaitingAndDelayedJump()
{
    Controller controller; FakeWorld world; Request request;
    auto f=Initial(); f.bombs={Sticky(1)};
    auto output=controller.Run(f,request,world);
    assert(output.controls && controller.Busy());
    const float target=controller.Snapshot().target;
    // No immediate window is a waiting state, not a failed activation. Manual
    // direction/speed can change while the original goal stays frozen.
    for (int tick=0;tick<60;++tick)
    {
        Tick(f); f.body.origin.x+=2; f.body.velocity.x=200; f.yaw=90;
        request.power=100;
        output=controller.Run(f,request,world);
        assert(controller.Busy() && output.controls);
        assert(!output.jump && !output.detonate);
        assert(controller.Snapshot().target==target);
    }
    world.jump=true; world.matchingJumpTick=7;
    for (int tick=0;tick<35 && !output.jump;++tick)
    { Tick(f); output=controller.Run(f,request,world); }
    assert(output.jump && output.duck && !output.detonate);
    f.grounded=false; f.body.origin.z=12; world.ready=true;
    Tick(f); output=controller.Run(f,request,world);
    assert(output.detonate && output.duck && !output.jump);

    Controller airborne; FakeWorld waiting;
    f=Initial(); f.grounded=false; f.bombs={Sticky(1)}; request.power=50;
    for (int tick=0;tick<30;++tick)
    {
        output=airborne.Run(f,request,waiting);
        assert(airborne.Busy() && !output.jump);
        Tick(f);
    }
    assert(waiting.jumpProbes==0); // No airborne jump preflight is attempted.
    waiting.ready=true; output=airborne.Run(f,request,waiting);
    assert(output.detonate && !output.jump);

    Controller timeout; FakeWorld noWindow;
    f=Initial(); f.bombs={Sticky(1)};
    timeout.Run(f,request,noWindow);
    f.wallTime+=8; Tick(f); output=timeout.Run(f,request,noWindow);
    assert(!timeout.Busy() && !output.controls && !output.detonate);

    Controller empty; FakeWorld none;
    f=Initial(); output=empty.Run(f,request,none);
    assert(!output.controls && Status(empty,"No owned stickies"));

    Controller transient; FakeWorld passing;
    f=Initial(); f.bombs={Sticky(1)};
    transient.Run(f,request,passing);
    Tick(f); output=transient.Run(f,request,passing);
    assert(!output.jump && passing.jumpProbes==1);
    passing.ready=true;
    Tick(f); output=transient.Run(f,request,passing);
    assert(output.detonate); // Future preflight cannot hide a new immediate window.
}

static void AutomaticDirection()
{
    assert(DirectionForElevation(90)==Direction::Vertical);
    assert(DirectionForElevation(70)==Direction::Vertical);
    assert(DirectionForElevation(69.9f)==Direction::Diagonal);
    assert(DirectionForElevation(15.1f)==Direction::Diagonal);
    assert(DirectionForElevation(15)==Direction::Horizontal);
    assert(DirectionForElevation(0)==Direction::Horizontal);
    assert(DirectionForElevation(-89)==Direction::Horizontal);

    Request request; request.automatic=true;
    Controller controller; FakeWorld world;
    auto f=Initial(); f.bombs={Sticky(1)}; f.pitch=89;
    assert(!controller.Run(f,request,world).jump);
    assert(world.probeDirection==Direction::Horizontal && std::abs(controller.Snapshot().target-400)<.01f);
    Tick(f); f.pitch=-70; world.jump=true;
    const auto takeoff=controller.Run(f,request,world);
    assert(takeoff.jump && !takeoff.detonate && world.probeDirection==Direction::Vertical);
    assert(std::abs(controller.Snapshot().target-600)<.01f && world.generations==1);
    Tick(f); f.grounded=false; f.pitch=-45; world.ready=true; world.liveBoost=600;
    assert(controller.Run(f,request,world).detonate);
    assert(world.probeDirection==Direction::Vertical && std::abs(controller.Snapshot().target-600)<.01f);
    assert(controller.Snapshot().automatic && controller.Snapshot().direction==Direction::Vertical);
    assert(controller.Snapshot().elevation==70);

    Controller immediate; FakeWorld passing;
    passing.autoMaximum[0]=0;
    f=Initial(); f.bombs={Sticky(1,f.projectileTime)};
    assert(!immediate.Run(f,request,passing).jump && immediate.Busy());
    Tick(f); f.projectileTime+=1; f.pitch=344; passing.ready=true;
    const auto launch=immediate.Run(f,request,passing);
    assert(launch.jump && launch.detonate && passing.probeDirection==Direction::Diagonal);
    assert(std::abs(immediate.Snapshot().target-500)<.01f && passing.generations==1);
    assert(immediate.Snapshot().direction==Direction::Diagonal && immediate.Snapshot().elevation==16);
}

int main()
{
    Physics(); Normalization(); Cancellations();
    AssistAndReplanning(); FailureAndBudget(); LiveLaunch(); ArmingClocks(); ExpensiveLaunchChecks();
    AssistWaitingAndDelayedJump(); AutomaticDirection();
    std::cout << "Auto Sticky Jump: independent solver and controller checks passed\n";
}
