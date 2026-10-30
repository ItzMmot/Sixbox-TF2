#include "AutoRocketJump.h"
#include "../AimbotProjectile/ProjProfiler.h"

#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"

namespace
{
	bool RocketLauncher(CTFWeaponBase* weapon)
	{
		if (!weapon) return false;
		const int id = weapon->GetWeaponID();
		return id == TF_WEAPON_ROCKETLAUNCHER || id == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT || id == TF_WEAPON_PARTICLE_CANNON;
	}
}

void CAutoRocketJump::Reset()
{
	m_iFrame = -1;
	m_iDelay = m_iChoke = 0;
	m_bRunning = m_bReloadWaiting = m_bDuckAfterLaunch = m_bLastGrounded = false;
	m_owner.Term(); m_weapon.Term();
	m_reload.Reset();
}

// Run before engine prediction: the reload-interrupt command must not jump first.
void CAutoRocketJump::Prepare(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!ShouldRun(pLocal, pWeapon, pCmd)) { Reset(); return; }
	if (m_owner.Get() != pLocal || m_weapon.Get() != pWeapon) Reset();
	m_owner.Set(pLocal); m_weapon.Set(pWeapon);
	if (m_iFrame != -1)
	{
		pCmd->buttons &= ~IN_JUMP;
		return;
	}
	if (Vars::Misc::Movement::AutoRocketJump.Value && pLocal->m_hGroundEntity() && G::CanPrimaryAttack)
		pCmd->buttons &= ~IN_JUMP; // predict the floor before issuing our timed jump

	const bool wasWaiting = m_bReloadWaiting;
	m_bReloadWaiting = m_bRunning = false;
	// Beggar's uses a load/release burst, rather than the normal reload interruption.
	if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka
		|| (!pLocal->m_hGroundEntity() && !pLocal->IsDucking() && !Vars::Misc::Movement::AutoRocketJumpWalls.Value))
	{
		m_reload.Reset();
		return;
	}
	const auto action = m_reload.Update(TICKS_TO_TIME(pLocal->m_nTickBase()), G::CanPrimaryAttack,
		pWeapon->IsInReload(), pWeapon->HasPrimaryAmmoForShot(), pWeapon->IsEnergyWeapon()
		|| pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType()) >= pWeapon->GetAmmoPerShot());
	if (action == RocketJump::ReloadAction::Ready && wasWaiting)
		pCmd->buttons &= ~IN_JUMP;
	if (action != RocketJump::ReloadAction::Interrupt && action != RocketJump::ReloadAction::WaitingForShell)
		return;

	m_bReloadWaiting = m_bRunning = true;
	m_bLastGrounded = bool(pLocal->m_hGroundEntity());
	pCmd->buttons &= ~(IN_JUMP | IN_ATTACK2 | IN_RELOAD);
	if (action == RocketJump::ReloadAction::Interrupt)
		pCmd->buttons |= IN_ATTACK;
	else
		pCmd->buttons &= ~IN_ATTACK; // let the first shell finish loading
}

bool CAutoRocketJump::ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pCmd || !I::GlobalVars || !pLocal->IsAlive() || pLocal->IsAGhost()
		|| !pLocal->CanAttack() || pLocal->IsSwimming() || pLocal->m_MoveType() != MOVETYPE_WALK
		|| (!Vars::Misc::Movement::AutoRocketJump.Value && !Vars::Misc::Movement::AutoCTap.Value)
		|| !RocketLauncher(pWeapon)
		|| I::EngineVGui->IsGameUIVisible()
		|| pCmd->weaponselect)
		return false;

	return true;
}

namespace
{
	Vec3 JumpWish(CTFPlayer* player, CUserCmd* cmd, bool preserveDiagonal)
	{
		const Vec3 input = { cmd->forwardmove, cmd->sidemove, 0.f };
		if (!preserveDiagonal && input.IsZero() && player->m_vecVelocity().Length2DSqr() > 1.f)
			return player->m_vecVelocity().Normalized2D();
		const float yaw = input.IsZero() ? cmd->viewangles.y : cmd->viewangles.y - Math::VectorAngles(input).y;
		Vec3 wish; Math::AngleVectors({ 0.f, yaw, 0.f }, &wish);
		return wish;
	}
}

bool CAutoRocketJump::RateBlast(CTFPlayer* pLocal, const CGameTrace& impact, const Vec3& origin,
	const Vec3& mins, const Vec3& maxs, const Vec3& view, const Vec3& wish, float& score)
{
	const Vec3 blast = impact.endpos + impact.plane.normal;
	const Vec3 nearest = origin + Vec3(std::clamp(blast.x - origin.x, mins.x, maxs.x),
		std::clamp(blast.y - origin.y, mins.y, maxs.y), std::clamp(blast.z - origin.z, mins.z, maxs.z));
	const float distanceSqr = blast.DistToSqr(nearest);
	const float radius = TF_ROCKET_RADIUS_FOR_RJS;
	const float safeRadius = radius - 12.f;
	if (!std::isfinite(distanceSqr) || distanceSqr >= safeRadius * safeRadius
		|| !SDK::VisPosWorld(pLocal, pLocal, blast, origin + view, MASK_SHOT))
		return false;
	// TF2's self-push direction uses the victim's center and a 10 HU bias.
	// Directed modes crouch after launch: use the expected crouched center.
	Vec3 forceMaxs = maxs;
	const auto mode = Vars::Misc::Movement::AutoRocketJump.Value
		? RocketJump::GetMode(Vars::Misc::Movement::AutoRocketJumpMode.Value) : RocketJump::Mode::Diagonal;
	if (mode != RocketJump::Mode::Diagonal)
		forceMaxs.z = std::min(forceMaxs.z, I::GameMovement->GetPlayerMaxs(true).z * pLocal->m_flModelScale());
	const Vec3 delta = origin + (mins + forceMaxs) * .5f + Vec3(0.f, 0.f, 10.f) - blast;
	score = RocketJump::BoostScore(mode, delta.x, delta.y, delta.z, std::sqrt(distanceSqr), radius, wish.x, wish.y);
	return std::isfinite(score) && score > 0.f;
}

bool CAutoRocketJump::FindFloorJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iImpact)
{
	const bool automated = Vars::Misc::Movement::AutoRocketJump.Value;
	const auto mode = automated ? RocketJump::GetMode(Vars::Misc::Movement::AutoRocketJumpMode.Value) : RocketJump::Mode::Diagonal;
	const bool diagonal = mode == RocketJump::Mode::Diagonal;
	const bool grounded = bool(pLocal->m_hGroundEntity());
	const Vec3 original = pLocal->m_vecOrigin();
	const Vec3 wish = JumpWish(pLocal, pCmd, diagonal);
	// Reuse a single native movement prediction for every candidate. Snapshots and
	// candidate arrays have a fixed size; the search allocates no containers.
	struct Snapshot { Vec3 origin, velocity, mins, maxs, view; bool grounded = false; } states[10] = {};
	MoveStorage storage;
	const bool initialized = F::MoveSim.Initialize(pLocal, storage, false);
	int landing = 0;
	storage.m_bBunnyHop = false; // the launch sequence owns jump timing
	if (initialized && !storage.m_bFailed)
	{
		for (int n = 1; n < 10; ++n)
		{
			F::MoveSim.RunTick(storage, false);
			if (storage.m_bFailed) break;
			states[n] = { storage.m_MoveData.m_vecAbsOrigin, storage.m_MoveData.m_vecVelocity,
				pLocal->m_vecMins(), pLocal->m_vecMaxs(), pLocal->m_vecViewOffset(), pLocal->IsOnGround() && !pLocal->IsSwimming() };
			if (!landing && states[n].grounded) landing = n;
		}
	}
	F::MoveSim.Restore(storage);
	if (!initialized || storage.m_bFailed || (!landing && diagonal)) return false;
	if (!landing) landing = 1; // directed airborne shots may splash the floor before landing

	ProjectileInfo initial = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, initial, ProjSimEnum::NoRandomAngles)) return false;
	const Vec3 muzzleOffset = initial.m_vPos - pLocal->GetShootPos();
	const float legacyOffset = std::sqrt(2.f * muzzleOffset.y * muzzleOffset.y + muzzleOffset.z * muzzleOffset.z);
	const float offsets[] = { 0.f, 8.f, 12.f, 16.f, 24.f, 32.f, 40.f, 48.f, 56.f, 64.f, 72.f, 80.f, 88.f, 96.f, 112.f };
	const int count = diagonal || !automated ? 1 : int(std::size(offsets));
	const int skip = std::clamp(grounded ? Vars::Misc::Movement::AutoRocketJumpSkipGround.Value : Vars::Misc::Movement::AutoRocketJumpSkipAir.Value, 0, 3);
	const float interval = I::GlobalVars->interval_per_tick;
	CTraceFilterWorldAndPropsOnly filter(pLocal);
	CTraceFilterCollideable rocketFilter(pLocal);
	Vec3 bestAngles = {};
	float bestScore = -FLT_MAX;
	bool found = false;
	for (int candidate = 0; candidate < count; ++candidate)
	{
		int lead = landing;
		for (int refine = 0; refine < (diagonal ? 1 : 3); ++refine)
		{
			m_vAngles = pCmd->viewangles;
			if (automated)
			{
				Vec3 point;
				if (diagonal)
					point = grounded ? original - wish * legacyOffset
						: states[landing].origin - states[landing].velocity.Normalized2D() * legacyOffset;
				else
					point = states[lead].origin - wish * offsets[candidate];
				CGameTrace floor = {};
				SDK::Trace(point + Vec3(0.f, 0.f, 24.f), point - Vec3(0.f, 0.f, 128.f), MASK_SOLID, &filter, &floor);
				if (!floor.DidHit() || floor.startsolid || floor.allsolid || (floor.surface.flags & (SURF_SKY | SURF_SKY2D))
					|| !RocketJump::FloorNormal(floor.plane.normal.x, floor.plane.normal.y, floor.plane.normal.z)
					|| !AimAtPoint(pLocal, pWeapon, floor.endpos + floor.plane.normal)) break;
			}
			ProjectileInfo info = {};
			if (!F::ProjSim.GetInfo(pLocal, pWeapon, m_vAngles, info, ProjSimEnum::Redirect | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles)
				|| info.m_flGravity != 0.f || info.m_flVelocity <= 0.f) break;
			Vec3 forward; Math::AngleVectors(info.m_vAng, &forward);
			CGameTrace impact = {};
			SDK::TraceHull(info.m_vPos, info.m_vPos + forward * (info.m_flVelocity * interval * 9.f),
				-info.m_vHull, info.m_vHull, MASK_SOLID, &rocketFilter, &impact);
			if (!impact.DidHit() || impact.startsolid || impact.allsolid || (impact.surface.flags & (SURF_SKY | SURF_SKY2D))
				|| impact.m_pEnt && impact.m_pEnt->IsPlayer()
				|| !RocketJump::FloorNormal(impact.plane.normal.x, impact.plane.normal.y, impact.plane.normal.z)) break;
			const int travelTicks = RocketJump::ImpactTicks(impact.endpos.DistTo(info.m_vPos), info.m_flVelocity, interval);
			const int tick = travelTicks + skip;
			if (!travelTicks || tick >= 10) break;
			const auto& state = states[tick];
			float score;
			if ((state.grounded || !grounded && !diagonal)
				&& RateBlast(pLocal, impact, state.origin, state.mins, state.maxs, state.view, wish, score))
			{
				if (diagonal || score > bestScore)
				{
					bestScore = score; bestAngles = m_vAngles; iImpact = tick; found = true;
				}
			}
			if (tick == lead || diagonal) break;
			lead = tick;
		}
	}
	if (found)
	{
		m_vAngles = bestAngles;
		if (Vars::Misc::Movement::AutoRocketJumpTimingOffset.Value && iImpact > Vars::Misc::Movement::AutoRocketJumpApplyAbove.Value)
			iImpact = std::max(iImpact + Vars::Misc::Movement::AutoRocketJumpTimingOffset.Value, 0);
	}
	return found;
}

bool CAutoRocketJump::AimAtPoint(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& vPoint)
{
	const Vec3 vLocalPos = pLocal->GetShootPos();
	ProjectileInfo tProjInfo = {};

	float flPitch, flYaw;
	{	// basic trajectory pass
		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		m_vAngles.x = flPitch = vAngleTo.x, m_vAngles.y = flYaw = vAngleTo.y;
	}

	if (!F::ProjSim.GetInfo(pLocal, pWeapon, { flPitch, flYaw, 0 }, tProjInfo, ProjSimEnum::Redirect | ProjSimEnum::NoRandomAngles))
		return false;

	{	// correct yaw
		Vec3 vShootPos = (tProjInfo.m_vPos - vLocalPos).To2D();
		Vec3 vTarget = vPoint - vLocalPos;
		Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng, &vForward); vForward.Normalize2D();
		float flB = 2 * (vShootPos.x * vForward.x + vShootPos.y * vForward.y);
		float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
		float root;
		if (RocketJump::PositiveRoot(flB, flC, root))
		{
			vShootPos += vForward * root;
			m_vAngles.y = flYaw - (Math::Rad2Deg(atan2(vShootPos.y, vShootPos.x)) - flYaw);
			flYaw = Math::Rad2Deg(atan2(vShootPos.y, vShootPos.x));
		}
	}

	{	// correct pitch
		Vec3 vShootPos = Math::RotatePoint(tProjInfo.m_vPos - vLocalPos, {}, { 0, -flYaw, 0 }); vShootPos.y = 0;
		Vec3 vTarget = Math::RotatePoint(vPoint - vLocalPos, {}, { 0, -flYaw, 0 });
		Vec3 vForward; Math::AngleVectors(tProjInfo.m_vAng - Vec3(0, flYaw, 0), &vForward); vForward.y = 0; vForward.Normalize();
		float flB = 2 * (vShootPos.x * vForward.x + vShootPos.z * vForward.z);
		float flC = (powf(vShootPos.x, 2) + powf(vShootPos.z, 2)) - (powf(vTarget.x, 2) + powf(vTarget.z, 2));
		float root;
		if (RocketJump::PositiveRoot(flB, flC, root))
		{
			vShootPos += vForward * root;
			m_vAngles.x = flPitch - (Math::Rad2Deg(atan2(-vShootPos.z, vShootPos.x)) - flPitch);
		}
	}

	m_vAngles.y = Math::NormalizeAngle(m_vAngles.y);
	return std::isfinite(m_vAngles.x) && std::isfinite(m_vAngles.y);
}

// Bounded, allocation-free search of nearby static walls. Rockets have no gravity;
// player travel is estimated in HU/s over at most ten server ticks.
bool CAutoRocketJump::FindWallJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iImpact)
{
	if (!Vars::Misc::Movement::AutoRocketJump.Value || !Vars::Misc::Movement::AutoRocketJumpWalls.Value
		|| pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
		return false;

	const Vec3 origin = pLocal->m_vecOrigin();
	const Vec3 velocity = pLocal->m_vecVelocity();
	const float interval = I::GlobalVars->interval_per_tick;
	const float distance = std::clamp(Vars::Misc::Movement::AutoRocketJumpWallDistance.Value, 32.f, 160.f);
	const float offsets[] = { 0.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f };
	const auto mode = RocketJump::GetMode(Vars::Misc::Movement::AutoRocketJumpMode.Value);
	const float center = std::min(pLocal->m_vecMaxs().z, I::GameMovement->GetPlayerMaxs(true).z * pLocal->m_flModelScale()) * .5f + 10.f;
	const float heights[] = { 12.f, 0.f, -16.f, -32.f, -48.f, -64.f, center, center - 16.f, center + 16.f };
	const int heightCount = mode == RocketJump::Mode::Diagonal ? 1 : int(std::size(heights));
	CTraceFilterWorldAndPropsOnly filter(pLocal);
	CTraceFilterCollideable rocketFilter(pLocal);
	Vec3 bestAngles = {};
	float bestScore = -FLT_MAX;
	bool found = false;
	for (float offset : offsets)
	for (int height = 0; height < heightCount; ++height)
	{
		Vec3 direction; Math::AngleVectors({ 0.f, pCmd->viewangles.y + offset, 0.f }, &direction);
		CGameTrace wall = {};
		const Vec3 start = origin + Vec3(0.f, 0.f, heights[height]);
		SDK::Trace(start, start + direction * distance, MASK_SOLID, &filter, &wall);
		if (!wall.DidHit() || wall.startsolid || wall.allsolid || (wall.surface.flags & (SURF_SKY | SURF_SKY2D))
			|| !RocketJump::WallNormal(wall.plane.normal.x, wall.plane.normal.y, wall.plane.normal.z)
			|| !AimAtPoint(pLocal, pWeapon, wall.endpos + wall.plane.normal))
			continue;

		ProjectileInfo info = {};
		if (!F::ProjSim.GetInfo(pLocal, pWeapon, m_vAngles, info, ProjSimEnum::Redirect | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles)
			|| info.m_flGravity != 0.f || info.m_flVelocity <= 0.f)
			continue;
		Vec3 forward; Math::AngleVectors(info.m_vAng, &forward);
		CGameTrace impact = {};
		SDK::TraceHull(info.m_vPos, info.m_vPos + forward * (info.m_flVelocity * interval * 10.f),
			-info.m_vHull, info.m_vHull, MASK_SOLID, &rocketFilter, &impact);
		if (!impact.DidHit() || impact.startsolid || impact.allsolid || (impact.surface.flags & (SURF_SKY | SURF_SKY2D))
			|| impact.m_pEnt && impact.m_pEnt->IsPlayer()
			|| !RocketJump::WallNormal(impact.plane.normal.x, impact.plane.normal.y, impact.plane.normal.z)
			|| impact.plane.normal.Dot(wall.plane.normal) < .9f)
			continue;
		const float travel = impact.endpos.DistTo(info.m_vPos);
		const int ticks = RocketJump::ImpactTicks(travel, info.m_flVelocity, interval);
		if (!ticks) continue;
		const float time = ticks * interval;
		Vec3 future = origin + velocity * time;
		if (pLocal->m_hGroundEntity()) future.z = origin.z;
		else
		{
			const float scale = pLocal->m_flGravity();
			future.z -= .5f * SDK::GetGravity() * (scale > 0.f ? scale : 1.f) * time * time;
		}
		CGameTrace movement = {};
		SDK::TraceHull(origin, future, pLocal->m_vecMins(), pLocal->m_vecMaxs(), pLocal->SolidMask(), &filter, &movement);
		if (movement.startsolid || movement.allsolid) continue;
		future = movement.endpos;
		float score;
		if (!RateBlast(pLocal, impact, future, pLocal->m_vecMins(), pLocal->m_vecMaxs(),
			pLocal->m_vecViewOffset(), impact.plane.normal.To2D(), score)) continue;
		if (mode == RocketJump::Mode::Diagonal)
		{
			const Vec3 blast = impact.endpos + impact.plane.normal;
			if (blast.z > future.z + 24.f) continue;
			score = -blast.DistToSqr(origin) - offset * offset * .25f;
		}
		else score -= offset * offset * .0000001f;
		if (score <= bestScore) continue;
		bestScore = score; bestAngles = m_vAngles; iImpact = ticks; found = true;
	}
	if (found) m_vAngles = bestAngles;
	return found;
}

void CAutoRocketJump::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	PROJ_FRAME("AutoRocketJump", pWeapon ? pWeapon->GetWeaponID() : -1);

	if (!ShouldRun(pLocal, pWeapon, pCmd))
	{
		Reset();
		return;
	}

	if (m_bReloadWaiting) return;
	const bool bLastGrounded = m_bLastGrounded;
	const bool bCurrGrounded = m_bLastGrounded = bool(pLocal->m_hGroundEntity());
	const bool bBeggars = pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka;
	bool pendingFloor = false;
	if (m_iFrame == -1)
	{
		m_bDuckAfterLaunch = false;
		const bool full = Vars::Misc::Movement::AutoRocketJump.Value;
		const auto mode = RocketJump::GetMode(Vars::Misc::Movement::AutoRocketJumpMode.Value);
		const bool floorEligible = full || (bCurrGrounded
			? !pLocal->IsDucking() && I::GlobalVars->curtime >= pLocal->m_flDuckTimer() : pLocal->IsDucking());
		const bool canLaunch = bCurrGrounded && bBeggars ? G::Attacking == 1 : G::CanPrimaryAttack;
		// A valid floor plan owns this tick even while waiting for stable ground.
		// Never let a nearby wall steal it during that one-tick stabilization.
		const auto surface = RocketJump::SelectSurface(
			[&] { return canLaunch && floorEligible && FindFloorJump(pLocal, pWeapon, pCmd, m_iDelay); },
			[&] { return G::CanPrimaryAttack && FindWallJump(pLocal, pWeapon, pCmd, m_iDelay); });
		if (surface == RocketJump::Surface::Floor)
		{
			if (!bCurrGrounded || bLastGrounded || bBeggars)
			{
				m_iFrame = 0;
				if (!bCurrGrounded && (!full || mode == RocketJump::Mode::Diagonal)) m_iDelay = 0;
				m_bDuckAfterLaunch = full && mode != RocketJump::Mode::Diagonal;
			}
			else
			{
				pendingFloor = true;
				pCmd->buttons &= ~(IN_JUMP | IN_ATTACK | IN_ATTACK2);
			}
		}
		else if (surface == RocketJump::Surface::Wall)
		{
			m_iFrame = 0;
			m_bDuckAfterLaunch = true;
		}
		if (m_iFrame != -1)
		{
			m_bFull = full;
			m_iChoke = bCurrGrounded ? Vars::Misc::Movement::AutoRocketJumpChokeGrounded.Value : Vars::Misc::Movement::AutoRocketJumpChokeAir.Value;
		}
	}

	if (m_iFrame != -1)
	{
		m_iFrame++;
		pCmd->buttons &= ~IN_JUMP;

		if (m_iFrame == 1)
		{
			if (!bBeggars)
				pCmd->buttons |= IN_ATTACK, pCmd->buttons &= ~IN_ATTACK2;

			if (m_bFull)
			{
				G::SilentAngles = bCurrGrounded;
				SDK::FixMovement(pCmd, m_vAngles);
				pCmd->viewangles = m_vAngles;
			}
		}

		if (m_bDuckAfterLaunch && m_iFrame > 1) pCmd->buttons |= IN_DUCK;

		if (bCurrGrounded && (m_iFrame - m_iDelay == 0 || !m_iDelay))
		{
			pCmd->buttons |= IN_JUMP;
			// Keep the first shot at the muzzle height used by the planner.
			// Directed boosts crouch on subsequent commands for stronger self-push.
			if (!m_bDuckAfterLaunch || m_iFrame > 1) pCmd->buttons |= IN_DUCK;
		}

		if (m_iFrame <= m_iChoke)
			G::PSilentAngles = true, G::SilentAngles = false;

		if (m_iFrame == m_iDelay + 3)
			m_iFrame = -1;
	}

	m_bRunning = m_iFrame != -1 || pendingFloor; // reserve aim/timing while a floor launch stabilizes
}
