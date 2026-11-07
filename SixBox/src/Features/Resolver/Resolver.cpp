#include "Resolver.h"

#include "../Backtrack/Backtrack.h"
#include "../Players/PlayerUtils.h"
#include "../Ticks/Ticks.h"
#include "../Output/Output.h"

static CTFPlayer* GetPlayerForUserID(int iUserID)
{
	int iEntIndex = I::EngineClient->GetPlayerForUserID(iUserID);
	if (iEntIndex <= 0)
		return nullptr;

	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIndex);
	return pEntity ? pEntity->As<CTFPlayer>() : nullptr;
}

void CResolver::Reset()
{
	m_mResolverData.clear();
	m_mSniperDots.clear();

	m_shots.Clear();m_hShotOwner.Term();BeginCommand();
	for(auto& state:m_aAnimation) state={};
}

void CResolver::StoreSniperDots(CTFPlayerResource* pResource)
{
	m_mSniperDots.clear();
	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::SniperDots))
	{
		if (auto pOwner = pEntity->m_hOwnerEntity().Get())
		{
			int iUserID = pResource->m_iUserID(pOwner->entindex());
			m_mSniperDots[iUserID] = pEntity->m_vecOrigin();
		}
	}
}

std::optional<float> CResolver::GetPitchForSniperDot(CTFPlayer* pEntity, CTFPlayerResource* pResource)
{
	int iUserID = pResource->m_iUserID(pEntity->entindex());
	if (m_mSniperDots.contains(iUserID))
	{
		Vec3 vOrigin = m_mSniperDots[iUserID];
		Vec3 vEyeOrigin = pEntity->m_vecOrigin() + pEntity->GetViewOffset();
		Vec3 vAngles = Math::VectorAngles(vOrigin - vEyeOrigin);
		return vAngles.x;
	}

	return std::nullopt;
}

bool CResolver::UsesAnimationReplay(CTFPlayer* player) const
{
	return !G::Unload && player && I::EngineClient->IsInGame() && Vars::Resolver::AnimationHypotheses.Value
		&& (Vars::Resolver::Enabled.Value || Vars::Visuals::Removals::Interpolation.Value)
		&& !I::EngineClient->IsPlayingDemo() && player->entindex()!=I::EngineClient->GetLocalPlayer()
		&& player->IsAlive() && !player->IsDormant() && !player->IsAGhost() && !player->InCond(TF_COND_TAUNTING)
		&& player->m_PlayerAnimState() && player->m_nSequence()>=0;
}

void CResolver::DiscardAnimation(CTFPlayer* player)
{
	if (!player) return;
	const int index=player->entindex();
	if (index>0 && index<int(m_aAnimation.size()))
	{
		auto& state=m_aAnimation[index];const auto identity=state.identity;state={};
		if (identity.handle==player->As<IHandleEntity>()->GetRefEHandle().ToInt() && identity.spawn==player->m_iSpawnCounter())
		{state.identity=identity;state.player.Set(player);}
	}
}

AnimationTimeline::Identity CResolver::AnimationIdentity(CTFPlayer* player) const
{
	if (!player) return {};
	const int index=player->entindex();
	if (index<=0 || index>=int(m_aAnimation.size())) return {};
	const auto& state=m_aAnimation[index];
	return state.player.Get()==player && state.identity.handle==player->As<IHandleEntity>()->GetRefEHandle().ToInt()
		&& state.identity.spawn==player->m_iSpawnCounter() ? state.identity : AnimationTimeline::Identity{};
}

void CResolver::InvalidateIdentity(CTFPlayer* player)
{
	if (!player) return;
	const int index=player->entindex();
	if (index<=0 || index>=int(m_aAnimation.size())) return;
	auto& state=m_aAnimation[index];
	if (++m_iNextRevision==0) ++m_iNextRevision;
	state.identity.revision=m_iNextRevision;
	state.identity.handle=player->As<IHandleEntity>()->GetRefEHandle().ToInt();state.identity.spawn=player->m_iSpawnCounter();
	state.identity.hypothesis=state.active;state.player.Set(player);
}

bool CResolver::QueueAnimationEvent(CTFPlayer* player,PlayerAnimEvent_t event,int data)
{
	if (!UsesAnimationReplay(player) || G::UpdatingAnims) return false;
	const int index=player->entindex();
	if (index<=0 || index>=int(m_aAnimation.size()) || event<0 || event>=PLAYERANIMEVENT_COUNT) return false;
	auto& state=m_aAnimation[index];
	if (state.player.Get()!=player) {state={};state.player.Set(player);}
	if (state.eventCount==int(state.events.size()))
	{
		// Lose continuity rather than partially replay an overflowing packet.
		state.overflow=true;state.eventCount=0;
	}
	state.events[state.eventCount++]={event,data,TIME_TO_TICKS(player->m_flSimulationTime())};
	return true;
}

bool CResolver::ReconstructAnimation(CTFPlayer* player)
{
	if (!UsesAnimationReplay(player)) {DiscardAnimation(player);return false;}
	const int index=player->entindex();
	if (index<=0 || index>=int(m_aAnimation.size())) return false;
	auto* anim=player->m_PlayerAnimState();
	if (!anim || !player->IsAlive() || player->IsDormant() || player->IsAGhost() || player->InCond(TF_COND_TAUNTING)
		|| player->m_nSequence()<0) {DiscardAnimation(player);return false;}
	if (I::Prediction->m_bEnginePaused) return true;
	auto& state=m_aAnimation[index];
	if (state.player.Get()!=player) {state={};state.player.Set(player);}
	auto point=[](const Vec3& value){return AnimationTimeline::V{value.x,value.y,value.z};};
	auto vector=[](AnimationTimeline::V value){return Vec3(value.x,value.y,value.z);};
	AnimationTimeline::Observation current;
	current.valid=true;current.tick=TIME_TO_TICKS(player->m_flSimulationTime());
	current.origin=point(player->m_vecOrigin());current.mins=point(player->m_vecMins());current.maxs=point(player->m_vecMaxs());
	current.velocity=point(player->GetAbsVelocity());current.angles={player->m_angEyeAnglesX(),player->m_angEyeAnglesY(),0};
	current.handle=player->As<IHandleEntity>()->GetRefEHandle().ToInt();current.spawn=player->m_iSpawnCounter();
	current.model=player->m_nModelIndex();current.playerClass=player->m_iClass();
	if (auto* parent=player->GetMoveParent()) current.parent=parent->As<IHandleEntity>()->GetRefEHandle().ToInt();
	current.interpolation=player->m_ubInterpolationFrame();current.scale=player->m_flModelScale();
	current.flags=player->m_fFlags();current.water=player->m_nWaterLevel();
	if (!AnimationTimeline::Valid(current)) {DiscardAnimation(player);return false;}
	if (AnimationTimeline::SameLife(state.observed,current) && state.observed.tick==current.tick && !state.overflow)
		return true;
	const float gravity=SDK::GetGravity()*(player->m_flGravity()!=0?player->m_flGravity():1.f);
	auto previous=state.observed;
	if (state.overflow) previous.valid=false;
	bool doubleJump=false;
	for (int i=0;i<state.eventCount;++i)
		doubleJump|=state.events[i].type==PLAYERANIMEVENT_DOUBLEJUMP||state.events[i].type==PLAYERANIMEVENT_DOUBLEJUMP_CROUCH;
	auto timeline=AnimationTimeline::Build(previous,current,TICK_INTERVAL,gravity,-1,doubleJump);
	if (timeline.count<=0) return false;
	float resolvedYaw=current.angles.y,resolvedPitch=current.angles.x;
	GetAngles(player,&resolvedYaw,&resolvedPitch);
	if (!std::isfinite(resolvedYaw)) resolvedYaw=current.angles.y;
	if (!std::isfinite(resolvedPitch)) resolvedPitch=current.angles.x;
	const float yawOffset=Math::NormalizeAngle(resolvedYaw-current.angles.y);
	const int candidate=(int(std::lround(yawOffset/45.f))+8)%8;
	const bool bank=Vars::Resolver::Enabled.Value && Vars::Resolver::AutoResolve.Value
		&&std::abs(Math::NormalizeAngle(yawOffset-candidate*45.f))<.01f;
	const int selected=bank?candidate:-1;
	if (!timeline.continuous || !state.pose.Restore(player))
	{
		timeline=AnimationTimeline::Build({},current,TICK_INTERVAL,gravity);
		if (state.observed.valid && (state.observed.spawn!=current.spawn || state.observed.model!=current.model
			|| state.observed.playerClass!=current.playerClass)) anim->ClearAnimationState();
		player->m_flAnimTime()=TICKS_TO_TIME(current.tick-1);state.layerOrder={};
		for (auto& feet:state.feet) feet={anim->m_flGoalFeetYaw,anim->m_flCurrentFeetYaw};
		F::Backtrack.ResolverUpdate(player);
	}
	if (selected>=0)
	{
		anim->m_flGoalFeetYaw=state.feet[selected].goal;
		anim->m_flCurrentFeetYaw=state.feet[selected].current;
	}
	if (state.active!=selected || !state.identity.Valid()) InvalidateIdentity(player);
	state.active=selected;
	state.identity.handle=current.handle;state.identity.spawn=current.spawn;state.identity.hypothesis=selected;
	struct RestoreInputs
	{
		CTFPlayer* player;CGlobalVarsBase clock;bool updating;
		Vec3 origin,absolute,velocity,absoluteVelocity,angles,mins,maxs,absoluteAngles;
		int flags;byte water;bool ducked;EHANDLE ground;float simulationTime;
		~RestoreInputs()
		{
			player->m_vecOrigin()=origin;player->SetAbsOrigin(absolute);
			player->m_vecVelocity()=velocity;player->SetAbsVelocity(absoluteVelocity);
			player->m_angEyeAnglesX()=angles.x;player->m_angEyeAnglesY()=angles.y;
			player->m_vecMins()=mins;player->m_vecMaxs()=maxs;player->m_fFlags()=flags;
			player->m_nWaterLevel()=water;player->m_bDucked()=ducked;player->m_hGroundEntity()=ground;
			player->SetAbsAngles(absoluteAngles);player->m_flSimulationTime()=simulationTime;
			*I::GlobalVars=clock;G::UpdatingAnims=updating;
		}
	} restore{player,*I::GlobalVars,G::UpdatingAnims,player->m_vecOrigin(),player->GetAbsOrigin(),player->m_vecVelocity(),
		player->GetAbsVelocity(),{player->m_angEyeAnglesX(),player->m_angEyeAnglesY(),0},player->m_vecMins(),player->m_vecMaxs(),
		player->GetAbsAngles(),player->m_fFlags(),player->m_nWaterLevel(),player->m_bDucked(),player->m_hGroundEntity(),player->m_flSimulationTime()};
	G::UpdatingAnims=true;I::GlobalVars->frametime=TICK_INTERVAL;I::GlobalVars->interpolation_amount=0;
	// Event packets identify the event, not its hidden command. Preserve their
	// order, place unknown events at the first command and fit jump transitions.
	for (int i=0;i<state.eventCount;++i)
		if (state.events[i].type==PLAYERANIMEVENT_JUMP && timeline.jump<0)
		{
			timeline.jump=timeline.first;
			if (current.flags&FL_ONGROUND) timeline.land=current.tick;
		}
	int earliest=timeline.first;
	for (int i=0;i<state.eventCount;++i)
	{
		auto& event=state.events[i];
		event.tick=earliest;
		if (event.type==PLAYERANIMEVENT_JUMP && timeline.jump>=0) event.tick=(std::max)(earliest,timeline.jump);
		else if ((event.type==PLAYERANIMEVENT_DOUBLEJUMP || event.type==PLAYERANIMEVENT_DOUBLEJUMP_CROUCH) && timeline.airDash>=0)
			event.tick=std::clamp(timeline.airDash,earliest,current.tick);
		earliest=event.tick;
	}
	int nextEvent=0;
	for (int tick=timeline.first;tick<=current.tick;++tick)
	{
		state.layerOrder.BeginTick();
		const auto sample=timeline.At(tick);const bool endpoint=tick==current.tick;
		I::GlobalVars->curtime=TICKS_TO_TIME(tick);I::GlobalVars->tickcount=tick;
		player->m_flSimulationTime()=TICKS_TO_TIME(tick);
		player->m_vecOrigin()=vector(sample.origin);player->SetAbsOrigin(vector(sample.origin));
		player->m_vecVelocity()=vector(sample.velocity);player->SetAbsVelocity(vector(sample.velocity));
		player->m_fFlags()=sample.flags;player->m_nWaterLevel()=byte(sample.water);player->m_bDucked()=bool(sample.flags&FL_DUCKING);
		player->m_vecMins()=vector(sample.mins);player->m_vecMaxs()=vector(sample.maxs);
		if (!(sample.flags&FL_ONGROUND)) player->m_hGroundEntity().Term();
		for (;nextEvent<state.eventCount && state.events[nextEvent].tick<=tick;++nextEvent)
			anim->DoAnimationEvent(state.events[nextEvent].type,state.events[nextEvent].data);
		const float yaw=endpoint?current.angles.y:Math::NormalizeAngle(sample.angles.y+yawOffset);
		const float pitch=endpoint?resolvedPitch:sample.angles.x;
		player->m_angEyeAnglesX()=pitch;player->m_angEyeAnglesY()=yaw;
		const bool moving=AnimationTimeline::Length2(sample.velocity)>1.f;
		for (int i=0;i<8;++i) state.feet[i].Step(endpoint?current.angles.y:sample.angles.y+i*45.f,TICK_INTERVAL,moving);
		const float interval=std::clamp(I::GlobalVars->curtime-player->m_flAnimTime(),0.f,.2f);
		if (interval>.001f) player->FrameAdvance(interval);
		std::array<CAnimationLayer,GESTURE_SLOT_COUNT> before{};
		const int layers=(std::min)(anim->m_aGestureSlots.Count(),int(before.size()));
		for (int i=0;i<layers;++i) if (auto* layer=anim->m_aGestureSlots[i].m_pAnimLayer)
		{
			before[i]=*layer;
			state.layerOrder.Observe(i,int(layer->m_nSequence),layer->m_flWeight>0);
		}
		anim->Update(yaw,pitch);
		for (int i=0;i<layers;++i)
		{
			auto* layer=anim->m_aGestureSlots[i].m_pAnimLayer;
			if (!layer) continue;
			const int order=state.layerOrder.Observe(i,int(layer->m_nSequence),layer->m_flWeight>0,
				layer->m_flCycle<before[i].m_flCycle);
			if (layer->m_flWeight<=0) continue;
			layer->m_nOrder=order;
			if (before[i].m_flWeight<=0 || layer->m_nSequence!=before[i].m_nSequence || layer->m_flCycle<before[i].m_flCycle)
				layer->m_flCycle=layer->m_flPrevCycle=0.f;
			else if (interval>.001f)
				layer->m_flCycle=(std::min)(float(before[i].m_flCycle)+(float(layer->m_flCycle)-float(before[i].m_flCycle))*interval/TICK_INTERVAL,1.f);
		}
	}
	if (selected>=0) state.feet[selected]={anim->m_flGoalFeetYaw,anim->m_flCurrentFeetYaw};
	state.pose.Store(player);state.observed=current;state.eventCount=0;state.overflow=false;
	// LATCH_ANIMATION_VAR: preserve the reconstructed sequence/poses through
	// the next PreDataUpdate, rather than restoring the old interpolation copy.
	player->OnLatchInterpolatedVariables(1);player->InvalidateBoneCache();
	return true;
}

void CResolver::FrameStageNotify()
{
	if(!Vars::Resolver::AnimationHypotheses.Value || (!Vars::Resolver::Enabled.Value&&!Vars::Visuals::Removals::Interpolation.Value))
		for(auto& state:m_aAnimation)
		{
			state.observed={};state.pose.valid=false;state.eventCount=0;state.overflow=false;state.active=-1;state.layerOrder={};
		}

	if (!Vars::Resolver::Enabled.Value || !I::EngineClient->IsInGame() || I::EngineClient->IsPlayingDemo())
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	StoreSniperDots(pResource);
	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer() ||
			pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() ||
			!H::Entities.GetDeltaTime(pPlayer->entindex()))
			continue;

		int iUserID = pResource->m_iUserID(pPlayer->entindex());
		auto& tData = m_mResolverData[iUserID];

		if (tData.m_flYaw)
			tData.m_bYaw = true;
		else
			tData.m_bYaw = false;

		if (fabsf(pPlayer->m_angEyeAnglesX()) == 90.f)
		{
			if (auto flPitch = GetPitchForSniperDot(pPlayer, pResource))
				tData.m_flPitch = *flPitch;
			else if (!tData.m_bFirstOOBPitch && tData.m_bAutoSetPitch || tData.m_bInversePitch)
				tData.m_flPitch = -pPlayer->m_angEyeAnglesX(); // set to inverse by default
			else
				tData.m_flPitch = fabsf(tData.m_flPitch) > 45.f ? sign(tData.m_flPitch) * 90 : 0; // limit to 90, 0, -90
			tData.m_bPitch = true, tData.m_bFirstOOBPitch = true;
		}
		else
			tData.m_bPitch = false;
	}
}

void CResolver::CycleMiss(int userID)
{
	if(auto* pTarget=GetPlayerForUserID(userID);pTarget&&pTarget->IsAlive()&&!pTarget->IsDormant())
	{
		auto& tData = m_mResolverData[userID];

		float& flYaw = tData.m_flYaw;
		float& flPitch = tData.m_flPitch;
		bool bAutoYaw = tData.m_bAutoSetYaw && Vars::Resolver::AutoResolveYawAmount.Value;
		bool bAutoPitch = tData.m_bAutoSetPitch && Vars::Resolver::AutoResolvePitchAmount.Value;

		if (bAutoPitch && fabsf(pTarget->m_angEyeAnglesX()) == 90.f && !m_mSniperDots.contains(userID)
			&& (!bAutoYaw || sign(flYaw) != sign(flYaw + Vars::Resolver::AutoResolveYawAmount.Value) && sign(flYaw) != 0))
		{
			flPitch = Math::ClampNormalizeAngle(flPitch + Vars::Resolver::AutoResolvePitchAmount.Value, 90.f);

			F::Backtrack.ResolverUpdate(pTarget);
			F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(userID), "Cycling", "pitch", flPitch);
		}

		if (bAutoYaw)
		{
			flYaw = Math::NormalizeAngle(flYaw + Vars::Resolver::AutoResolveYawAmount.Value);

			F::Backtrack.ResolverUpdate(pTarget);
			F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(userID), "Cycling", "yaw", flYaw);
		}

	}
}

void CResolver::CreateMove()
{
	auto* local=H::Entities.GetLocal();
	if(!Vars::Resolver::Enabled.Value||!local||!local->IsAlive()||I::EngineClient->IsPlayingDemo())
	{m_shots.Clear();m_hShotOwner.Term();return;}
	if(m_hShotOwner.Get()!=local) {m_shots.Clear();m_hShotOwner.Set(local);}
	std::array<int,128> cycled{};int count=0;
	if (!Vars::Resolver::AutoResolve.Value) m_shots.Clear();
	m_shots.ExpireDetailed(I::GlobalVars->curtime,I::ClientState->command_ack,[&](const AimPolicy::PendingShot& shot) {
		const int userID=shot.target;
		auto* target=GetPlayerForUserID(userID);
		if (!target || (Vars::Resolver::AutoResolveHeadshotOnly.Value&&!shot.head)
			|| (Vars::Resolver::AutoResolveCheatersOnly.Value&&!F::PlayerUtils.HasTag(target->entindex(),CHEATER_TAG))) return;
		const AnimationTimeline::Identity expected{shot.revision,shot.entity,shot.spawn,shot.hypothesis};
		if (!AnimationTimeline::FeedbackApplies(expected,AnimationIdentity(GetPlayerForUserID(userID)))) return;
		for(int i=0;i<count;++i) if(cycled[i]==userID) return;
		if(count<int(cycled.size())) {cycled[count++]=userID;CycleMiss(userID);}
	});

	if (!Vars::Resolver::Enabled.Value)
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	auto fGetPlayerClosestToFOV = [&]()
	{
		CTFPlayer* pClosest = nullptr;
		float flMinFOV = std::numeric_limits<float>::max();

		const Vec3 vLocalPos = F::Ticks.GetShootPos();
		const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

		for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
		{
			if (pEntity->entindex() == I::EngineClient->GetLocalPlayer() || pEntity->IsDormant())
				continue;

			Vec3 vCurPos = pEntity->GetCenter();
			Vec3 vCurAngleTo = Math::CalcAngle(vLocalPos, vCurPos);
			float flCurFOVTo = Math::CalcFov(vLocalAngles, vCurAngleTo);

			if (flCurFOVTo < flMinFOV)
			{
				pClosest = pEntity->As<CTFPlayer>();
				flMinFOV = flCurFOVTo;
			}
		}

		return pClosest;
	};

	if (Vars::Resolver::CycleYaw.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastYawCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				int iUserID = pResource->m_iUserID(pTarget->entindex());
				auto& tData = m_mResolverData[iUserID];

				float& flYaw = tData.m_flYaw;
				flYaw = Math::NormalizeAngle(flYaw + Vars::Resolver::CycleYaw.Value);

				F::Backtrack.ResolverUpdate(pTarget);
				F::Output.ReportResolver(pTarget->entindex(), "Cycling", "yaw", flYaw);
			}

			m_flLastYawCycle = SDK::PlatFloatTime();
		}
	}
	else
		m_flLastYawCycle = 0.f;

	if (Vars::Resolver::CyclePitch.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastPitchCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				int iUserID = pResource->m_iUserID(pTarget->entindex());
				auto& tData = m_mResolverData[iUserID];

				if (!m_mSniperDots.contains(iUserID))
				{
					if (fabsf(pTarget->m_angEyeAnglesX()) != 90.f)
						F::Output.ReportResolver("Target not using out of bounds pitch");
					
					float& flPitch = tData.m_flPitch;
					flPitch = Math::ClampNormalizeAngle(flPitch + Vars::Resolver::CyclePitch.Value, 90.f);

					F::Backtrack.ResolverUpdate(pTarget);
					F::Output.ReportResolver(pTarget->entindex(), "Cycling", "pitch", flPitch);
				}
				else
					F::Output.ReportResolver("Target pitch overridden by sniper dot");
			}

			m_flLastPitchCycle = SDK::PlatFloatTime();
		}
	}
	else
		m_flLastPitchCycle = 0.f;

	if (Vars::Resolver::CycleMinwalk.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastMinwalkCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				int iUserID = pResource->m_iUserID(pTarget->entindex());
				auto& tData = m_mResolverData[iUserID];

				bool& bMinwalk = tData.m_bMinwalk;
				bMinwalk = !bMinwalk;

				F::Backtrack.ResolverUpdate(pTarget);
				F::Output.ReportResolver(pTarget->entindex(), "Cycling", "minwalk", bMinwalk);
			}

			m_flLastMinwalkCycle = SDK::PlatFloatTime();
		}
	}
	else
		m_flLastMinwalkCycle = 0.f;

	if (Vars::Resolver::CycleView.Value)
	{
		if (SDK::PlatFloatTime() > m_flLastViewCycle + 0.5f)
		{
			if (auto pTarget = fGetPlayerClosestToFOV())
			{
				int iUserID = pResource->m_iUserID(pTarget->entindex());
				auto& tData = m_mResolverData[iUserID];

				bool& bView = tData.m_bView;
				bView = !bView;

				F::Backtrack.ResolverUpdate(pTarget);
				F::Output.ReportResolver(pTarget->entindex(), "Cycling", "view", bView ? "view to local" : "static");
			}

			m_flLastViewCycle = SDK::PlatFloatTime();
		}
	}
	else
		m_flLastViewCycle = 0.f;
}

void CResolver::HitscanRan(CTFPlayer* pLocal, CTFPlayer* pTarget, CTFWeaponBase* pWeapon, int nHitbox,const TickRecord* record)
{
	if (!Vars::Resolver::Enabled.Value || !Vars::Resolver::AutoResolve.Value
		|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
		|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::SmoothVelocity
		|| pLocal->m_iTeamNum() == pTarget->m_iTeamNum())
		return;

	if (Vars::Resolver::AutoResolveCheatersOnly.Value && !F::PlayerUtils.HasTag(pTarget->entindex(), CHEATER_TAG))
		return;

	if (Vars::Resolver::AutoResolveHeadshotOnly.Value && (nHitbox != HITBOX_HEAD || !G::CanHeadshot))
		return;

	if (pWeapon->GetWeaponSpread())
	{
		// check for perfect bullet
		const float flTimeSinceLastShot = I::GlobalVars->curtime - pWeapon->m_flLastFireTime();
		if (flTimeSinceLastShot <= (pWeapon->GetBulletsPerShot() > 1 ? 0.25f : 1.25f))
			return;
	}

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	if(!G::CurrentUserCmd) return;
	if(m_hShotOwner.Get()!=pLocal) {m_shots.Clear();m_hShotOwner.Set(pLocal);}
	const bool head=nHitbox==HITBOX_HEAD&&G::CanHeadshot;
	const float deadline=I::GlobalVars->curtime+F::Backtrack.GetReal(MAX_FLOWS,false)*1.5f+.1f;
	if (!AnimationIdentity(pTarget).Valid()) InvalidateIdentity(pTarget);
	const auto identity=record && record->m_tResolverIdentity.Valid()?record->m_tResolverIdentity:AnimationIdentity(pTarget);
	m_preparedShot={pResource->m_iUserID(pTarget->entindex()),pWeapon->GetWeaponID(),G::CurrentUserCmd->command_number,deadline,head,
		identity.handle,identity.spawn,identity.hypothesis,identity.revision};
	m_hPreparedWeapon.Set(pWeapon);m_iPreparedCommand=G::OriginalCmd.command_number;
}

void CResolver::BeginCommand()
{
	m_preparedShot={};m_hPreparedWeapon.Term();m_iPreparedCommand=-1;
}

void CResolver::CommitShot(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command)
{
	// CritHack may replace the command number, and later movement/compatibility
	// features may cancel attack. Record only the final command that will fire.
	if (m_preparedShot.target && m_iPreparedCommand==G::OriginalCmd.command_number
		&& local && local==m_hShotOwner.Get() && weapon && weapon==m_hPreparedWeapon.Get()
		&& SDK::IsAttacking(local,weapon,command)==1)
	{
		m_preparedShot.command=command->command_number;
		m_shots.Add(m_preparedShot);
	}
	BeginCommand();
}

void CResolver::PlayerHurt(IGameEvent* event)
{
	if(!event||!Vars::Resolver::Enabled.Value
		||I::EngineClient->GetPlayerForUserID(event->GetInt("attacker"))!=I::EngineClient->GetLocalPlayer()) return;
	const int custom=event->GetInt("custom");
	const bool head=custom==TF_DMG_CUSTOM_HEADSHOT||custom==TF_DMG_CUSTOM_HEADSHOT_DECAPITATION;
	m_shots.Hit(event->GetInt("userid"),event->GetInt("weaponid"),head);
}

void CResolver::SetYaw(int iUserID, float flValue, bool bAuto)
{
	auto& tData = m_mResolverData[iUserID];

	if (bAuto)
	{
		tData.m_bAutoSetYaw = true;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "yaw", "auto");
	}
	else
	{
		tData.m_flYaw = flValue;
		tData.m_bAutoSetYaw = false;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "yaw", flValue);
	}

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
}

void CResolver::SetPitch(int iUserID, float flValue, bool bInverse, bool bAuto)
{
	auto& tData = m_mResolverData[iUserID];

	if (bAuto)
	{
		tData.m_bInversePitch = false;
		tData.m_bAutoSetPitch = true;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "pitch", "auto");
	}
	else if (bInverse)
	{
		tData.m_bInversePitch = true;
		tData.m_bAutoSetPitch = false;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "pitch", "inverse");
	}
	else
	{
		auto pPlayer = GetPlayerForUserID(iUserID);
		if (pPlayer && fabsf(pPlayer->m_angEyeAnglesX()) != 90.f)
			F::Output.ReportResolver("Target not using out of bounds pitch");

		tData.m_flPitch = flValue;
		tData.m_bInversePitch = false;
		tData.m_bAutoSetPitch = false;

		F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "pitch", flValue);
	}

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
}

void CResolver::SetMinwalk(int iUserID, bool bValue)
{
	auto& tData = m_mResolverData[iUserID];

	tData.m_bMinwalk = bValue;

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
	F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "minwalk", bValue);
}

void CResolver::SetView(int iUserID, bool bValue)
{
	auto& tData = m_mResolverData[iUserID];

	tData.m_bView = bValue;

	F::Backtrack.ResolverUpdate(GetPlayerForUserID(iUserID));
	F::Output.ReportResolver(I::EngineClient->GetPlayerForUserID(iUserID), "Set", "view", bValue ? "view to local" : "static");
}

bool CResolver::GetAngles(CTFPlayer* pPlayer, float* pYaw, float* pPitch, bool* pMinwalk, bool bFake)
{
	if (!Vars::Resolver::Enabled.Value)
		return false;

	if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
		return false;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return false;

	int iUserID = pResource->m_iUserID(pPlayer->entindex());
	auto& tData = m_mResolverData[iUserID];

	bool bYaw = tData.m_bYaw, bPitch = tData.m_bPitch;
	
	if (pYaw)
	{
		*pYaw = pPlayer->m_angEyeAnglesY();
		if (bYaw && !bFake)
		{
			auto pLocal = H::Entities.GetLocal();
			if (pLocal && tData.m_bView)
				*pYaw = Math::CalcAngle(pPlayer->m_vecOrigin(), pLocal->m_vecOrigin()).y;
			*pYaw += tData.m_flYaw;
		}
	}

	if (pPitch)
	{
		*pPitch = pPlayer->m_angEyeAnglesX();
		if (bPitch)
			*pPitch = tData.m_flPitch;
	}

	if (pMinwalk)
		*pMinwalk = tData.m_bMinwalk;

	return bYaw || bPitch;
}
