#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

// No engine types: geometry and ranking are exercised by native unit tests.
namespace Dodge
{
	// Share calculation time between early/late calls, excluding other helpers.
	// Native movement/traces have map- and machine-dependent costs.
	template<class Clock = std::chrono::steady_clock>
	class TimeBudget
	{
		typename Clock::duration m_remaining{};
		typename Clock::time_point m_started{};
		bool m_active=false;
	public:
		void Start(std::chrono::microseconds duration) { m_remaining=duration; }
		bool Exhausted(std::chrono::microseconds reserve={}) const
		{
			return m_remaining<=reserve||(m_active&&Clock::now()-m_started>=m_remaining-reserve);
		}
		class Scope
		{
			TimeBudget& m_budget;
		public:
			explicit Scope(TimeBudget& budget):m_budget(budget) { budget.m_started=Clock::now();budget.m_active=true; }
			~Scope() { m_budget.m_remaining-=Clock::now()-m_budget.m_started;m_budget.m_active=false; }
			Scope(const Scope&)=delete;
			Scope& operator=(const Scope&)=delete;
		};
		Scope Measure() { return Scope(*this); }
	};
	struct ContinueEvaluation { bool operator()() const { return true; } };
	struct V
	{
		float x = 0, y = 0, z = 0;
		V operator+(V b) const { return {x+b.x,y+b.y,z+b.z}; }
		V operator-(V b) const { return {x-b.x,y-b.y,z-b.z}; }
		V operator*(float s) const { return {x*s,y*s,z*s}; }
		float Dot(V b) const { return x*b.x+y*b.y+z*b.z; }
	};
	inline bool Finite(V v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
	inline float Setting(float value,float fallback,float low,float high) { return std::isfinite(value)?std::clamp(value,low,high):fallback; }
	inline V WorldWish(float yaw,float forward,float side,bool inverted=false)
	{
		float angle=yaw*.0174532925199433f,c=std::cos(angle),s=std::sin(angle);
		if(inverted) forward=-forward;
		return {c*forward+s*side,s*forward-c*side,0};
	}
	inline V MovementAxes(float yaw,V wish,bool inverted=false)
	{
		float angle=yaw*.0174532925199433f,c=std::cos(angle),s=std::sin(angle);
		float forward=c*wish.x+s*wish.y;
		return {inverted?-forward:forward,s*wish.x-c*wish.y,0};
	}
	inline V Min(V a,V b) { return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)}; }
	inline V Max(V a,V b) { return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)}; }
	inline V Clamp(V p,V lo,V hi) { return {std::clamp(p.x,lo.x,hi.x),std::clamp(p.y,lo.y,hi.y),std::clamp(p.z,lo.z,hi.z)}; }
	inline bool SegmentBox(V a,V b,V lo,V hi)
	{
		if (!Finite(a)||!Finite(b)||!Finite(lo)||!Finite(hi)) return false;
		float enter=0,leave=1;
		const float start[]={a.x,a.y,a.z},delta[]={b.x-a.x,b.y-a.y,b.z-a.z};
		const float low[]={lo.x,lo.y,lo.z},high[]={hi.x,hi.y,hi.z};
		for(int i=0;i<3;++i)
		{
			if(std::abs(delta[i])<1e-8f) { if(start[i]<low[i]||start[i]>high[i]) return false; continue; }
			float t0=(low[i]-start[i])/delta[i],t1=(high[i]-start[i])/delta[i];
			if(t0>t1) std::swap(t0,t1);
			enter=std::max(enter,t0);leave=std::min(leave,t1);
			if(enter>leave) return false;
		}
		return true;
	}
	// Exact segment/AABB distance. Between crossings of the six box planes,
	// squared distance is quadratic. Check its minimum in each interval instead
	// of repeatedly sampling it with a 20-iteration ternary search.
	inline float SegmentDistance(V a,V b,V lo,V hi,float& fraction)
	{
		const V delta=b-a;
		auto distance=[&](float t) { V p=a+delta*t,d=p-Clamp(p,lo,hi); return d.Dot(d); };
		fraction=0;
		float result=distance(0);
		if(delta.Dot(delta)<1e-12f) return std::sqrt(result);
		float cuts[8]={0,1};int count=2;
		const float start[]={a.x,a.y,a.z},velocity[]={delta.x,delta.y,delta.z};
		const float low[]={lo.x,lo.y,lo.z},high[]={hi.x,hi.y,hi.z};
		for(int axis=0;axis<3;++axis)
		{
			if(std::abs(velocity[axis])<1e-8f) continue;
			for(float plane:{low[axis],high[axis]})
			{
				float t=(plane-start[axis])/velocity[axis];
				if(t>0&&t<1) cuts[count++]=t;
			}
		}
		for(int i=1;i<count;++i)
		{
			float value=cuts[i];int j=i;
			while(j>0&&cuts[j-1]>value) {cuts[j]=cuts[j-1];--j;}
			cuts[j]=value;
		}
		for(int i=1;i<count;++i)
		{
			float middle=(cuts[i-1]+cuts[i])*.5f,numerator=0,denominator=0;
			for(int axis=0;axis<3;++axis)
			{
				float p=start[axis]+velocity[axis]*middle;
				if(p>=low[axis]&&p<=high[axis]) continue;
				float offset=start[axis]-(p<low[axis]?low[axis]:high[axis]);
				numerator+=offset*velocity[axis];denominator+=velocity[axis]*velocity[axis];
			}
			float t=denominator>0?std::clamp(-numerator/denominator,cuts[i-1],cuts[i]):middle;
			float value=distance(t);
			if(value<result) {fraction=t;result=value;}
		}
		return std::sqrt(result);
	}
	inline V Bounce(V velocity,V normal,float elasticity)
	{
		float dot=velocity.Dot(normal);
		return dot<0 ? (velocity-normal*(2*dot))*std::clamp(elasticity,0.f,.9f) : velocity;
	}
	inline float AdvanceTime(float serverNow,float simulationTime,float outgoing,float queued)
	{
		// Origins come from entity netvars, not rendered/interpolated positions.
		if(!std::isfinite(serverNow)||!std::isfinite(simulationTime)) return 0;
		return std::clamp(std::max(0.f,serverNow-simulationTime)+std::max(0.f,outgoing)+std::max(0.f,queued),0.f,1.f);
	}
	inline bool Armed(float now,float created,float delay) { return created<=0 || now>=created+std::max(0.f,delay); }
	// Earliest entry into the danger sphere, rather than distance + time in
	// mixed units. A distant fast incoming rocket can outrank a nearby miss.
	inline float TimeToDanger(V relativePosition,V relativeVelocity,float radius,float horizon)
	{
		float c=relativePosition.Dot(relativePosition)-radius*radius;
		if(c<=0) return 0;
		float a=relativeVelocity.Dot(relativeVelocity),b=relativePosition.Dot(relativeVelocity);
		if(a<1e-8f||b>=0) return std::numeric_limits<float>::infinity();
		float discriminant=b*b-a*c;
		if(discriminant<0) return std::numeric_limits<float>::infinity();
		float time=c/(-b+std::sqrt(discriminant));
		return time<=horizon?time:std::numeric_limits<float>::infinity();
	}
	struct Body { V origin,mins,maxs; };
	struct Floor
	{
		bool complete=false,supported=false;
		float height=0;
	};
	// Probe stable world/prop support at projected feet, not at the jump's
	// current height. A landing on the far side cannot hide a gap in between.
	class LedgeGuard
	{
		V m_last;
		float m_floor=0,m_front=56;
		bool m_anchored=false,m_valid=true;
	public:
		LedgeGuard(V feet,float front,Floor initial):m_last(feet),m_floor(initial.height),m_front(front),
			m_anchored(initial.supported),m_valid(initial.complete) {}
		template<class Probe>
		bool Check(V feet,V wish,V velocity,bool jumping,bool grounded,bool force,Probe probe)
		{
			if(!m_valid) return false;
			V travel=feet-m_last;travel.z=0;
			if(!force&&travel.Dot(travel)<144&&std::abs(feet.z-m_last.z)<18) return true;
			V start=m_last;m_last=feet;
			int samples=std::max(1,int(std::ceil(std::sqrt(travel.Dot(travel))/12.f)));
			if(samples>32) return m_valid=false;
			wish.z=velocity.z=0;
			for(int sample=1;sample<=samples;++sample)
			{
				V point=start+(feet-start)*(float(sample)/samples);
				bool onGround=grounded&&sample==samples;
				Floor center=probe(point,m_anchored?m_floor:point.z,m_anchored||onGround?64.f:256.f);
				if(!center.complete) return m_valid=false;
				if(!center.supported)
				{
					// Already airborne above a pit may recover onto a platform.
					if(m_anchored||onGround) return m_valid=false;
					continue;
				}
				m_anchored=true;m_floor=center.height;
				auto ahead=[&](V direction)
				{
					float speed=std::sqrt(direction.Dot(direction));
					if(speed<1) return true;
					Floor floor=probe(point+direction*(m_front/speed),m_floor,64.f);
					return floor.complete&&floor.supported;
				};
				if(!ahead(wish)||(jumping&&!ahead(velocity))) return m_valid=false;
			}
			return true;
		}
	};
	struct Step
	{
		V from,to,hull;
		float radius=0;
		bool direct=false,area=false,areaAtEnd=false;
	};
	struct Threat { std::vector<Step> steps;bool conservative=false; };
	struct Score
	{
		float damage=0,proximity=0,deviation=0;
		bool complete=false,previous=false;
	};
	// visible returns -1 on budget exhaustion, 0 for cover, 1 for exposure.
	template<class Visibility,class Continue=ContinueEvaluation>
	Score Evaluate(const std::vector<Body>& path,const std::vector<Threat>& threats,float margin,float dt,Visibility visible,Continue proceed={})
	{
		Score result;
		if(path.size()<2||!proceed()) return result;
		for(const auto& body:path) if(!Finite(body.origin)||!Finite(body.mins)||!Finite(body.maxs)) return result;
		for(size_t n=0;n<threats.size();++n)
		{
			const auto& threat=threats[n];
			if(threat.steps.size()+1!=path.size()) return result;
			float peak=0,exposure=0,nearPeak=0,nearTime=0;
			for(size_t i=0;i<threat.steps.size();++i)
			{
				if((i&15)==0&&!proceed()) return result;
				const auto& s=threat.steps[i];
				if(!s.direct&&!s.area) continue;
				const auto& a=path[i]; const auto& b=path[i+1];
				const V lo=Min(a.mins,b.mins),hi=Max(a.maxs,b.maxs);
				const V from=s.from-a.origin,to=s.to-b.origin;
				float damage=0,proximity=0;
				const V padding={margin,margin,margin};
				if(s.direct&&SegmentBox(from,to,lo-s.hull-padding,hi+s.hull+padding))
				{
					if(SegmentBox(from,to,lo-s.hull,hi+s.hull)) damage=4.f;
					if(margin>0)
					{
						float fraction=0;
						float directDistance=SegmentDistance(from,to,lo-s.hull,hi+s.hull,fraction);
						proximity=std::clamp(1-directDistance/margin,0.f,1.f);
					}
				}
				if(s.area && s.radius>0)
				{
					V blastStart=s.areaAtEnd?s.to:s.from;
					const V reach={s.radius+margin,s.radius+margin,s.radius+margin};
					float f=0;
					float distance=SegmentBox(blastStart-a.origin,s.to-b.origin,lo-reach,hi+reach)
						?SegmentDistance(blastStart-a.origin,s.to-b.origin,lo,hi,f):std::numeric_limits<float>::infinity();
					if(distance<s.radius+margin)
					{
						V source=blastStart+(s.to-blastStart)*f;
						V origin=a.origin+(b.origin-a.origin)*f;
						V nearest=origin+Clamp(source-origin,lo,hi);
						int exposed=visible(n,i,source,nearest,origin+(lo+hi)*.5f);
						if(exposed<0) return result;
						if(exposed)
						{
							if(distance<=s.radius) damage=std::max(damage,.5f+.5f*(1-distance/s.radius));
							if(margin>0) proximity=std::max(proximity,std::clamp(1-std::max(0.f,distance-s.radius)/margin,0.f,1.f));
						}
					}
				}
				peak=std::max(peak,damage);exposure+=damage*dt;
				nearPeak=std::max(nearPeak,proximity);nearTime+=proximity*dt;
			}
			result.damage+=peak+exposure;
			result.proximity+=nearPeak+nearTime;
		}
		result.complete=proceed();
		return result;
	}
	inline bool Better(const Score& a,const Score& b)
	{
		if(!a.complete) return false;
		if(!b.complete) return true;
		if(std::abs(a.damage-b.damage)>.01f) return a.damage<b.damage;
		if(std::abs(a.proximity-b.proximity)>.02f) return a.proximity<b.proximity;
		if(a.previous!=b.previous) return a.previous;
		return a.deviation+.01f<b.deviation;
	}
	inline bool NeedsDodge(const Score& score) { return score.complete&&(score.damage>.01f||score.proximity>.02f); }
}
