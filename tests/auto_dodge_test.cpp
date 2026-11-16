// Windows SDK defines this legacy macro even for x64 builds.
#define near
#include "../SixBox/src/Features/Misc/AutoDodge/DodgePolicy.h"
#undef near
#include <cassert>
#include <iostream>
#include <random>

using namespace Dodge;

namespace
{
	Body Player(float x=0,float y=0,float z=0,float height=82) { return {{x,y,z},{-24,-24,0},{24,24,height}}; }
	int Open(size_t,size_t,V,V,V) { return 1; }
	int Covered(size_t,size_t,V,V,V) { return 0; }
	Score Rate(const std::vector<Body>& path,const std::vector<Threat>& threats,float margin=48)
	{ return Evaluate(path,threats,margin,.1f,Open); }
	Threat Shot(V from,V to,V hull={1,1,1})
	{
		Step s;s.from=from;s.to=to;s.hull=hull;s.direct=true;
		return {{s}};
	}
	Threat Area(V from,V to,float radius=146)
	{
		Step s;s.from=from;s.to=to;s.radius=radius;s.area=true;
		return {{s}};
	}
	struct FakeClock
	{
		using duration=std::chrono::nanoseconds;
		using time_point=std::chrono::time_point<FakeClock,duration>;
		static inline duration elapsed{};
		static time_point now() { return time_point(elapsed); }
	};
}

int main()
{
	// Terrain safety uses support along the whole route, including the
	// horizontal footprint of a jump and a margin before the platform edge.
	auto platform=[](V p,float reference,float depth)
	{
		bool exists=p.x<100&&p.y>-30&&p.y<30;
		return Floor{true,exists&&reference>=-18&&reference-depth<=0,0};
	};
	LedgeGuard alongBridge({0,0,0},56,{true,true,0});
	assert(alongBridge.Check({0,0,0},{450,0,0},{100,0,0},false,true,true,platform));
	assert(alongBridge.Check({40,0,0},{450,0,0},{100,0,0},false,true,true,platform));
	assert(!alongBridge.Check({50,0,0},{450,0,0},{100,0,0},false,true,true,platform)); // center still on platform
	LedgeGuard retreat({90,0,0},56,{true,true,0});
	assert(retreat.Check({90,0,0},{-450,0,0},{-100,0,0},false,true,true,platform));
	LedgeGuard jumpDrift({90,0,0},56,{true,true,0});
	assert(!jumpDrift.Check({90,0,40},{-450,0,0},{100,0,200},true,false,true,platform));
	auto gap=[](V p,float,float){return Floor{true,p.x<20||p.x>80,0};};
	LedgeGuard acrossGap({0,0,0},10,{true,true,0});
	assert(acrossGap.Check({0,0,0},{100,0,0},{100,0,200},true,false,true,gap));
	assert(!acrossGap.Check({40,0,60},{100,0,0},{100,0,0},true,false,true,gap));
	assert(!acrossGap.Check({100,0,0},{100,0,0},{100,0,-200},true,true,true,gap)); // far-side landing cannot erase the gap
	LedgeGuard unknown({0,0,0},56,{});
	assert(!unknown.Check({0,0,0},{},{},false,true,true,platform));
	LedgeGuard floorTimeout({0,0,0},56,{true,true,0});
	assert(!floorTimeout.Check({0,0,0},{},{},false,true,true,[](V,float,float){return Floor{};}));
	LedgeGuard fastGap({0,0,0},10,{true,true,0});
	assert(!fastGap.Check({100,0,60},{1000,0,0},{1000,0,0},true,false,true,gap));
	// Already airborne over a pit may still steer onto a platform to recover.
	LedgeGuard recover({40,0,60},10,{true,false,0});
	assert(recover.Check({40,0,60},{100,0,0},{100,0,0},true,false,true,gap));
	assert(recover.Check({100,0,0},{100,0,0},{100,0,-200},true,true,true,gap));
	// Compare exact geometry to an independent, high-precision convex search,
	// including segment endpoints, parallel axes and collapsed box dimensions.
	std::mt19937 random(73521);
	std::uniform_real_distribution<float> coordinate(-2000,2000),extent(0,100);
	for(int i=0;i<4000;++i)
	{
		V a={coordinate(random),coordinate(random),coordinate(random)};
		V b=i%7==0?a:V{coordinate(random),coordinate(random),coordinate(random)};
		if(i%11==0) b.x=a.x;
		V lo={coordinate(random),coordinate(random),coordinate(random)};
		V hi=lo+V{extent(random),extent(random),i%5==0?0:extent(random)};
		auto squared=[&](double t)
		{
			double value=0;
			const double start[]={a.x,a.y,a.z},end[]={b.x,b.y,b.z};
			const double low[]={lo.x,lo.y,lo.z},high[]={hi.x,hi.y,hi.z};
			for(int axis=0;axis<3;++axis)
			{
				double p=start[axis]+(end[axis]-start[axis])*t;
				double d=p-std::clamp(p,low[axis],high[axis]);value+=d*d;
			}
			return value;
		};
		double left=0,right=1;
		for(int step=0;step<100;++step)
		{
			double l=(2*left+right)/3,r=(left+2*right)/3;
			if(squared(l)<squared(r)) right=r;else left=l;
		}
		double reference=std::sqrt(std::min({squared(0),squared(1),squared((left+right)*.5)}));
		float fraction=0,value=SegmentDistance(a,b,lo,hi,fraction);
		assert(fraction>=0&&fraction<=1&&std::abs(value-reference)<.002);
		assert(std::abs(value-std::sqrt(squared(fraction)))<.002);
	}
	TimeBudget<FakeClock> budget;assert(budget.Exhausted());
	budget.Start(std::chrono::microseconds(2000));
	{
		auto time=budget.Measure();FakeClock::elapsed+=std::chrono::microseconds(700);
		assert(!budget.Exhausted());
	}
	FakeClock::elapsed+=std::chrono::seconds(1); // other features between calls
	assert(!budget.Exhausted());
	{
		auto time=budget.Measure();FakeClock::elapsed+=std::chrono::microseconds(1299);
		assert(!budget.Exhausted());FakeClock::elapsed+=std::chrono::microseconds(1);
		assert(budget.Exhausted());
	}
	assert(budget.Exhausted());budget.Start(std::chrono::microseconds(2000));assert(!budget.Exhausted());
	{
		auto time=budget.Measure();
		FakeClock::elapsed+=std::chrono::microseconds(500);
		assert(budget.Exhausted(std::chrono::microseconds(1500))&&!budget.Exhausted());
	}
	// Earliest danger entry has priority over raw distance or closest approach.
	float incomingTime=TimeToDanger({-1000,0,0},{2000,0,0},100,1);
	float nearbyTime=TimeToDanger({-150,0,0},{100,0,0},100,1);
	assert(std::abs(incomingTime-.45f)<.0001f&&incomingTime<nearbyTime);
	assert(TimeToDanger({0,0,0},{1,0,0},100,1)==0);
	assert(!std::isfinite(TimeToDanger({-150,0,0},{-100,0,0},100,1)));
	assert(!std::isfinite(TimeToDanger({-1000,101,0},{2000,0,0},100,1)));
	assert(!std::isfinite(TimeToDanger({-1000,0,0},{100,0,0},100,1)));
	assert(!std::isfinite(TimeToDanger({200,0,0},{0,0,0},100,1)));
	const std::vector<Body> still={Player(),Player()};
	// Continuous collision: neither endpoint is inside the player.
	assert(SegmentBox({-10000,0,40},{10000,0,40},{-25,-25,-1},{25,25,83}));
	auto incoming=Shot({-10000,0,40},{10000,0,40});
	auto direct=Rate(still,{incoming});
	assert(direct.complete&&direct.damage>=4&&NeedsDodge(direct));
	assert(!SegmentBox({-100,100,40},{100,100,40},{-25,-25,-1},{25,25,83}));
	assert(!SegmentBox({100,0,40},{200,0,40},{-25,-25,-1},{25,25,83}));
	// Player crosses a stationary/slow projectile between simulation ticks.
	assert(Rate({Player(-100),Player(100)},{Shot({0,0,40},{0,0,40})}).damage>=4);
	assert(Rate(still,{Shot({-100,100,40},{100,100,40})},0).damage==0);
	assert(!NeedsDodge(Rate(still,{Shot({-100,500,40},{100,500,40})})));
	// Proactive clearance starts before a direct hit is predicted.
	auto near=Rate(still,{Shot({-100,50,40},{100,50,40})});
	assert(near.damage==0&&near.proximity>0&&NeedsDodge(near));
	assert(Better(Rate(still,{Shot({-100,200,40},{100,200,40})}),near));
	// Both moving bodies must be considered; equal movement preserves separation.
	assert(Rate({Player(),Player(200)},{Shot({-100,0,40},{100,0,40})}).damage==0);
	// Direct hits use the union of transitioning hulls: crouch is not instant.
	assert(Rate({Player(),Player(0,0,0,62)},{Shot({-100,0,78},{100,0,78})}).damage>=4);
	assert(Rate({Player(0,0,0,62),Player(0,0,0,62)},{Shot({-100,0,78},{100,0,78})},0).damage==0);
	// Near-wall explosion and cover are separate from the direct projectile hull.
	auto explosion=Area({100,0,10},{100,0,10});
	assert(Rate(still,{explosion}).damage>0);
	auto shielded=Evaluate(still,std::vector<Threat>{explosion},48,.1f,Covered);
	assert(shielded.complete&&shielded.damage==0&&shielded.proximity==0);
	// An impact radius applies at the collision point, not along the rocket's flight.
	auto endBlast=Area({-500,0,40},{500,0,40},80);
	endBlast.steps[0].areaAtEnd=true;
	assert(Rate(still,{endBlast}).damage==0);
	endBlast.steps[0].to={80,0,40};
	assert(Rate(still,{endBlast}).damage>0);
	// A terminal collision produces one blast event, not a permanent hazard.
	Threat terminal=explosion;terminal.steps.push_back({});
	auto terminalRate=Rate({Player(),Player(),Player()},{terminal});
	assert(terminalRate.damage==Rate(still,{explosion}).damage);
	// Grenade bounce changes the next segment rather than tunnelling into a wall.
	V bounced=Bounce({100,0,-200},{0,0,1},.45f);
	assert(std::abs(bounced.x-45)<.001f&&std::abs(bounced.z-90)<.001f);
	assert(Bounce({100,0,0},{-1,0,0},.5f).x==-50);
	assert(Bounce({100,0,0},{1,0,0},.5f).x==100);
	Threat bouncedShot=Shot({-100,0,40},{0,0,40});
	bouncedShot.steps.push_back(Shot({0,0,40},{-100,0,40}).steps[0]);
	assert(Rate({Player(-50),Player(-50),Player(-50)},{bouncedShot}).damage>=4);
	// Newly planted stickies become hazards only at their arming time.
	assert(!Armed(10.79f,10,.8f)&&Armed(10.8f,10,.8f));
	assert(Armed(1,0,.8f)); // missing creation time: conservative
	Threat sticky=Area({0,0,0},{0,0,0});
	sticky.steps[0].area=Armed(10.1f,10,.8f);
	assert(!NeedsDodge(Rate(still,{sticky})));
	sticky.steps[0].area=true;
	sticky.steps.push_back(sticky.steps[0]);sticky.steps.push_back(sticky.steps[0]);
	// Every path begins inside the same zone: leaving promptly must beat freezing.
	auto freeze=Rate({Player(),Player(),Player(),Player()},{sticky});
	auto escape=Rate({Player(),Player(80),Player(160),Player(240)},{sticky});
	assert(escape.damage>0&&Better(escape,freeze));
	// A second threat can invalidate an otherwise good escape direction.
	auto right=Rate({Player(),Player(120)},{incoming,Shot({100,-150,40},{100,150,40})});
	auto left=Rate({Player(),Player(-120)},{incoming,Shot({100,-150,40},{100,150,40})});
	assert(Better(left,right));
	// When damage is unavoidable, choose lower exposure, then reserve and input.
	Score worse{2,0,0,true,false},less{1,1,0,true,false};
	assert(Better(less,worse));
	Score straight{0,.2f,0,true,false},wide{0,.1f,1,true,false};
	assert(Better(wide,straight));
	Score previous=straight;previous.previous=true;previous.proximity+=.005f;
	assert(Better(previous,straight)&&!Better(straight,previous));
	Score quiet{0,0,0,true,false};
	assert(!NeedsDodge(quiet));
	Score failed;failed.damage=-100;
	assert(!Better(failed,quiet));
	// Exhausted visibility budgets and incomplete paths never authorize a move.
	auto exhausted=Evaluate(still,std::vector<Threat>{explosion},48,.1f,[](size_t,size_t,V,V,V){return -1;});
	assert(!exhausted.complete&&!Better(exhausted,direct));
	assert(!Rate({Player()},{}).complete);
	assert(!Rate(still,{Threat{}}).complete);
	// Direct-only evaluation also observes the deadline; no visibility callback
	// is needed to interrupt a CPU-heavy forecast.
	std::vector<Body> longPath(65,Player());Threat longThreat;
	longThreat.steps.resize(64,incoming.steps[0]);int checks=0;
	auto interrupted=Evaluate(longPath,std::vector<Threat>{longThreat},48,.015f,Open,[&]{return ++checks<3;});
	assert(!interrupted.complete&&checks==3&&!Better(interrupted,direct));
	auto corrupt=still;corrupt[0].origin.x=std::numeric_limits<float>::quiet_NaN();
	assert(!Rate(corrupt,{incoming}).complete);
	// Single outgoing latency plus age of netvars; no additional interpolation.
	assert(std::abs(AdvanceTime(10,9.9f,.05f,.03f)-.18f)<.001f);
	assert(AdvanceTime(10,11,0,0)==0);
	assert(AdvanceTime(20,0,5,5)==1);
	assert(Setting(std::numeric_limits<float>::quiet_NaN(),.8f,.2f,1.5f)==.8f);
	assert(Setting(2,.8f,.2f,1.5f)==1.5f);
	// Aimbot/anti-aim yaw and inverted pitch must preserve the world direction.
	const V wish=WorldWish(0,450,0);
	for(float yaw=-180;yaw<=180;yaw+=15)
		for(bool inverted:{false,true})
		{
			V axes=MovementAxes(yaw,wish,inverted);
			V roundtrip=WorldWish(yaw,axes.x,axes.y,inverted);
			assert(std::abs(roundtrip.x-wish.x)<.001f&&std::abs(roundtrip.y-wish.y)<.001f);
		}
	assert(WorldWish(0,0,450).y==-450);
	std::cout<<"auto dodge policy tests passed\n";
}
