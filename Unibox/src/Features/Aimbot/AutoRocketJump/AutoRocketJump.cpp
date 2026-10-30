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
	m_bRunning = m_bReloadWaiting = m_bWallJump = m_bLastGrounded = false;
	m_owner.Term(); m_weapon.Term();
	m_reload.Reset();
}

// Run before engine prediction: the reload-interrupt command must not jump first.
void CAutoRocketJump::Prepare(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!ShouldRun(pLocal, pWeapon, pCmd)) { Reset(); return; }
	if (m_owner.Get() != pLocal || m_weapon.Get() != pWeapon) Reset();
	m_owner.Set(pLocal); m_weapon.Set(pWeapon);
	if (m_iFrame != -1) return;

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

bool CAutoRocketJump::SetAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_vAngles = pCmd->viewangles;
	if (!Vars::Misc::Movement::AutoRocketJump.Value)
		return true;

	ProjectileInfo tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, tProjInfo, ProjSimEnum::NoRandomAngles)
		|| !F::ProjSim.Initialize(tProjInfo, false))
		return false;

	Vec3 vOrigin = pLocal->m_vecOrigin();
	Vec3 vLocalPos = pLocal->GetShootPos();
	Vec3 vOffset = tProjInfo.m_vPos - vLocalPos; vOffset.x = 0; //vOffset.y *= -1;

	Vec3 vPoint;
	if (pLocal->IsOnGround())
	{
		float flOffset = sqrtf(2 * powf(vOffset.y, 2.f) + powf(vOffset.z, 2.f));
		Vec3 vWishVel = { pCmd->forwardmove, pCmd->sidemove, 0.f }, vDir;
		if (vWishVel.IsZero())
			vDir = { 0.f, m_vAngles.y, 0.f };
		else
		{
			Vec3 vWishAng = Math::VectorAngles(vWishVel);
			vDir = { 0.f, m_vAngles.y - vWishAng.y, 0.f };
		}
		Vec3 vForward; Math::AngleVectors(vDir, &vForward);
		vPoint = pLocal->m_vecOrigin() - vForward * flOffset;
	}
	else
	{
		//float flOffset = pLocal->m_vecMaxs().x;
		float flOffset = sqrtf(2 * powf(vOffset.y, 2.f) + powf(vOffset.z, 2.f));
		bool bShouldReturn = true;
		MoveStorage tMoveStorage;
		if (F::MoveSim.Initialize(pLocal, tMoveStorage, false))
		{
			for (int n = 1; n < 10; n++)
			{
				F::MoveSim.RunTick(tMoveStorage);
				if (!pLocal->IsOnGround() || pLocal->IsSwimming())
					continue;

				Vec3 vForward = tMoveStorage.m_MoveData.m_vecVelocity.Normalized2D();
				vPoint = tMoveStorage.m_MoveData.m_vecAbsOrigin - vForward * flOffset; //- Vec3(0, 0, 20);
				bShouldReturn = false;
				break;
			}
		}
		F::MoveSim.Restore(tMoveStorage);
		if (bShouldReturn)
			return false;
	}

	return AimAtPoint(pLocal, pWeapon, vPoint);
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
	CTraceFilterWorldAndPropsOnly filter(pLocal);
	Vec3 bestAngles = {};
	float bestScore = FLT_MAX;
	bool found = false;
	for (float offset : offsets)
	{
		Vec3 direction; Math::AngleVectors({ 0.f, pCmd->viewangles.y + offset, 0.f }, &direction);
		CGameTrace wall = {};
		const Vec3 start = origin + Vec3(0.f, 0.f, 12.f);
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
			-info.m_vHull, info.m_vHull, MASK_SOLID, &filter, &impact);
		if (!impact.DidHit() || impact.startsolid || impact.allsolid || (impact.surface.flags & (SURF_SKY | SURF_SKY2D))
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
		const Vec3 blast = impact.endpos + impact.plane.normal;
		const Vec3 mins = future + pLocal->m_vecMins(), maxs = future + pLocal->m_vecMaxs();
		const Vec3 nearest = { std::clamp(blast.x, mins.x, maxs.x), std::clamp(blast.y, mins.y, maxs.y), std::clamp(blast.z, mins.z, maxs.z) };
		const float radius = TF_ROCKET_RADIUS_FOR_RJS - 12.f;
		if (blast.z > future.z + 24.f || blast.DistToSqr(nearest) > radius * radius
			|| !SDK::VisPosWorld(pLocal, pLocal, blast, future + pLocal->m_vecViewOffset(), MASK_SHOT))
			continue;
		const float score = blast.DistToSqr(origin) + offset * offset * .25f;
		if (score >= bestScore) continue;
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
	if (m_iFrame == -1)
	{
		m_bWallJump = false;
		if (G::CanPrimaryAttack && FindWallJump(pLocal, pWeapon, pCmd, m_iDelay))
		{
			m_iFrame = 0;
			m_bWallJump = m_bFull = true;
			m_iChoke = bCurrGrounded ? Vars::Misc::Movement::AutoRocketJumpChokeGrounded.Value : Vars::Misc::Movement::AutoRocketJumpChokeAir.Value;
		}
		else
		{
			const bool bDuck = pLocal->IsDucking();
			if (bCurrGrounded ? bDuck || I::GlobalVars->curtime < pLocal->m_flDuckTimer() : !bDuck)
				return;
		}
	}

	bool bValidWeapon = false, bBeggars = pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka;
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON: bValidWeapon = true;
		}
	}

	if (bValidWeapon && m_iFrame == -1 && (bCurrGrounded && bBeggars ? G::Attacking == 1 : G::CanPrimaryAttack)
		&& SetAngles(pLocal, pWeapon, pCmd))
	{
		bool bWillHit = false;
		if (Vars::Misc::Movement::AutoRocketJump.Value || Vars::Misc::Movement::AutoCTap.Value)
		{
			MoveStorage tMoveStorage;
			ProjectileInfo tProjInfo = {};

			bool bProjSimSetup = F::ProjSim.GetInfo(pLocal, pWeapon, m_vAngles, tProjInfo, ProjSimEnum::Redirect | ProjSimEnum::InitCheck | ProjSimEnum::NoRandomAngles) && F::ProjSim.Initialize(tProjInfo);
			bool bMoveSimSetup = F::MoveSim.Initialize(pLocal, tMoveStorage, false); // do move sim after to not mess with proj sim
			if (bMoveSimSetup && bProjSimSetup)
			{
				int iSkip = bCurrGrounded ? Vars::Misc::Movement::AutoRocketJumpSkipGround.Value : Vars::Misc::Movement::AutoRocketJumpSkipAir.Value;

				CGameTrace trace = {};
				CTraceFilterCollideable filter(pLocal);

				Vec3 vNew = F::ProjSim.GetOrigin();
				for (int n = 1; n < 10; n++)
				{
					F::MoveSim.RunTick(tMoveStorage);
					if (n <= iSkip)
						continue;

					F::ProjSim.RunTick(tProjInfo);

					Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
					SDK::Trace(vOld, vNew, MASK_SOLID, &filter, &trace);
					if (trace.DidHit())
					{
						if (!pLocal->IsOnGround() || pLocal->IsSwimming())
							break;

						auto fWillHit = [](CTFPlayer* pLocal, const Vec3& vOrigin, const Vec3& vPoint)
						{
							const Vec3 vOriginal = pLocal->GetAbsOrigin();
							pLocal->SetAbsOrigin(vOrigin);
							Vec3 vPos; pLocal->m_Collision()->CalcNearestPoint(vPoint, &vPos);
							pLocal->SetAbsOrigin(vOriginal);

							float flRadiusSqr = powf(TF_ROCKET_RADIUS_FOR_RJS, 2);
							return vPoint.DistToSqr(vPos) < flRadiusSqr && SDK::VisPosWorld(pLocal, pLocal, vPoint, vOrigin + pLocal->m_vecViewOffset(), MASK_SHOT);
						};

						bWillHit = fWillHit(pLocal, tMoveStorage.m_MoveData.m_vecAbsOrigin, trace.endpos + trace.plane.normal);
						if (bWillHit)
						{
							m_iDelay = n;
							if (Vars::Misc::Movement::AutoRocketJumpTimingOffset.Value && n > Vars::Misc::Movement::AutoRocketJumpApplyAbove.Value)
								m_iDelay = std::max(m_iDelay + Vars::Misc::Movement::AutoRocketJumpTimingOffset.Value, 0);

							if (G::CanPrimaryAttack)
							{
								SDK::Output("Auto jump", std::format("Ticks to hit: {} ({})", m_iDelay, n).c_str(), { 255, 0, 0 }, Vars::Debug::Logging.Value);
								if (Vars::Debug::Info.Value)
								{
									G::PathStorage.clear(); G::BoxStorage.clear(); G::LineStorage.clear();
									tProjInfo.m_vPath.push_back(trace.endpos + trace.plane.normal);

									G::PathStorage.emplace_back(tMoveStorage.m_vPath, I::GlobalVars->curtime + 5.f, Color_t(255, 255, 255), Vars::Visuals::Path::StyleEnum::Line);
									G::BoxStorage.emplace_back(tMoveStorage.m_MoveData.m_vecAbsOrigin, pLocal->m_vecMins(), pLocal->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 255, 255), Color_t(0, 0, 0, 0), true);
									G::PathStorage.emplace_back(tProjInfo.m_vPath, I::GlobalVars->curtime + 5.f, Color_t(255, 150, 150), Vars::Visuals::Path::StyleEnum::Line);
									G::BoxStorage.emplace_back(tProjInfo.m_vPath.back(), Vec3::Get(-1), Vec3::Get(1), Vec3() /*Math::VectorAngles(trace.plane.normal)*/, I::GlobalVars->curtime + 5.f, Color_t(255, 150, 150), Color_t(0, 0, 0, 0), true);
								}
							}
						}

						break;
					}

					// this will just prevent shooting speedshots when projectile isn't fast enough
					//if (!bCurrGrounded && !bLastGround && bCurGround)
					//	break;
				}
			}
			F::MoveSim.Restore(tMoveStorage);
		}

		if (bWillHit)
		{
			if (bCurrGrounded && bCurrGrounded == bLastGrounded)
				m_iFrame = 0;
			else if (!bCurrGrounded)
				m_iFrame = m_iDelay = 0;

			if (m_iFrame != -1)
			{
				m_bFull = Vars::Misc::Movement::AutoRocketJump.Value;
				m_iChoke = bCurrGrounded ? Vars::Misc::Movement::AutoRocketJumpChokeGrounded.Value : Vars::Misc::Movement::AutoRocketJumpChokeAir.Value;

			}
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

		if (m_bWallJump && m_iFrame > 1) pCmd->buttons |= IN_DUCK;

		if (bCurrGrounded && (m_iFrame - m_iDelay == 0 || !m_iDelay))
		{
			pCmd->buttons |= IN_JUMP;
			// A one-tick wall shot was traced with the current muzzle height.
			// Defer automated ducking until after that shot, like other wall shots.
			if (!m_bWallJump || m_iFrame > 1) pCmd->buttons |= IN_DUCK;
		}

		if (m_iFrame <= m_iChoke)
			G::PSilentAngles = true, G::SilentAngles = false;

		if (m_iFrame == m_iDelay + 3)
			m_iFrame = -1;
	}

	m_bRunning = m_iFrame != -1; // prevent stuff like antiaim messing with timing
}
