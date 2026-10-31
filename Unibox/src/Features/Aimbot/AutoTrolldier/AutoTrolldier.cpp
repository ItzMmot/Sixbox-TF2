#include "AutoTrolldier.h"
#include "../AimbotGlobal/AimbotGlobal.h"
#include "../../Backtrack/Backtrack.h"
#include "../../Ticks/Ticks.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../Binds/Binds.h"
#include "../AutoRocketJump/AutoRocketJump.h"

namespace
{
	Trolldier::Vector Snapshot(const Vec3& v) { return { v.x, v.y, v.z }; }
	Vec3 Native(Trolldier::Vector v) { return { v.x, v.y, v.z }; }
	float Gravity(CTFPlayer* pPlayer)
	{
		const float scale = pPlayer->m_flGravity();
		return SDK::GetGravity() * (scale > 0.f ? scale : 1.f);
	}
	bool Gardener(CTFWeaponBase* weapon)
	{
		return weapon && weapon->GetSlot() == SLOT_MELEE && weapon->GetWeaponID() == TF_WEAPON_SHOVEL
			&& uint16_t(weapon->m_iItemDefinitionIndex()) == Soldier_t_TheMarketGardener;
	}
	bool Launcher(CTFWeaponBase* weapon)
	{
		if (!weapon || weapon->GetSlot() != SLOT_PRIMARY) return false;
		const int id = weapon->GetWeaponID();
		return id == TF_WEAPON_ROCKETLAUNCHER || id == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT || id == TF_WEAPON_PARTICLE_CANNON;
	}
}

bool CAutoTrolldier::Eligible(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd) const
{
	if (!Vars::Misc::Movement::AutoTrolldier::Enabled.Value || !pLocal || !pWeapon || !pCmd || !I::GlobalVars
		|| I::EngineClient->IsPlayingDemo() || I::ClientState->IsPaused() || pCmd->weaponselect
		|| !pLocal->IsAlive() || pLocal->m_iClass() != TF_CLASS_SOLDIER || !pLocal->CanAttack()
		|| pLocal->IsAGhost() || pLocal->IsTaunting() || pLocal->IsSwimming() || pLocal->m_MoveType() != MOVETYPE_WALK
		|| pLocal->m_hGroundEntity() || !pLocal->InCond(TF_COND_BLASTJUMPING)
		|| pLocal->InCond(TF_COND_PARACHUTE_ACTIVE) || pLocal->InCond(TF_COND_HALLOWEEN_KART)
		|| pLocal->InCond(TF_COND_STUNNED) || pLocal->InCond(TF_COND_AIR_CURRENT)
		|| pLocal->m_hGrapplingHookTarget().Get() || pLocal->m_flWaterJumpTime() > 0.f
		|| !pLocal->m_vecBaseVelocity().IsZero()
		|| pWeapon->GetWeaponID() != TF_WEAPON_SHOVEL || uint16_t(pWeapon->m_iItemDefinitionIndex()) != 416
		|| pWeapon->GetSlot() != 2 || SDK::AttribHookValue(0, "crit_while_airborne", pWeapon) <= 0
		|| F::Ticks.m_bShifting || F::Ticks.m_bDoubletap || F::Ticks.m_bWarp || F::Ticks.m_bRecharge || F::Ticks.m_bAntiWarp
		|| Vars::Doubletap::Warp.Value)
		return false;
	if (Vars::Misc::Movement::AutoTrolldier::CancelOnInput.Value
		&& (G::OriginalCmd.buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
		return false;
	return true;
}

Trolldier::Trace CAutoTrolldier::TraceWorld(void* pContext, Trolldier::Vector from, Trolldier::Vector to,
	Trolldier::Vector mins, Trolldier::Vector maxs, int actor)
{
	auto* self = static_cast<CAutoTrolldier*>(pContext);
	auto* pPlayer = actor < 0 ? self->m_owner.Get() : self->m_handles[actor].Get();
	if (!pPlayer)
		return { 0.f, {}, true };
	CTraceFilterWorldAndPropsOnly filter(pPlayer);
	CGameTrace trace = {};
	SDK::TraceHull(Native(from), Native(to), Native(mins), Native(maxs), pPlayer->SolidMask(), &filter, &trace);
	return { trace.fraction, Snapshot(trace.plane.normal), trace.startsolid || trace.allsolid };
}

void CAutoTrolldier::ResetControl()
{
	m_active = false;
	m_command = -1;
	m_owner.Term(); m_weapon.Term(); m_target.Term();
	m_plan.target = -1;
	m_worldWish = {};
	m_rotationInitialized = false;
	m_pitchVelocity = m_yawVelocity = 0.f;
#ifndef TEXTMODE
	m_drawCache.Set({});
#endif
}

void CAutoTrolldier::CancelSwap()
{
	m_swapPhase = SwapPhase::Idle;
	m_swapOwner.Term(); m_launcher.Term(); m_gardener.Term();
	m_swing.Reset();
}

void CAutoTrolldier::Reset()
{
	ResetControl();
	CancelSwap();
	m_swapConsumed = false;
	m_strikeTarget.Term(); m_strikeOwner.Term(); m_strikeWeapon.Term();
	m_strikeUntil = 0.f;
}

bool CAutoTrolldier::HasCommittedStrike(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const CUserCmd* pCmd) const
{
	return Vars::Misc::Movement::AutoTrolldier::Enabled.Value && pLocal && pLocal->IsAlive() && pWeapon && pCmd && !pCmd->weaponselect
		&& m_strikeOwner.Get() == pLocal && m_strikeWeapon.Get() == pWeapon
		&& m_strikeUntil > TICKS_TO_TIME(pLocal->m_nTickBase());
}

int CAutoTrolldier::AutomaticAimType() const
{
	using namespace Vars::Aimbot::General;
	if (AimType.Value != AimTypeEnum::Off) return AimType.Value;
	// Read enabled key-bind values without pressing a physical key or toggling
	// unrelated settings. Avoid GetBind(), which copies its vector/string data.
	std::scoped_lock lock(F::Binds.m_mMutex);
	const auto& binds = F::Binds.m_vBinds;
	for (int i = 0; i < int(binds.size()); ++i)
	{
		if (!binds[i].m_bEnabled || binds[i].m_iType != BindEnum::Key) continue;
		bool allowed = true;
		int parent = binds[i].m_iParent, depth = 0;
		while (parent != DEFAULT_BIND)
		{
			if (parent < 0 || parent >= int(binds.size()) || ++depth > int(binds.size())
				|| !binds[parent].m_bEnabled || (binds[parent].m_iType != BindEnum::Key && !binds[parent].m_bActive))
			{ allowed = false; break; }
			parent = binds[parent].m_iParent;
		}
		const auto value = AimType.Map.find(i);
		if (allowed && value != AimType.Map.end() && value->second > AimTypeEnum::Off && value->second <= AimTypeEnum::SmoothVelocity)
			return value->second;
	}
	return AimTypeEnum::Silent;
}

void CAutoTrolldier::UpdateAutomation(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	using namespace Vars::Misc::Movement::AutoTrolldier;
	if (!Enabled.Value || !AutoMarketGardenerSwap.Value || !pLocal || !pWeapon || !pCmd
		|| !pLocal->IsAlive() || pLocal->m_iClass() != TF_CLASS_SOLDIER || !I::GlobalVars
		|| I::EngineClient->IsPlayingDemo() || I::ClientState->IsPaused())
	{
		CancelSwap(); m_swapConsumed = false;
		return;
	}
	const bool airborne = !pLocal->m_hGroundEntity() && pLocal->InCond(TF_COND_BLASTJUMPING);
	const float now = TICKS_TO_TIME(pLocal->m_nTickBase());
	if (pCmd->weaponselect || (CancelOnInput.Value && (G::OriginalCmd.buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT))))
	{
		CancelSwap(); m_swapConsumed = true; // Explicit player/other-feature selection wins.
		return;
	}
	if (m_swapPhase != SwapPhase::Idle && (m_swapOwner.Get() != pLocal || !m_launcher.Get() || !m_gardener.Get()))
	{
		CancelSwap(); m_swapConsumed = true;
		return;
	}
	if (m_swapPhase == SwapPhase::Idle)
	{
		if (!airborne) m_swapConsumed = false;
		if (!airborne || m_swapConsumed || !Launcher(pWeapon) || F::AutoRocketJump.IsLaunching()
			|| F::Ticks.m_bShifting || F::Ticks.m_bDoubletap || F::Ticks.m_bWarp || F::Ticks.m_bRecharge
			|| F::Ticks.m_bAntiWarp || Vars::Doubletap::Warp.Value || !pLocal->CanAttack()
			|| pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->InCond(TF_COND_PARACHUTE_ACTIVE)
			|| pLocal->IsSwimming() || pLocal->IsTaunting() || pLocal->IsAGhost()) return;
		CTFWeaponBase* melee = nullptr;
		for (auto& handle : pLocal->m_hMyWeapons())
		{
			auto* candidate = handle.Get();
			if (Gardener(candidate) && candidate->CanBeSelected()) { melee = candidate; break; }
		}
		if (!melee) return;
		m_swapOwner.Set(pLocal); m_launcher.Set(pWeapon); m_gardener.Set(melee);
		m_swapPhase = SwapPhase::SelectingGardener;
		m_swapRequested = now; m_swapConsumed = true; m_swing.Reset();
	}
	if (m_swapPhase == SwapPhase::SelectingGardener)
	{
		if (pWeapon == m_gardener.Get()) m_swapPhase = SwapPhase::HoldingGardener;
		else if (pWeapon != m_launcher.Get() || now - m_swapRequested > 1.5f)
		{ CancelSwap(); return; }
		else
		{
			pCmd->weaponselect = m_gardener.Get()->entindex(); pCmd->weaponsubtype = 0;
			pCmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
			return;
		}
	}
	if (m_swapPhase == SwapPhase::HoldingGardener)
	{
		if (pWeapon != m_gardener.Get()) { CancelSwap(); return; }
		const float smack = pWeapon->m_flSmackTime();
		if (!m_swing.Active() && m_swing.Result() == Trolldier::SwingResult::None && smack > 0.f)
			m_swing.Request(now, std::max(0.f, smack - now));
		float acknowledgment = 0.2f;
		if (auto* channel = I::EngineClient->GetNetChannelInfo())
			acknowledgment += channel->GetLatency(FLOW_INCOMING) + channel->GetLatency(FLOW_OUTGOING);
		const auto result = m_swing.Observe(now, smack, TICK_INTERVAL, acknowledgment);
		if (m_swing.Result() != Trolldier::SwingResult::None) pCmd->buttons &= ~IN_ATTACK;
		const bool finished = result == Trolldier::SwingResult::Hit || result == Trolldier::SwingResult::Unconfirmed || result == Trolldier::SwingResult::Cancelled;
		if (finished || (!airborne && !m_swing.Active()))
		{
			m_swapPhase = SwapPhase::ReturningLauncher;
			m_swapRequested = now;
		}
	}
	if (m_swapPhase == SwapPhase::ReturningLauncher)
	{
		if (pWeapon == m_launcher.Get() || now - m_swapRequested > 1.5f) { CancelSwap(); return; }
		if (!m_launcher.Get()->CanBeSelected()) return;
		pCmd->weaponselect = m_launcher.Get()->entindex(); pCmd->weaponsubtype = 0;
		pCmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
	}
}

void CAutoTrolldier::AfterAim(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (IsActive(pCmd) && !HasCommittedStrike(pLocal, pWeapon, pCmd) && !pCmd->weaponselect
		&& ((G::CanPrimaryAttack && (pCmd->buttons & IN_ATTACK)) || pWeapon->m_flSmackTime() > 0.f))
	{
		const float now = TICKS_TO_TIME(pLocal->m_nTickBase());
		const float impact = pWeapon->m_flSmackTime() > 0.f ? pWeapon->m_flSmackTime() : now + pWeapon->GetSmackDelay();
		if (std::isfinite(impact) && impact >= now && impact <= now + 5.f)
		{
			m_strikeTarget = m_target; m_strikeOwner.Set(pLocal); m_strikeWeapon.Set(pWeapon);
			m_strikeUntil = impact + 3.f * TICK_INTERVAL;
		}
	}
	if (m_swapPhase != SwapPhase::HoldingGardener || m_swapOwner.Get() != pLocal || m_gardener.Get() != pWeapon
		|| m_swing.Result() != Trolldier::SwingResult::None || pCmd->weaponselect) return;
	if (G::CanPrimaryAttack && (pCmd->buttons & IN_ATTACK) && pWeapon->m_flSmackTime() <= 0.f)
		m_swing.Request(TICKS_TO_TIME(pLocal->m_nTickBase()), pWeapon->GetSmackDelay());
}

void CAutoTrolldier::Event(IGameEvent* pEvent, uint32_t hash)
{
	if ((hash == FNV1A::Hash32Const("player_spawn") || hash == FNV1A::Hash32Const("player_changeclass"))
		&& I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid")) == I::EngineClient->GetLocalPlayer())
	{ Reset(); return; }
	if (hash == FNV1A::Hash32Const("client_disconnect") || hash == FNV1A::Hash32Const("game_newmap")
		|| hash == FNV1A::Hash32Const("teamplay_round_start")) { Reset(); return; }
	if (hash != FNV1A::Hash32Const("player_hurt") || !m_swing.Active() || !m_swapOwner.Get()) return;
	if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker")) == m_swapOwner.Get()->entindex()
		&& pEvent->GetInt("weaponid", -1) == TF_WEAPON_SHOVEL && pEvent->GetInt("damageamount") > 0)
		m_swing.ConfirmHit();
}

bool CAutoTrolldier::IsActive(const CUserCmd* pCmd) const
{
	return m_active && pCmd && m_command == pCmd->command_number && m_owner.Get() && m_weapon.Get() && m_target.Get();
}

void CAutoTrolldier::Prepare(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const bool committed = HasCommittedStrike(pLocal, pWeapon, pCmd);
	if (!committed)
	{
		m_strikeTarget.Term(); m_strikeOwner.Term(); m_strikeWeapon.Term(); m_strikeUntil = 0.f;
	}
	if (!Eligible(pLocal, pWeapon, pCmd))
	{
		ResetControl();
		return;
	}
	const auto oldTarget = m_command + 1 == pCmd->command_number && m_owner.Get() == pLocal && m_weapon.Get() == pWeapon ? m_target : CHandle<CTFPlayer>{};
	const auto previousSide = oldTarget.Get() ? m_plan.side : Trolldier::Vector{};
	m_active = false;
	m_owner.Set(pLocal); m_weapon.Set(pWeapon);
	m_command = pCmd->command_number;

	// Take one bounded settings snapshot for this command. ConfigVar values
	// already include the normal bind system's current overrides.
	static auto sv_airaccelerate = H::ConVars.FindVar("sv_airaccelerate");
	m_settings.interval = TICK_INTERVAL;
	if (!std::isfinite(m_settings.interval) || m_settings.interval <= 0.f || m_settings.interval > 0.1f
		|| !std::isfinite(Vars::Misc::Movement::AutoTrolldier::PredictionTime.Value)
		|| !std::isfinite(Vars::Misc::Movement::AutoTrolldier::MaxDistance.Value))
	{
		ResetControl();
		return;
	}
	m_settings.airAcceleration = sv_airaccelerate ? sv_airaccelerate->GetFloat() : 10.f;
	m_settings.wishSpeed = SDK::MaxSpeed(pLocal);
	m_settings.friction = pLocal->m_surfaceFriction();
	m_settings.airCap = SDK::AttribHookValue(1.f, "mod_air_control", pLocal)
		* SDK::AttribHookValue(1.f, "mod_air_control_blast_jump", pLocal) * 30.f;
	m_settings.eyeOffset = Snapshot(pLocal->m_vecViewOffset());
	const float range = SDK::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon);
	const float hull = SDK::AttribHookValue(18.f, "melee_bounds_multiplier", pWeapon);
	m_settings.reach = (range + hull) * std::max(1.f, pLocal->m_flModelScale()) - 8.f;
	const float horizon = std::clamp(Vars::Misc::Movement::AutoTrolldier::PredictionTime.Value, 0.25f, 5.f);
	m_settings.ticks = std::clamp(TIME_TO_TICKS(horizon), 1, Trolldier::MaxTicks);
	m_settings.traceBudget = std::clamp(Vars::Misc::Movement::AutoTrolldier::TraceBudget.Value, 256, 16384);
	const float pending = pWeapon->m_flSmackTime();
	const float cooldown = std::max(0.f, std::max(pWeapon->m_flNextPrimaryAttack(), pLocal->m_flNextAttack()) - TICKS_TO_TIME(pLocal->m_nTickBase()));
	const float delay = pending > 0.f ? std::max(0.f, pending - TICKS_TO_TIME(pLocal->m_nTickBase())) : pWeapon->GetSmackDelay() + cooldown;
	if (!std::isfinite(m_settings.airAcceleration) || !std::isfinite(m_settings.wishSpeed) || !std::isfinite(m_settings.friction)
		|| !std::isfinite(m_settings.airCap) || m_settings.airAcceleration < 0.f || m_settings.wishSpeed <= 0.f
		|| m_settings.friction < 0.f || m_settings.airCap <= 0.f || !std::isfinite(delay) || delay < 0.f
		|| delay > Trolldier::MaxTicks * m_settings.interval)
	{
		ResetControl();
		return;
	}
	const int impactSample = Trolldier::ImpactSample(pLocal->m_nTickBase(), m_settings.interval,
		pending > 0.f ? pending : TICKS_TO_TIME(pLocal->m_nTickBase()) + delay);
	if (!impactSample) { ResetControl(); return; }
	m_settings.strikeTick = std::max(1, impactSample - 1); // retain the pre-impact safety sample
	Trolldier::Actor local = { Snapshot(pLocal->m_vecOrigin()), Snapshot(pLocal->m_vecVelocity()),
		Snapshot(pLocal->m_vecMins() + PLAYER_ORIGIN_COMPRESSION), Snapshot(pLocal->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION), Gravity(pLocal), false };
	if (!local.Valid())
	{
		ResetControl();
		return;
	}

	const float distance = std::clamp(Vars::Misc::Movement::AutoTrolldier::MaxDistance.Value, 128.f, 3000.f);
	std::array<float, Trolldier::MaxTargets> distances = {};
	int count = 0;
	const int maxTargets = std::clamp(Vars::Misc::Movement::AutoTrolldier::MaxTargets.Value, 1, Trolldier::MaxTargets);
	for (auto* pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto* pPlayer = pEntity->As<CTFPlayer>();
		if (committed && pPlayer != m_strikeTarget.Get()) continue;
		if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsInvulnerable()
			|| pPlayer->m_iTeamNum() == pLocal->m_iTeamNum()
			|| pPlayer->IsSwimming() || pPlayer->m_MoveType() != MOVETYPE_WALK
			|| F::AimbotGlobal.ShouldIgnore(pPlayer, pLocal, pWeapon))
			continue;
		const float d = (pPlayer->m_vecOrigin() - pLocal->m_vecOrigin()).LengthSqr();
		if (!std::isfinite(d) || d > distance * distance)
			continue;
		int slot = 0;
		while (slot < count && distances[slot] <= d) ++slot;
		if (slot >= maxTargets)
			continue;
		for (int i = std::min(count, maxTargets - 1); i > slot; --i)
		{
			distances[i] = distances[i - 1]; m_targets[i] = m_targets[i - 1]; m_handles[i] = m_handles[i - 1];
		}
		distances[slot] = d;
		m_handles[slot].Set(pPlayer);
		m_targets[slot] = { Snapshot(pPlayer->m_vecOrigin()), Snapshot(pPlayer->m_vecVelocity()),
			Snapshot(pPlayer->m_vecMins() + PLAYER_ORIGIN_COMPRESSION), Snapshot(pPlayer->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION),
			Gravity(pPlayer), pPlayer->IsOnGround() };
		count = std::min(count + 1, maxTargets);
	}
	int previous = -1;
	for (int i = 0; i < count; ++i)
	{
		if (m_handles[i] == oldTarget) previous = i;
		if (!Vars::Aimbot::Melee::SwingAccountPing.Value || F::AntiCheatCompatibility.Active())
			continue;
		auto* pPlayer = m_handles[i].Get();
		const float lead = std::clamp(TICKS_TO_TIME(F::Backtrack.GetRewindTick()) - pPlayer->m_flSimulationTime(), 0.f, 0.2f);
		auto& a = m_targets[i];
		if (!a.Valid() || !std::isfinite(lead))
			continue;
		auto end = a.origin + a.velocity * lead;
		if (!a.grounded) end.z -= 0.5f * a.gravity * lead * lead;
		const auto trace = TraceWorld(this, a.origin, end, a.mins, a.maxs, i);
		if (!trace.solid)
		{
			a.origin = a.origin + (end - a.origin) * trace.fraction;
			if (!a.grounded) a.velocity.z -= a.gravity * lead;
		}
	}
	m_solver.Solve(local, m_targets, count, m_settings, TraceWorld, this, previous, m_plan, previousSide, pending > 0.f ? 1 : 3);
	if (m_plan.target < 0)
	{
		m_target.Term();
		m_rotationInitialized = false;
#ifndef TEXTMODE
		m_drawCache.Set({});
#endif
		return;
	}
	m_target = m_handles[m_plan.target];
	if (m_target != oldTarget)
		m_rotationInitialized = false;
	m_worldWish = Native(m_plan.wish * m_settings.wishSpeed);
	m_active = true;
	ApplyMovement(pCmd);
#ifndef TEXTMODE
	DrawData draw;
	if (Vars::Misc::Movement::AutoTrolldier::DrawPath.Value)
	{
		draw.count = m_plan.endTick + 1;
		draw.color = Vars::Misc::Movement::AutoTrolldier::PathColor.Value;
		draw.depth = !Vars::Misc::Movement::AutoTrolldier::PathThroughWalls.Value;
		draw.expires = GetTickCount64() + 250;
		for (int i = 0; i < draw.count; ++i)
			draw.points[i] = Native(m_plan.path[i].local) + Vec3(0.f, 0.f, 4.f);
	}
	m_drawCache.Set(draw);
#endif
}

void CAutoTrolldier::RotateCamera(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	using namespace Vars::Misc::Movement::AutoTrolldier;
	if (!IsActive(pCmd) || !pLocal)
		return;
	const int mode = Rotation.Value;
	// The melee adapter owns aim when its strike validation succeeds. Flight
	// rotations must not replace the swing angle, including a silent one.
	if (mode == RotationEnum::Off || G::Attacking || G::AimbotSteering
		|| G::AimPoint.m_iTickCount == I::GlobalVars->tickcount)
	{
		m_rotationInitialized = false;
		return;
	}
	const bool silent = mode == RotationEnum::Silent || mode == RotationEnum::LegitSilent;
	const bool legit = mode == RotationEnum::Legit || mode == RotationEnum::LegitSilent;
	if (mode < RotationEnum::Plain || mode > RotationEnum::LegitSilent)
		return;
	const int lookTick = std::min(m_plan.hitTick, 8);
	const Vec3 eye = pLocal->GetEyePosition();
	const Vec3 look = Native(m_plan.path[lookTick].local + m_settings.eyeOffset);
	if ((look - eye).LengthSqr() < 1.f)
		return;
	Vec3 angles = Math::CalcAngle(eye, look);
	if (!m_rotationInitialized || m_rotationMode != mode)
	{
		m_rotationAngles = silent && G::LastUserCmd && G::LastUserCmd->command_number + 1 == pCmd->command_number
			? G::LastUserCmd->viewangles : pCmd->viewangles;
		m_pitchVelocity = m_yawVelocity = 0.f;
		m_rotationMode = mode;
		m_rotationInitialized = true;
	}
	if (legit)
	{
		const Vec3 from = silent ? m_rotationAngles : pCmd->viewangles;
		angles.x = Trolldier::SmoothRotation(from.x, angles.x, m_settings.interval, RotationSpeed.Value, true, m_pitchVelocity);
		angles.y = Trolldier::SmoothRotation(from.y, angles.y, m_settings.interval, RotationSpeed.Value, false, m_yawVelocity);
	}
	Math::ClampAngles(angles);
	pCmd->viewangles = m_rotationAngles = angles;
	if (silent)
		G::SilentAngles = true;
	else
		I::EngineClient->SetViewAngles(angles);
}

void CAutoTrolldier::RenderWorld()
{
#ifndef TEXTMODE
	const auto draw = m_drawCache.Get();
	if (draw.count < 2 || GetTickCount64() > draw.expires)
		return;
	for (int i = 1; i < draw.count; ++i)
		H::Draw.RenderLine(draw.points[i - 1], draw.points[i], draw.color, draw.depth);
	// Small intercept marker, using the same color as the path.
	const Vec3 end = draw.points[draw.count - 1];
	H::Draw.RenderLine(end - Vec3(6.f, 0.f, 0.f), end + Vec3(6.f, 0.f, 0.f), draw.color, draw.depth);
	H::Draw.RenderLine(end - Vec3(0.f, 6.f, 0.f), end + Vec3(0.f, 6.f, 0.f), draw.color, draw.depth);
#endif
}

void CAutoTrolldier::ApplyMovement(CUserCmd* pCmd) const
{
	if (!IsActive(pCmd))
		return;
	const float yaw = Math::Deg2Rad(pCmd->viewangles.y);
	const float c = std::cos(yaw), s = std::sin(yaw);
	pCmd->forwardmove = m_worldWish.x * c + m_worldWish.y * s;
	pCmd->sidemove = m_worldWish.x * s - m_worldWish.y * c;
}

bool CAutoTrolldier::StrikePrediction(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd,
	float flDelay, Vec3& vEye, Vec3& vTargetOrigin) const
{
	if (!IsActive(pCmd) || !Eligible(pLocal, pWeapon, pCmd) || !std::isfinite(flDelay) || flDelay < 0.f)
		return false;
	auto* pTarget = m_target.Get();
	if (!pTarget || pTarget->IsDormant() || !pTarget->IsAlive() || pTarget->IsInvulnerable()
		|| pTarget->m_iTeamNum() == pLocal->m_iTeamNum()
		|| F::AimbotGlobal.ShouldIgnore(pTarget, pLocal, pWeapon))
		return false;
	if (flDelay > Trolldier::MaxTicks * m_settings.interval)
		return false;
	const bool pending = pWeapon->m_flSmackTime() > 0.f;
	const int tick = Trolldier::ImpactSample(pLocal->m_nTickBase(), m_settings.interval,
		pending ? pWeapon->m_flSmackTime() : TICKS_TO_TIME(pLocal->m_nTickBase()) + flDelay);
	if (!tick || tick > m_plan.endTick || (!pending && !Trolldier::StableStrikeWindow(m_plan, tick)))
		return false;
	vEye = Native(m_plan.path[tick].local + m_settings.eyeOffset);
	vTargetOrigin = Native(m_plan.path[tick].target);
	return true;
}
