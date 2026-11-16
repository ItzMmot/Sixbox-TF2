#include "AutoDodge.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../Backtrack/Backtrack.h"
#include "../../Ticks/Ticks.h"
#include "../../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../../Aimbot/AutoTrolldier/AutoTrolldier.h"
#include "../../Aimbot/SpyKnife/SpyKnife.h"
#include "../../Aimbot/AimbotProjectile/AimbotProjectile.h"
#include <cstring>

namespace
{
	constexpr int MaxMoves=2048, MaxTraces=4096, MaxThreats=32;
	constexpr int TerrainReserve=256, MaxTerrainTraces=768;
	constexpr int MoveButtons=IN_JUMP|IN_DUCK|IN_FORWARD|IN_BACK|IN_MOVELEFT|IN_MOVERIGHT;
	Dodge::V Point(const Vec3& v) { return {v.x,v.y,v.z}; }
	Vec3 Native(Dodge::V v) { return {v.x,v.y,v.z}; }
	bool Finite(const Vec3& v) { return Dodge::Finite(Point(v)); }
	Vec3 WorldWish(const CUserCmd& cmd)
	{
		return Native(Dodge::WorldWish(cmd.viewangles.y,cmd.forwardmove,cmd.sidemove,std::abs(Math::NormalizeAngle(cmd.viewangles.x))>90));
	}
	void SetWish(CUserCmd& cmd,const Vec3& wish,int buttons)
	{
		Dodge::V axes=Dodge::MovementAxes(cmd.viewangles.y,Point(wish),std::abs(Math::NormalizeAngle(cmd.viewangles.x))>90);
		cmd.forwardmove=axes.x;cmd.sidemove=axes.y;
		cmd.buttons=(cmd.buttons&~MoveButtons)|buttons;
		if(cmd.forwardmove>1) cmd.buttons|=IN_FORWARD;
		if(cmd.forwardmove<-1) cmd.buttons|=IN_BACK;
		if(cmd.sidemove>1) cmd.buttons|=IN_MOVERIGHT;
		if(cmd.sidemove<-1) cmd.buttons|=IN_MOVELEFT;
	}
	enum class Kind { Ignore,Impact,Grenade,Sticky,Stop,Pierce };
	Kind Classify(CBaseEntity* entity)
	{
		switch(entity->GetClassID())
		{
		case ETFClassID::CTFProjectile_Rocket: case ETFClassID::CTFProjectile_SentryRocket:
		case ETFClassID::CTFBaseRocket: case ETFClassID::CTFFlameRocket:
		case ETFClassID::CTFProjectile_EnergyBall: case ETFClassID::CTFProjectile_Flare:
		case ETFClassID::CTFProjectile_Jar: case ETFClassID::CTFProjectile_JarMilk: case ETFClassID::CTFProjectile_JarGas:
			return Kind::Impact;
		case ETFClassID::CTFGrenadePipebombProjectile:
			switch(entity->As<CTFGrenadePipebombProjectile>()->m_iType())
			{
			case TF_GL_MODE_REMOTE_DETONATE: return Kind::Sticky;
			case TF_GL_MODE_REGULAR: case TF_GL_MODE_CANNONBALL: return Kind::Grenade;
			default: return Kind::Ignore; // sticky jumper / practice projectiles
			}
		case ETFClassID::CBaseGrenade: case ETFClassID::CTFWeaponBaseGrenadeProj:
			return Kind::Grenade;
		case ETFClassID::CTFProjectile_Arrow: case ETFClassID::CTFProjectile_HealingBolt:
		case ETFClassID::CTFProjectile_Cleaver: case ETFClassID::CTFStunBall: case ETFClassID::CTFBall_Ornament:
			return Kind::Stop;
		case ETFClassID::CTFProjectile_EnergyRing: case ETFClassID::CTFProjectile_BallOfFire:
		case ETFClassID::CTFProjectile_MechanicalArmOrb:
			return Kind::Pierce;
		default: return Kind::Ignore;
		}
	}
	class ProjectileFilter : public CTraceFilterCollideable
	{
		CBaseEntity* m_local; CBaseEntity* m_projectile;
	public:
		ProjectileFilter(CTFPlayer* local,CBaseEntity* projectile):m_local(local),m_projectile(projectile)
		{
			m_iTeam=projectile->m_iTeamNum();m_iType=SKIP_CHECK;m_iObject=OBJECT_DEFAULT;
		}
		bool ShouldHitEntity(IHandleEntity* entity,int mask) override
		{
			if(entity==m_local||entity==m_projectile) return false;
			return CTraceFilterCollideable::ShouldHitEntity(entity,mask);
		}
	};
	struct Entry { CBaseEntity* entity;Kind kind;float urgency,clearance,gravity;Vec3 velocity; };
	float DetonateTime(CBaseEntity* entity)
	{
		// Use reflected client state only. Many TF2 grenades do not expose a
		// fuse to the client; never infer a fresh full fuse from first sighting.
		int depth=0;
		for(auto* map=entity->GetPredDescMap();map&&depth++<16;map=map->baseMap)
			for(int i=0;i<map->dataNumFields;++i)
			{
				const auto& field=map->dataDesc[i];
				if(!field.fieldName||std::strcmp(field.fieldName,"m_flDetonateTime")) continue;
				if((field.fieldType!=FIELD_FLOAT&&field.fieldType!=FIELD_TIME)||field.fieldOffset[TD_OFFSET_NORMAL]<=0) continue;
				float time=0;std::memcpy(&time,reinterpret_cast<const byte*>(entity)+field.fieldOffset[TD_OFFSET_NORMAL],sizeof(time));
				if(std::isfinite(time)&&time>0) return time;
			}
		return 0;
	}
}

void CAutoDodge::Reset()
{
	m_owner.Term();m_spawn=m_command=m_control=m_previousCommand=-1;
	m_wish=m_previousWish={};m_buttons=m_previousButtons=0;
	m_escapeWish={};
	m_moves=m_traces=m_terrainTraces=m_ticks=m_leadTicks=0;m_threats.clear();
	m_checkedInput=m_minimumUsed=m_lateMinimumUsed=false;
}

void CAutoDodge::Unload()
{
	Reset();
	if(m_sim.m_pEnv) I::Physics->DestroyEnvironment(m_sim.m_pEnv);
	if(m_sim.m_pBoxCollide) I::PhysicsCollision->DestroyCollide(m_sim.m_pBoxCollide);
	m_sim.m_pBoxCollide=nullptr;
	m_sim.m_pEnv=nullptr;m_sim.m_pObj=m_sim.m_pObjBox=nullptr;m_sim.m_pCurrent=nullptr;
}

bool CAutoDodge::Eligible(CTFPlayer* local,const CUserCmd* command) const
{
	return Vars::Misc::Movement::AutoDodge::Enabled.Value && local && command && command->command_number>0
		&& Finite(command->viewangles) && std::isfinite(command->forwardmove) && std::isfinite(command->sidemove)
		&& Finite(local->m_vecOrigin()) && Finite(local->m_vecVelocity())
		&& local->IsAlive() && !local->IsAGhost() && !local->IsSwimming() && !local->IsTaunting()
		&& local->m_MoveType()==MOVETYPE_WALK && !local->InCond(TF_COND_HALLOWEEN_KART)
		&& !local->InCond(TF_COND_SHIELD_CHARGE) && !local->InCond(TF_COND_STUNNED)
		&& !local->m_hGrapplingHookTarget().Get() && local->m_flWaterJumpTime()<=0
		&& !I::ClientState->IsPaused() && !I::EngineClient->IsPlayingDemo() && !I::EngineVGui->IsGameUIVisible()
		&& !F::Ticks.m_bShifting && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap
		&& !F::Ticks.m_bRecharge && !F::Ticks.m_bAntiWarp && !G::Unload;
}

bool CAutoDodge::Gather(CTFPlayer* local)
{
	const float horizon=Dodge::Setting(Vars::Misc::Movement::AutoDodge::PredictionTime.Value,.8f,.2f,1.5f);
	const float margin=Dodge::Setting(Vars::Misc::Movement::AutoDodge::SafetyMargin.Value,48.f,0.f,128.f);
	m_ticks=std::max(1,TIME_TO_TICKS(horizon));m_threats.clear();
	std::vector<Entry> entries;
	const Vec3 center=local->m_vecOrigin()+(local->m_vecMins()+local->m_vecMaxs())*.5f;
	const float now=TICKS_TO_TIME(F::Backtrack.m_iTickCount);
	const float outgoing=F::Backtrack.GetReal(FLOW_OUTGOING);
	const float queued=TICKS_TO_TIME(std::max(F::Backtrack.GetAnticipatedChoke(),0));
	m_leadTicks=TIME_TO_TICKS(std::clamp(outgoing+queued,0.f,1.f));
	for(auto* entity:H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		// Census all projectiles before spending time on native physics: the
		// last entity in the group may be the next one to hit us.
		if(!entity||entity->IsDormant()||entity->m_iTeamNum()<TF_TEAM_RED||entity->m_iTeamNum()==local->m_iTeamNum()) continue;
		Kind kind=Classify(entity);if(kind==Kind::Ignore) continue;
		if(kind==Kind::Stop&&entity->m_MoveType()==MOVETYPE_NONE) continue;
		if(kind==Kind::Sticky && entity->As<CTFGrenadePipebombProjectile>()->m_bTouched()
			&& !Vars::Misc::Movement::AutoDodge::AvoidStickies.Value) continue;
		// Current projectile team is authoritative after reflection. Its original
		// launcher/owner can still belong to the local player.
		auto owners=m_sim.GetEntities(entity);auto* weapon=owners.first;
		if(weapon && SDK::AttribHookValue(1.f,"mult_dmg",weapon)<=0) continue;
		Vec3 velocity=m_sim.GetVelocity(entity),delta=entity->m_vecOrigin()-center;
		if(!Finite(velocity)||!Finite(delta)) continue;
		const float gravity=m_sim.GetGravity(entity,weapon);
		const float age=Dodge::AdvanceTime(now,entity->m_flSimulationTime(),outgoing,queued);
		Vec3 aged=delta+velocity*age-Vec3(0,0,.5f*gravity*age*age);
		Vec3 relative=velocity-local->m_vecVelocity()-Vec3(0,0,gravity*age);
		float t=std::clamp(-aged.Dot(relative)/std::max(relative.LengthSqr(),1.f),0.f,horizon);
		float distance=(aged+relative*t).Length();
		// Include gravity's displacement and the player's reachable envelope.
		float radius=F::AimbotProjectile.GetSplashRadius(entity,weapon,owners.second);
		if(kind==Kind::Grenade||kind==Kind::Sticky)
		{
			float networkRadius=entity->As<CBaseGrenade>()->m_DmgRadius();
			radius=std::isfinite(networkRadius)&&networkRadius>0?networkRadius:std::max(radius,TF_ROCKET_RADIUS);
		}
		float range=256+radius+margin+SDK::MaxSpeed(local)*horizon+.5f*gravity*horizon*horizon;
		if(distance>range) continue;
		const float danger=radius+margin+(local->m_vecMaxs()-local->m_vecMins()).Length()*.5f+4;
		float urgency=Dodge::TimeToDanger(Point(aged),Point(relative),danger,horizon);
		// Curved falling trajectories can enter the danger sphere even when
		// the initial velocity ray misses it.
		for(int sample=1;sample<=8&&gravity>0;++sample)
		{
			float time=horizon*sample/8;
			if((aged+relative*time-Vec3(0,0,.5f*gravity*time*time)).Length()<danger)
				{urgency=std::min(urgency,time);break;}
		}
		if(kind==Kind::Sticky)
		{
			auto* pipe=entity->As<CTFGrenadePipebombProjectile>();
			static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime");
			float arm=live?live->GetFloat():.8f;
			if(weapon) arm=SDK::AttribHookValue(arm,"sticky_arm_time",weapon);
			if(pipe->m_flCreationTime()>0) urgency=std::max(urgency,pipe->m_flCreationTime()+arm-now-outgoing-queued);
		}
		else if(kind==Kind::Grenade)
		{
			float detonate=DetonateTime(entity);
			if(detonate>0&&distance<danger) urgency=std::min(urgency,std::max(0.f,detonate-now-outgoing-queued));
		}
		entries.push_back({entity,kind,urgency,distance-danger,gravity,velocity});
	}
	std::stable_sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b)
	{
		return a.urgency!=b.urgency?a.urgency<b.urgency:a.clearance<b.clearance;
	});
	if(entries.size()>MaxThreats) entries.resize(MaxThreats);
	auto forecast=[&](const Entry& entry,bool exact,Dodge::Threat& threat)
	{
		auto* entity=entry.entity;const Kind kind=entry.kind;
		ProjectileInfo info{};m_sim.GetInfo(entity,info);
		if(!Finite(info.m_vPos)||!Finite(info.m_vHull)) return false;
		info.m_vHull={std::max(std::abs(info.m_vHull.x),1.f),std::max(std::abs(info.m_vHull.y),1.f),std::max(std::abs(info.m_vHull.z),1.f)};
		float radius=F::AimbotProjectile.GetSplashRadius(entity,info.m_pWeapon,info.m_pOwner);
		float armTime=0,created=0;
		const float detonate=entry.kind==Kind::Grenade?DetonateTime(entity):0;
		if(kind==Kind::Grenade||kind==Kind::Sticky)
		{
			float networkRadius=entity->As<CBaseGrenade>()->m_DmgRadius();
			if(std::isfinite(networkRadius)&&networkRadius>0) radius=networkRadius;
			else if(radius<=0) radius=TF_ROCKET_RADIUS;
		}
		if(kind==Kind::Sticky)
		{
			auto* pipe=entity->As<CTFGrenadePipebombProjectile>();created=pipe->m_flCreationTime();
			static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime");
			armTime=live?live->GetFloat():.8f;
			if(info.m_pWeapon) armTime=SDK::AttribHookValue(armTime,"sticky_arm_time",info.m_pWeapon);
		}
		const int advance=TIME_TO_TICKS(Dodge::AdvanceTime(now,entity->m_flSimulationTime(),outgoing,queued));
		threat.steps.reserve(m_ticks);
		const bool pipe=entity->GetClassID()==ETFClassID::CTFGrenadePipebombProjectile;
		bool touched=pipe&&entity->As<CTFGrenadePipebombProjectile>()->m_bTouched();
		const bool cannon=pipe&&entity->As<CTFGrenadePipebombProjectile>()->m_iType()==TF_GL_MODE_CANNONBALL;
		// Zero velocity alone also occurs at the apex of a vertical throw.
		bool stationary=(kind==Kind::Sticky&&touched)||(kind==Kind::Grenade&&touched&&info.m_flVelocity<1.f);
		if(!exact)
		{
			// Complete conservative coverage for every selected threat before
			// optional collision refinement. Unknown impacts may occur anywhere
			// along the flight prefix; bouncing/attaching pipes use a reach bound.
			threat.conservative=!stationary;
			Vec3 start=info.m_vPos;
			for(int tick=0;tick<m_ticks;++tick)
			{
				float elapsed=TICKS_TO_TIME(advance+tick+1);
				float time=now+outgoing+queued+TICKS_TO_TIME(tick+1);
				Dodge::Step step{};step.from=Point(start);step.hull=Point(info.m_vHull);step.radius=radius;
				step.to=stationary?step.from:Point(start+entry.velocity*elapsed-Vec3(0,0,.5f*entry.gravity*elapsed*(elapsed+TICK_INTERVAL)));
				step.direct=kind!=Kind::Sticky&&!(kind==Kind::Grenade&&touched&&!cannon);
				step.area=(kind==Kind::Impact&&radius>0)||(kind==Kind::Grenade&&(detonate<=0||time>=detonate))
					||(kind==Kind::Sticky&&Dodge::Armed(time,created,armTime));
				if(!stationary&&(kind==Kind::Grenade||kind==Kind::Sticky))
				{
					float reach=entry.velocity.Length()*elapsed+.5f*entry.gravity*elapsed*elapsed;
					step.to=step.from;step.radius+=reach;
					step.hull=step.hull+Dodge::V{reach,reach,reach};
				}
				else if(!stationary&&entry.gravity>0)
				{
					step.hull.z+=entry.gravity*elapsed*elapsed*.125f;
				}
				threat.steps.push_back(step);
			}
			return true;
		}
		bool stopped=false;
		Vec3 position=info.m_vPos;
		if(!stationary && !m_sim.Initialize(info,false,true)) return false;
		ProjectileFilter filter(local,entity);int mask=MASK_SOLID;
		m_sim.SetupTrace(filter,mask,entity);
		bool complete=true;
		for(int tick=-advance;tick<m_ticks;++tick)
		{
			if(m_budget.Exhausted(m_movementReserve)) {complete=false;break;}
			Dodge::Step step{};step.from=step.to=Point(position);step.hull=Point(info.m_vHull);
			const float time=now+outgoing+queued+TICKS_TO_TIME(tick+1);
			step.radius=radius;
			step.area=((kind==Kind::Grenade&&detonate<=0) || (kind==Kind::Sticky&&Dodge::Armed(time,created,armTime)))&&!stopped;
			if(!stationary&&!stopped)
			{
				if(m_traces>=MaxTraces-TerrainReserve) {complete=false;break;}
				m_sim.RunTick(info,false);Vec3 next=m_sim.GetOrigin();
				if(!Finite(next)||m_budget.Exhausted(m_movementReserve)) {complete=false;break;}
				CGameTrace trace{};++m_traces;
				SDK::TraceHull(position,next,-info.m_vHull,info.m_vHull,mask,&filter,&trace);
				step.direct=kind!=Kind::Sticky&&!(kind==Kind::Grenade&&touched&&!cannon);
				position=trace.endpos;step.to=Point(position);
				if(trace.DidHit())
				{
					if(kind==Kind::Sticky) {stationary=true;position+=trace.plane.normal;}
					else if(kind==Kind::Grenade)
					{
						// World/prop contact bounces; contact with a hostile actor
						// detonates live pipes. A touched pipe no longer direct-hits.
						bool actor=trace.m_pEnt&&(trace.m_pEnt->IsPlayer()||trace.m_pEnt->IsBaseObject());
						if(actor&&cannon) {position=next;step.to=Point(next);}
						else if(actor&&!touched) {stopped=true;step.area=true;step.areaAtEnd=true;}
						else
						{
							touched=true;
							Vec3 velocity=Native(Dodge::Bounce(Point(m_sim.GetVelocity()),Point(trace.plane.normal),entity->m_flElasticity()));
							position+=trace.plane.normal;
							stationary=trace.startsolid||velocity.LengthSqr()<400;
							m_sim.SetState(position,velocity);
						}
					}
					else
					{
						// Piercing projectiles pass actors but stop at world geometry.
						bool actor=trace.m_pEnt&&(trace.m_pEnt->IsPlayer()||trace.m_pEnt->IsBaseObject());
						if(kind==Kind::Pierce&&actor) {position=next;step.to=Point(next);}
						else {stopped=true;step.area=kind==Kind::Impact&&radius>0;step.areaAtEnd=true;}
					}
				}
			}
			if(kind==Kind::Grenade&&detonate>0&&time>=detonate&&!stopped)
			{
				step.area=true;step.areaAtEnd=true;stopped=true;
			}
			if(tick>=0) threat.steps.push_back(step);
		}
		m_sim.m_pCurrent=nullptr;
		return complete;
	};
	std::vector<Entry> valid;
	for(const auto& entry:entries)
	{
		Dodge::Threat coarse;
		if(forecast(entry,false,coarse)) {m_threats.push_back(std::move(coarse));valid.push_back(entry);}
	}
	for(size_t i=0;i<valid.size();++i)
	{
		if(m_budget.Exhausted(m_movementReserve)) break;
		Dodge::Threat exact;
		if(!forecast(valid[i],true,exact)) break;
		m_threats[i]=std::move(exact);
	}
	if(!valid.empty())
	{
		const auto& urgent=valid.front();
		Vec3 away=center-urgent.entity->m_vecOrigin();away.z=0;
		if(urgent.kind!=Kind::Sticky&&urgent.velocity.Length2DSqr()>1)
		{
			Vec3 side{-urgent.velocity.y,urgent.velocity.x,0};
			if(side.Dot(away)<0) side=-side;
			away=side;
		}
		if(away.Length2DSqr()<1) away={1,0,0};
		m_escapeWish=away*(450.f/away.Length2D());
	}
	return true;
}

void CAutoDodge::Prepare(CTFPlayer* local,CUserCmd* command,bool predictionBase)
{
	if(!Eligible(local,command)) {Reset();return;}
	if(m_owner.Get()!=local||m_spawn!=local->m_iSpawnCounter()||command->command_number<m_command) Reset();
	if(Controls(command)) {ApplyMovement(command);return;}
	const bool fresh=m_command!=command->command_number;
	if(fresh)
	{
		m_previousWish=m_wish;m_previousButtons=m_buttons;m_previousCommand=m_control;
		m_control=-1;m_command=command->command_number;m_owner.Set(local);m_spawn=local->m_iSpawnCounter();
		m_moves=m_traces=m_terrainTraces=0;
		const auto duration=std::chrono::microseconds(int(Dodge::Setting(Vars::Misc::Movement::AutoDodge::CalculationBudget.Value,4.f,1.f,16.f)*1000));
		m_checkedInput=m_minimumUsed=m_lateMinimumUsed=false;m_budget.Start(duration);
		m_movementReserve=duration*3/4;
	}
	auto time=m_budget.Measure();
	if(fresh && !Gather(local)) {m_threats.clear();return;}
	if(m_threats.empty()||m_moves+m_ticks+m_leadTicks>MaxMoves) return;
	const Vec3 desired=WorldWish(*command);
	const int originalButtons=command->buttons&(IN_JUMP|IN_DUCK);
	// Aimbot angle changes that preserve world input do not require a second
	// local forecast. A genuinely different late helper input still does.
	if(m_checkedInput&&(desired-m_checkedWish).LengthSqr()<.0001f
		&&originalButtons==m_checkedButtons&&command->upmove==m_checkedUpmove) return;
	const bool minimum=!m_minimumUsed||(predictionBase&&m_checkedInput&&!m_lateMinimumUsed);
	if(!minimum&&m_budget.Exhausted()) return;
	CScopedLocalMovement simulation(local,predictionBase);
	if(!simulation.Valid()) return;
	m_minimumUsed=true;
	if(predictionBase) m_lateMinimumUsed=true;
	// Apply latency preroll once, then validate all routes from that same state.
	const CUserCmd& previous=G::LastUserCmd?*G::LastUserCmd:*command;
	for(int tick=0;tick<m_leadTicks;++tick)
	{
		if(!minimum&&m_budget.Exhausted()) return;
		++m_moves;
		if(!simulation.Step(previous)) return;
	}
	if(m_leadTicks) simulation.Checkpoint();
	const bool grounded=local->IsOnGround();
	auto floor=[&](Dodge::V point,float reference,float depth)
	{
		Dodge::Floor result;
		if(m_traces>=MaxTraces||m_terrainTraces>=MaxTerrainTraces) return result;
		CTraceFilterWorldAndPropsOnly filter(local);CGameTrace trace{};
		++m_traces;++m_terrainTraces;
		SDK::Trace({point.x,point.y,reference+18},{point.x,point.y,reference-depth},local->SolidMask(),&filter,&trace);
		result.complete=true;
		result.supported=trace.DidHit()&&!trace.startsolid&&trace.plane.normal.z>=.707f&&Finite(trace.endpos);
		result.height=trace.endpos.z;
		return result;
	};
	auto feet=[&]{return Point(local->m_vecOrigin()+Vec3(0,0,local->m_vecMins().z));};
	const Dodge::V startFeet=feet();
	const float halfX=std::max(std::abs(local->m_vecMins().x),std::abs(local->m_vecMaxs().x));
	const float halfY=std::max(std::abs(local->m_vecMins().y),std::abs(local->m_vecMaxs().y));
	const float front=std::sqrt(halfX*halfX+halfY*halfY)+32.f;
	const Dodge::Floor initialFloor=floor(startFeet,startFeet.z,grounded?64.f:256.f);
	const Dodge::LedgeGuard initialGuard(startFeet,front,initialFloor);
	bool nearLedge=!initialFloor.complete||!initialFloor.supported;
	if(grounded&&!nearLedge&&Vars::Misc::Movement::AutoDodge::Jump.Value)
	{
		// No automatic launch near an edge, including sideways momentum and
		// a jump in place that could carry the player out over the pit.
		for(int i=0;i<8;++i)
		{
			float angle=float(i)*3.14159265359f/4;
			auto support=floor(startFeet+Dodge::V{std::cos(angle)*front,std::sin(angle)*front,0},initialFloor.height,64);
			if(!support.complete||!support.supported) {nearLedge=true;break;}
		}
	}
	const float margin=Dodge::Setting(Vars::Misc::Movement::AutoDodge::SafetyMargin.Value,48.f,0.f,128.f);
	struct Candidate { Vec3 wish;int buttons;bool original;float deviation=0;bool previous=false; };
	std::vector<Candidate> candidates={{desired,originalButtons,true}};
	std::vector<Vec3> directions={desired,{}};
	directions.push_back(m_escapeWish);
	directions.push_back(-m_escapeWish);
	for(int i=0;i<8;++i) {float a=float(i)*3.14159265359f/4;directions.push_back({std::cos(a)*450,std::sin(a)*450,0});}
	const bool jumping=Vars::Misc::Movement::AutoDodge::Jump.Value&&grounded;
	const bool ducking=Vars::Misc::Movement::AutoDodge::Duck.Value;
	for(int variant=0;variant<4;++variant)
	{
		if((variant&1)&&!jumping) continue;
		if((variant&1)&&nearLedge) continue;
		if((variant&2)&&!ducking) continue;
		for(const Vec3& direction:directions)
		{
			// Disabled actions preserve manual input. An automatic jump is a
			// single press on the first grounded tick, never an airborne jump.
			int buttons=originalButtons;
			if(jumping) buttons=(buttons&~IN_JUMP)|((variant&1)?IN_JUMP:0);
			if(ducking) buttons=(buttons&~IN_DUCK)|((variant&2)?IN_DUCK:0);
			candidates.push_back({direction,buttons,false});
		}
	}
	// The held direction may not coincide with the eight compass directions.
	if(m_previousCommand==command->command_number-1
		&&(jumping||((m_previousButtons^originalButtons)&IN_JUMP)==0)
		&&(!(m_previousButtons&IN_JUMP)||!nearLedge)
		&&(ducking||((m_previousButtons^originalButtons)&IN_DUCK)==0))
		candidates.push_back({m_previousWish,m_previousButtons,false});
	for(auto& candidate:candidates)
	{
		candidate.deviation=(candidate.wish-desired).Length2D()/450.f
			+((candidate.buttons^originalButtons)&IN_JUMP?.15f:0)+((candidate.buttons^originalButtons)&IN_DUCK?.1f:0);
		candidate.previous=m_previousCommand==command->command_number-1
			&&(candidate.wish-m_previousWish).Length2DSqr()<1&&candidate.buttons==m_previousButtons;
	}
	std::stable_sort(candidates.begin()+1,candidates.end(),[&](const Candidate& a,const Candidate& b)
	{
		if(a.previous!=b.previous) return a.previous>b.previous;
		// Reserve the first alternative for an actual escape direction, rather
		// than spending the minimum comparison on crouching in the same place.
		auto rank=[&](const Candidate& c)
		{
			if(c.buttons!=originalButtons) return 2;
			if((c.wish-m_escapeWish).Length2DSqr()<1) return 0;
			if((c.wish+m_escapeWish).Length2DSqr()<1) return 1;
			if(c.wish.Length2DSqr()<1) return 2;
			return 3;
		};
		int rankA=rank(a),rankB=rank(b);
		if(rankA!=rankB) return rankA<rankB;
		return a.deviation<b.deviation;
	});
	for(size_t i=1;i<candidates.size();)
	{
		bool duplicate=false;
		for(size_t j=0;j<i;++j)
			if(candidates[i].buttons==candidates[j].buttons&&(candidates[i].wish-candidates[j].wish).Length2DSqr()<1)
			{duplicate=true;break;}
		if(duplicate) candidates.erase(candidates.begin()+i);else ++i;
	}
	Dodge::Score baseline,best;Candidate chosen{};
	bool baselineUnsafe=false;
	std::vector<Dodge::Body> path;path.reserve(m_ticks+1);
	bool escapeSettled=false;int minimumAttempts=0;
	for(const auto& candidate:candidates)
	{
		const bool braking=candidate.wish.Length2DSqr()<1&&candidate.buttons==(originalButtons&~IN_JUMP);
		const bool essential=minimum&&(candidate.original||(!escapeSettled&&minimumAttempts<3)
			||(braking&&baseline.complete&&chosen.original));
		if(m_moves+m_ticks>MaxMoves||(!essential&&m_budget.Exhausted())) break;
		simulation.Rewind();
		path.clear();
		auto guard=initialGuard;
		const bool guardMomentum=(candidate.buttons&IN_JUMP)||!grounded||candidate.wish.Length2DSqr()>1;
		bool supported=guard.Check(feet(),Point(candidate.wish),Point(local->m_vecVelocity()),guardMomentum,local->IsOnGround(),true,floor);
		if(!candidate.original&&!supported) continue;
		if(!candidate.original&&essential) ++minimumAttempts;
		auto body=[&]{return Dodge::Body{Point(local->m_vecOrigin()),Point(local->m_vecMins()),Point(local->m_vecMaxs())};};
		path.push_back(body());
		bool valid=true,dangerousFall=!supported;float startZ=local->m_vecOrigin().z,lowestZ=startZ;
		CUserCmd probe=*command;
		for(int tick=0;tick<m_ticks;++tick)
		{
			if(!essential&&m_budget.Exhausted()) {valid=false;break;}
			int buttons=candidate.buttons;
			if(!candidate.original && tick>0) buttons&=~IN_JUMP;
			SetWish(probe,candidate.wish,buttons);
			Vec3 before=local->m_vecOrigin();float falling=local->m_vecVelocity().z;
			++m_moves;
			if(!simulation.Step(probe)) {valid=false;break;}
			path.push_back(body());lowestZ=std::min(lowestZ,local->m_vecOrigin().z);
			if(!guard.Check(feet(),Point(candidate.wish),Point(local->m_vecVelocity()),guardMomentum,local->IsOnGround(),tick==m_ticks-1,floor))
			{
				dangerousFall=true;
				if(!candidate.original) {valid=false;break;}
			}
			if((falling<-580&&local->IsOnGround())||startZ-lowestZ>256)
			{
				dangerousFall=true;
				if(!candidate.original) {valid=false;break;}
			}
			// Reject blocked escape directions; the original route remains a
			// comparator even when it runs against a wall.
			if(!candidate.original&&tick>2&&local->IsOnGround()&&candidate.wish.Length2DSqr()>1
				&& (local->m_vecOrigin()-before).Length2DSqr()<.01f) {valid=false;break;}
		}
		if(!valid) {if(candidate.original) return;continue;}
		if(!essential&&m_budget.Exhausted()) break;
		if(!candidate.original)
		{
			// Verify landing and braking beyond the scoring horizon. A short
			// forecast must not end just before inertia carries us off the edge.
			int settled=0;
			for(int tick=0;tick<TIME_TO_TICKS(1.f);++tick)
			{
				if(m_moves>=MaxMoves||(!essential&&m_budget.Exhausted())) {valid=false;break;}
				float falling=local->m_vecVelocity().z;
				SetWish(probe,{},candidate.buttons&IN_DUCK);++m_moves;
				if(!simulation.Step(probe)||!guard.Check(feet(),{},Point(local->m_vecVelocity()),guardMomentum,local->IsOnGround(),true,floor))
					{valid=false;break;}
				if((falling<-580&&local->IsOnGround())||startZ-local->m_vecOrigin().z>256) {valid=false;break;}
				if(local->IsOnGround()&&local->m_vecVelocity().Length2DSqr()<36) ++settled;else settled=0;
				if(settled>=2) break;
			}
			if(!valid||settled<2) continue;
		}
		auto visible=[&](size_t threat,size_t,Dodge::V source,Dodge::V nearest,Dodge::V center)
		{
			if(m_threats[threat].conservative) return 1;
			CTraceFilterWorldAndPropsOnly filter(local);
			for(Dodge::V target:{nearest,center})
			{
				// Unknown cover stays exposed when the reserve is exhausted. Do
				// not turn an unfinished visibility query into a safe route.
				if(m_traces>=MaxTraces-TerrainReserve||m_budget.Exhausted()) return essential?1:-1;
				CGameTrace trace{};++m_traces;
				SDK::Trace(Native(source),Native(target),MASK_SOLID,&filter,&trace);
				if(!trace.DidHit()) return 1;
			}
			return 0;
		};
		Dodge::Score score=Dodge::Evaluate(path,m_threats,margin,TICK_INTERVAL,visible,[&]{return essential||!m_budget.Exhausted();});
		if(!score.complete) break;
		// Preserve manual input when no projectile threatens it. Terrain is
		// a hard constraint on the alternatives this controller can choose.
		if(candidate.original)
		{
			m_checkedWish=desired;m_checkedButtons=originalButtons;m_checkedUpmove=command->upmove;m_checkedInput=true;
			if(!Dodge::NeedsDodge(score)) return;
		}
		score.deviation=candidate.deviation;score.previous=candidate.previous;
		if(candidate.original)
		{
			baseline=best=score;chosen=candidate;
			baselineUnsafe=dangerousFall;
		}
		// Terrain safety is a hard constraint, including relative to an
		// unsafe original route under heavy fire; a fixed damage penalty is
		// insufficient when dozens of projectiles outweigh it.
		else if((chosen.original&&baselineUnsafe)||Dodge::Better(score,best)) {best=score;chosen=candidate;}
		if(!candidate.original&&candidate.buttons==originalButtons)
		{
			if((candidate.wish-m_escapeWish).Length2DSqr()<1
				&&((baselineUnsafe&&!chosen.original)||Dodge::Better(best,baseline))) escapeSettled=true;
			if((candidate.wish+m_escapeWish).Length2DSqr()<1) escapeSettled=true;
		}
		// In preference order, a fully clear route cannot be improved by
		// evaluating more movement or splash traces this command.
		if(!candidate.original&&score.damage==0&&score.proximity==0) break;
	}
	if(chosen.original||!baseline.complete||(!baselineUnsafe&&!Dodge::Better(best,baseline))) return;
	m_wish=chosen.wish;m_buttons=chosen.buttons;m_control=command->command_number;
	F::AutoRocketJump.Reset();F::AutoTrolldier.Reset();F::SpyKnife.Reset();
	ApplyMovement(command);
}

void CAutoDodge::ApplyMovement(CUserCmd* command) const
{
	if(Controls(command)) SetWish(*command,m_wish,m_buttons);
}
