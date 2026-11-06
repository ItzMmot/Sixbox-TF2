#include "AimbotMelee.h"
#include "../SpyKnife/SpyKnife.h"
#include "../Shared/AimGeometry.h"
#include "../AutoTrolldier/AutoTrolldier.h"

#include "../Aimbot.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../Ticks/Ticks.h"
#include "../../Visuals/Visuals.h"
#include "../../NavBot/BotUtils.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../Configs/Configs.h"
#include <cstdio>

namespace
{
	struct KnifeStatus {bool active=false,geometry=false;int hit=-1,targets=0,result=0;const char* stage="entry";} s_knife;
	struct KnifeTraceScope
	{
		CTFWeaponBase* weapon;CUserCmd* command;bool active;
		KnifeTraceScope(CTFWeaponBase* w,CUserCmd* c):weapon(w),command(c),active(w->GetWeaponID()==TF_WEAPON_KNIFE
			&&(Vars::Aimbot::General::AimType.Value||w->As<CTFKnife>()->m_bReadyToBackstab()))
		{if(active) {s_knife={};s_knife.active=true;}}
		~KnifeTraceScope()
		{
			if(!active) return;s_knife.active=false;
			static ULONGLONG next=0;static int records=0;const auto now=GetTickCount64();
			if(now<next||records>=64) return;next=now+2000;++records;
			char path[MAX_PATH],line[512];
			const int pathSize=std::snprintf(path,sizeof(path),"%sSpyKnife.txt",F::Configs.m_sConfigPath.c_str());
			if(pathSize<0||pathSize>=int(sizeof(path))) return;
			const int size=std::snprintf(line,sizeof(line),
				"spy-fix-1 cmd=%d stage=%s raised=%d ready=%d aim=%d autoshoot=%d targets=%d trace_target=%d geometry=%d result=%d requested_attack=%d smack=%.5f\r\n",
				G::OriginalCmd.command_number,s_knife.stage,int(weapon->As<CTFKnife>()->m_bReadyToBackstab()),int(G::CanPrimaryAttack),
				Vars::Aimbot::General::AimType.Value,int(Vars::Aimbot::General::AutoShoot.Value),s_knife.targets,s_knife.hit,int(s_knife.geometry),
				s_knife.result,int(bool(command->buttons&IN_ATTACK)),weapon->m_flSmackTime());
			if(size<=0||size>=int(sizeof(line))) return;
			const HANDLE file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
			if(file==INVALID_HANDLE_VALUE) return;DWORD written;WriteFile(file,line,DWORD(size),&written,nullptr);CloseHandle(file);
		}
	};
}

static inline bool AimFriendlyBuilding(CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	int iCurrMetal = pLocal->m_iMetalCount();

	bool bShouldRepair = false;
	switch (pBuilding->GetClassID())
	{
	case ETFClassID::CObjectSentrygun:
		if (Vars::Aimbot::AutoEngie::AutoRepair.Value & Vars::Aimbot::AutoEngie::AutoRepairEnum::Sentry)
		{
			int iShells, iMaxShells, iRockets, iMaxRockets; pBuilding->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);
			if (iCurrMetal && (iShells < iMaxShells || iRockets < iMaxRockets))
				return true;
			bShouldRepair = true;
		}
		break;
	case ETFClassID::CObjectDispenser:
		if (Vars::Aimbot::AutoEngie::AutoRepair.Value & Vars::Aimbot::AutoEngie::AutoRepairEnum::Dispenser)
			bShouldRepair = true;
		break;
	case ETFClassID::CObjectTeleporter:
		if (Vars::Aimbot::AutoEngie::AutoRepair.Value & Vars::Aimbot::AutoEngie::AutoRepairEnum::Teleporter)
			bShouldRepair = true;
		break;
	default:
		break;
	}

	if (bShouldRepair && ((iCurrMetal && pBuilding->m_iHealth() != pBuilding->m_iMaxHealth()) || pBuilding->m_bHasSapper()))
		return true;

	if (iCurrMetal && Vars::Aimbot::AutoEngie::AutoUpgrade.Value && !pBuilding->m_bMiniBuilding())
	{
		int iUpgradeLevel = pBuilding->m_iUpgradeLevel();

		int iMaxLevel = 0;
		switch (pBuilding->GetClassID())
		{
		case ETFClassID::CObjectSentrygun:
			if (!(Vars::Aimbot::AutoEngie::AutoUpgrade.Value & Vars::Aimbot::AutoEngie::AutoUpgradeEnum::Sentry))
				return false;
			iMaxLevel = Vars::Aimbot::AutoEngie::AutoUpgradeSentryLVL.Value;
			break;
		case ETFClassID::CObjectDispenser:
			if (!(Vars::Aimbot::AutoEngie::AutoUpgrade.Value & Vars::Aimbot::AutoEngie::AutoUpgradeEnum::Dispenser))
				return false;
			iMaxLevel = Vars::Aimbot::AutoEngie::AutoUpgradeDispenserLVL.Value;
			break;
		case ETFClassID::CObjectTeleporter:
			if (!(Vars::Aimbot::AutoEngie::AutoUpgrade.Value & Vars::Aimbot::AutoEngie::AutoUpgradeEnum::Teleporter))
				return false;
			iMaxLevel = Vars::Aimbot::AutoEngie::AutoUpgradeTeleporterLVL.Value;
			break;
		default:
			break;
		}

		if (iUpgradeLevel < iMaxLevel)
			return true;
	}
	return false;
}

static inline std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
	{
		auto eGroup = !SDK::FriendlyFire() || Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Team ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;
		if (Vars::Aimbot::Melee::WhipTeam.Value &&
			!SDK::FriendlyFire() && SDK::AttribHookValue(0, "speed_buff_ally", pWeapon) > 0)
			eGroup = EntityEnum::PlayerAll;

		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			// Melee target selection uses collision bounds, not hitscan bones.
			// A knife can already be touching a valid victim without a bone record.
			const Vec3 mins=pEntity->m_vecOrigin()+pEntity->m_vecMins();
			const Vec3 maxs=pEntity->m_vecOrigin()+pEntity->m_vecMaxs();
			Vec3 direction;Math::AngleVectors(vLocalAngles,&direction);
			const float entry=AimPolicy::RayBox(AimGeometry::Point(vLocalPos),AimGeometry::Point(direction),
				AimGeometry::Point(mins),AimGeometry::Point(maxs),10000.f);
			Vec3 vPos=entry>=0?vLocalPos+direction*(entry+.01f):vLocalPos.Clamp(mins,maxs);
			if(vPos.DistToSqr(vLocalPos)<1e-6f) vPos=(mins+maxs)*.5f;
			const Vec3 vAngleTo=Math::CalcAngle(vLocalPos,vPos);
			const float flFOVTo=entry>=0?0.f:Math::CalcFov(vLocalAngles,vAngleTo);
			if(flFOVTo>F::AimbotGlobal.GetAimFOV()) continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			if (bTeam && !SDK::FriendlyFire())
				iPriority = 0;
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	{
		auto eGroup = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
			eGroup = EntityEnum::BuildingEnemy;
		bool bWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH, bSapper = SDK::AttribHookValue(0, "set_dmg_apply_to_sapper", pWeapon);
		if ((Vars::Aimbot::AutoEngie::AutoUpgrade.Value || Vars::Aimbot::AutoEngie::AutoRepair.Value) && (bWrench || bSapper))
			eGroup = eGroup != EntityEnum::Invalid ? EntityEnum::BuildingAll : EntityEnum::BuildingTeam;
		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && (bWrench && !AimFriendlyBuilding(pLocal, pEntity->As<CBaseObject>()) || bSapper && !pEntity->As<CBaseObject>()->m_bHasSapper()))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			int iPriority = 0;
			if (bTeam)
			{
				int iOwner = pEntity->As<CBaseObject>()->m_hBuilder().GetEntryIndex();
				switch (Vars::Aimbot::Healing::HealPriority.Value)
				{
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeFriends:
					if (iOwner == I::EngineClient->GetLocalPlayer() || H::Entities.IsFriend(iOwner))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeParty:
					if (H::Entities.InParty(iOwner))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeTeam:
					iPriority = std::numeric_limits<int>::max();
				}
			}

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

static inline Vec3 GetLastNetworkedOrigin(CBaseEntity* pEntity)
{
	if (pEntity->m_iv_vecOrigin().m_LastNetworkedValue)
		return *pEntity->m_iv_vecOrigin().m_LastNetworkedValue;
	return pEntity->m_vecOrigin();
}

std::vector<int> CAimbotMelee::GetTimeMods()
{
	std::vector<int> vTimeMods = {};
	switch (Vars::Aimbot::Melee::SwingValidateMode.Value)
	{
	case Vars::Aimbot::Melee::SwingValidateModeEnum::Both:
		if (m_iSimulatedTicks != m_iSwingTicks)
			vTimeMods.push_back(m_iSimulatedTicks);
		[[fallthrough]];
	case Vars::Aimbot::Melee::SwingValidateModeEnum::Swing:
		vTimeMods.push_back(m_iSwingTicks);
		break;
	case Vars::Aimbot::Melee::SwingValidateModeEnum::Simulated:
		vTimeMods.push_back(m_iSimulatedTicks);
		break;
	}
	return vTimeMods;
}

static inline int GetSwingTime(CTFWeaponBase* pWeapon, bool bVar = true)
{
	return pWeapon->GetWeaponID() == TF_WEAPON_KNIFE ? 0
		: bVar ? Vars::Aimbot::Melee::SwingTicks.Value
		: ceilf(pWeapon->GetSmackDelay() / TICK_INTERVAL);
}

void CAimbotMelee::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, const std::vector<Target_t>& vTargets)
{
	m_mRecordMap.clear(); m_mPaths.clear();
	if(pWeapon->m_flSmackTime()>0.f||!Vars::Aimbot::General::AutoShoot.Value) m_swingDeferral.Reset();
	m_bNextWindow=false;m_aNextValid.fill(false);
	m_vEyePos = pLocal->GetShootPos();
	m_flRange = pWeapon->GetSwingRange();
	m_iSimulatedTicks = GetSwingTime(pWeapon), m_iSwingTicks = GetSwingTime(pWeapon, false);
	m_iDoubletapTicks = pWeapon->GetWeaponID()==TF_WEAPON_KNIFE?0:F::Ticks.GetTicks(pWeapon);
	m_bShouldSwing = m_iDoubletapTicks <= m_iSwingTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity();
	m_bSimulatedLocal = false;

	if ((!Vars::Aimbot::Melee::SwingPrediction.Value || !m_iSimulatedTicks) && !m_iDoubletapTicks || !G::CanPrimaryAttack || pWeapon->m_flSmackTime() > 0.f)
	{
		m_iSimulatedTicks = m_iSwingTicks = 0;
		return;
	}

	const bool bAccountPing = Vars::Aimbot::Melee::SwingAccountPing.Value;
	std::unordered_map<int, MoveStorage> mMoveStorage;
	std::unordered_map<int, int> mTargetTicks;

	auto& localMove=mMoveStorage[pLocal->entindex()];
	if(!F::MoveSim.Initialize(pLocal,localMove,false,!m_iDoubletapTicks,false)||localMove.m_bFailed)
	{
		F::MoveSim.Restore(localMove);
		m_iSimulatedTicks=m_iSwingTicks=m_iDoubletapTicks=0;m_bShouldSwing=true;
		return;
	}
	for (auto& tTarget : vTargets)
	{
		const bool bNetworked = bAccountPing && tTarget.m_pEntity->IsPlayer();
		const Vec3 vTargetOrigin = bNetworked ? GetLastNetworkedOrigin(tTarget.m_pEntity) : tTarget.m_pEntity->m_vecOrigin();
		F::MoveSim.Initialize(tTarget.m_pEntity, mMoveStorage[tTarget.m_pEntity->entindex()], false, true, false, bNetworked ? &vTargetOrigin : nullptr);
	}

	auto fStepTarget = [&](const Target_t& tTarget)
	{
		const int iIndex = tTarget.m_pEntity->entindex();
		auto& tMoveStorage = mMoveStorage[iIndex];
		if (tMoveStorage.m_bFailed)
			return;

		F::MoveSim.RunTick(tMoveStorage);
		const int iTick = ++mTargetTicks[iIndex];
		if (Vars::Aimbot::Melee::SwingPredictLag.Value && !tMoveStorage.m_bPredictNetworked)
			return;

		Vec3 vOrigin = Vars::Aimbot::Melee::SwingPredictLag.Value ? tMoveStorage.m_vPredictedOrigin : tMoveStorage.m_MoveData.m_vecAbsOrigin;
		m_mRecordMap[iIndex].emplace_front(
			tTarget.m_pEntity->m_flSimulationTime() + TICKS_TO_TIME(iTick), vOrigin, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs()
		);
	};

	int iMax = std::max(m_iSimulatedTicks, m_iDoubletapTicks), iTicks = iMax; bool bSwung = false;
	Vec3 vLocalOrigin = mMoveStorage[pLocal->entindex()].m_MoveData.m_vecAbsOrigin;
	for (int i = 0; i < iTicks; i++)
	{
		{
			auto& tMoveStorage = mMoveStorage[pLocal->entindex()];
			if (!bSwung && (!m_iDoubletapTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity() || iMax - i <= m_iSwingTicks))
			{
				iTicks = std::min(i + m_iSwingTicks, iMax), bSwung = true;
				if (!m_iSwingTicks)
					break;

				if (pLocal->InCond(TF_COND_SHIELD_CHARGE))
				{
					pLocal->RemoveCond(TF_COND_SHIELD_CHARGE);
					tMoveStorage.m_MoveData.m_flMaxSpeed = tMoveStorage.m_MoveData.m_flClientMaxSpeed = SDK::MaxSpeed(pLocal);
					pLocal->m_flMaxspeed() = tMoveStorage.m_MoveData.m_flMaxSpeed;
				}
			}
			if (m_iDoubletapTicks && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
				F::Ticks.AntiWarp(pLocal, pCmd->viewangles.y, tMoveStorage.m_MoveData.m_flForwardMove, tMoveStorage.m_MoveData.m_flSideMove, iMax - i - 1);

			F::MoveSim.RunTick(tMoveStorage);

			vLocalOrigin = tMoveStorage.m_MoveData.m_vecAbsOrigin;
			m_bSimulatedLocal = true;
		}

		if (i < m_iSimulatedTicks - m_iDoubletapTicks)
		{
			for (auto& tTarget : vTargets)
				fStepTarget(tTarget);
		}
	}

	if (bAccountPing)
	{
		const auto vTimeMods = GetTimeMods();
		const int iMaxExtra = TIME_TO_TICKS(0.2f);
		for (auto& tTarget : vTargets)
		{
			if (!tTarget.m_pEntity->IsPlayer())
				continue;

			const int iRewound = TIME_TO_TICKS(tTarget.m_pEntity->m_flSimulationTime()) + mTargetTicks[tTarget.m_pEntity->entindex()];
			int iExtra = 0;
			for (int iTimeMod : vTimeMods)
				iExtra = std::max(iExtra, F::Backtrack.GetRewindTick(-TICKS_TO_TIME(iTimeMod)) - iRewound);

			for (int i = 0; i < std::min(iExtra, iMaxExtra); i++)
				fStepTarget(tTarget);
		}
	}
	m_vEyePos =vLocalOrigin + pLocal->m_vecViewOffset();
	m_flRange = pWeapon->GetSwingRange();

	if (Vars::Visuals::Prediction::SwingLines.Value && Vars::Visuals::Prediction::PlayerPath.Value)
	{
		for (auto& [iIndex, tMoveStorage] : mMoveStorage)
			m_mPaths[iIndex] = tMoveStorage.m_vPath;

		const bool bAlwaysDraw = !Vars::Aimbot::General::AutoShoot.Value || Vars::Debug::Info.Value;
		if (bAlwaysDraw)
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			for (auto& vPath : m_mPaths | std::views::values)
			{
				float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value;
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(vPath, !flDuration ? -int(vPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
				if (Vars::Colors::PlayerPath.Value.a)
					G::PathStorage.emplace_back(vPath, !flDuration ? -int(vPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
			}
		}
	}

	if(Vars::Aimbot::Melee::SwingMargin.Value&&m_bSimulatedLocal&&m_iSimulatedTicks>0&&!m_iDoubletapTicks&&pWeapon->m_flSmackTime()<0)
	{
		auto& own=mMoveStorage[pLocal->entindex()];
		if(!own.m_bFailed)
		{
			F::MoveSim.RunTick(own,false);
			m_vNextEyePos=own.m_MoveData.m_vecAbsOrigin+pLocal->m_vecViewOffset();m_bNextWindow=true;
			for(auto& target:vTargets)
			{
				const int index=target.m_pEntity->entindex();
				if(index<=0||index>=int(m_aNextDelta.size())) continue;
				auto& move=mMoveStorage[index];
				if(move.m_bFailed) continue;
				const Vec3 before=move.m_vPredictedOrigin;
				F::MoveSim.RunTick(move,false);
				m_aNextDelta[index]=move.m_vPredictedOrigin-before;m_aNextValid[index]=true;
			}
		}
	}
	for (auto& tMoveStorage : mMoveStorage | std::views::values)
		F::MoveSim.Restore(tMoveStorage);
}

bool CAimbotMelee::TryReadyKnife(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command)
{
	if(weapon->GetWeaponID()!=TF_WEAPON_KNIFE||!weapon->As<CTFKnife>()->m_bReadyToBackstab()
		||!weapon->As<CTFKnife>()->m_bKnifeExists()||!Vars::Aimbot::General::AutoShoot.Value||!G::CanPrimaryAttack||command->weaponselect
		||!(Vars::Aimbot::General::Target.Value&Vars::Aimbot::General::TargetEnum::Players)) return false;
	const float scale=(std::max)(1.f,local->m_flModelScale());
	const float range=SDK::AttribHookValue(weapon->GetSwingRange(),"melee_range_multiplier",weapon)*scale;
	const float hull=SDK::AttribHookValue(18.f,"melee_bounds_multiplier",weapon)*scale;
	if(!std::isfinite(range)||!std::isfinite(hull)||range<=0||hull<0) return false;
	const Vec3 eye=local->GetShootPos();Vec3 forward;Math::AngleVectors(command->viewangles,&forward);
	CGameTrace trace={};CTraceFilterHitscan filter(local);
	SDK::TraceHull(eye,eye+forward*range,{}, {},MASK_SOLID,&filter,&trace);
	if(Trolldier::HullFallback(trace.fraction,trace.startsolid,trace.allsolid))
		SDK::TraceHull(eye,eye+forward*range,Vec3::Get(-hull),Vec3::Get(hull),MASK_SOLID,&filter,&trace);
	auto* entity=trace.m_pEnt;
	if(s_knife.active) {s_knife.stage="ready-trace";s_knife.hit=entity?entity->entindex():-1;}
	if(!entity||!entity->IsPlayer()||trace.allsolid||entity->m_iTeamNum()==local->m_iTeamNum()
		||F::AimbotGlobal.ShouldIgnore(entity,local,weapon)) return false;
	auto* victim=entity->As<CTFPlayer>();
	if(!victim->IsAlive()||victim->IsDormant()||victim->IsAGhost()
		||SDK::AttribHookValue(0,"cannot_be_backstabbed",victim)) return false;
	if(Vars::Aimbot::Melee::IgnoreRazorback.Value)
	{
		CBaseEntity* items[64]{};CUtlVector<CBaseEntity*> sources(items,64);
		if(SDK::AttribHookValue(0,"set_blockbackstab_once",victim,&sources))
			for(int i=0;i<sources.Count();++i) if(sources[i]&&sources[i]->ShouldDraw()) return false;
	}
	if(Vars::Aimbot::Melee::AutoBackstab.Value)
	{
		const Vec3 ownerCenter=local->GetAbsOrigin()+(local->m_vecMins()+local->m_vecMaxs())*.5f;
		const Vec3 targetCenter=victim->GetAbsOrigin()+(victim->m_vecMins()+victim->m_vecMaxs())*.5f;
		Vec3 targetForward;Math::AngleVectors(victim->GetEyeAngles(),&targetForward);
		const bool valid=AimPolicy::BackstabGeometry(AimGeometry::Point(targetCenter-ownerCenter),
			AimGeometry::Point(forward),AimGeometry::Point(targetForward));
		if(s_knife.active) {s_knife.stage="ready-geometry";s_knife.geometry=valid;}
		if(!valid) return false;
	}
	// The ready flag is only a hint: the trace and geometry above must match
	// today's victim. No historical yaw, bones, or swing credit can veto this hit.
	command->buttons|=IN_ATTACK;
	if(s_knife.active) {s_knife.stage="ready-attack";s_knife.result=1;}
	G::Attacking=SDK::IsAttacking(local,weapon,command,true);
	if(G::Attacking) F::Aimbot.m_eRanType=EWeaponType::MELEE;
	G::AimTarget={victim->entindex(),I::GlobalVars->tickcount};
	G::AimPoint={trace.endpos,I::GlobalVars->tickcount};
	return true;
}

bool CAimbotMelee::CanBackstab(CBaseEntity* pTarget, CTFPlayer* pLocal, Vec3 vEyeAngles)
{
	if (!pTarget->IsPlayer() || pTarget->m_iTeamNum() == pLocal->m_iTeamNum())
		return false;

	if (Vars::Aimbot::Melee::IgnoreRazorback.Value)
	{
		CBaseEntity* items[64]{};
		CUtlVector<CBaseEntity*> itemList(items,64);
		int iBackstabShield = SDK::AttribHookValue(0, "set_blockbackstab_once", pTarget, &itemList);
		if (iBackstabShield && itemList.Count())
		{
			CBaseEntity* pEntity = itemList.Element(0);
			if (pEntity && pEntity->ShouldDraw())
				return false;
		}
	}

	Vec3 vEyePos = m_vEyePos;
	const float flCompDist = PLAYER_ORIGIN_COMPRESSION / 2;
	const float flSqCompDist = 0.0884f;

	if (auto pCmd = G::CurrentUserCmd;
		!m_bSimulatedLocal && pCmd->viewangles != vEyeAngles && G::CanPrimaryAttack)
	{
		CUserCmd tOldCmd = *pCmd;
		Vec3 vOldAngles = I::EngineClient->GetViewAngles();
		int iOldAttacking = G::Attacking;
		bool bOldSilent = G::PSilentAngles;
		F::EnginePrediction.End(pLocal, pCmd);

		G::Attacking = true;
		Aim(pCmd, vEyeAngles);
		F::Ticks.Start(pLocal, pCmd);
		vEyePos = pLocal->GetShootPos();
		F::EnginePrediction.End(pLocal, pCmd);

		*pCmd = tOldCmd;
		I::EngineClient->SetViewAngles(vOldAngles);
		G::Attacking = iOldAttacking;
		G::PSilentAngles = bOldSilent;
		F::Ticks.Start(pLocal, pCmd);
	}

	Vec3 vToTarget = (pTarget->GetAbsOrigin() - vEyePos).To2D();
	const float flDist = vToTarget.Normalize();
	if (flDist < flSqCompDist)
		return false;

	const float flExtra = 2.f * flCompDist / flDist;
	float flPosVsTargetViewMinDot = 0.f + 0.0031f + flExtra;
	float flPosVsOwnerViewMinDot = 0.5f + flExtra;
	float flViewAnglesMinDot = -0.3f + 0.0031f;

	auto fTestDots = [&](Vec3 vTargetAngles)
	{
		Vec3 vOwnerForward; Math::AngleVectors(vEyeAngles, &vOwnerForward);
		vOwnerForward.Normalize2D();

		Vec3 vTargetForward; Math::AngleVectors(vTargetAngles, &vTargetForward);
		vTargetForward.Normalize2D();

		const float flPosVsTargetViewDot = vToTarget.Dot(vTargetForward);
		const float flPosVsOwnerViewDot = vToTarget.Dot(vOwnerForward);
		const float flViewAnglesDot = vTargetForward.Dot(vOwnerForward);

		return flPosVsTargetViewDot > flPosVsTargetViewMinDot && flPosVsOwnerViewDot > flPosVsOwnerViewMinDot && flViewAnglesDot > flViewAnglesMinDot;
	};

	Vec3 vTargetAngles = { 0.f, H::Entities.GetEyeAngles(pTarget->entindex()).y, 0.f };
	if (!(Vars::Aimbot::Melee::BackstabFlags.Value & Vars::Aimbot::Melee::BackstabFlagsEnum::AccountPing))
	{
		if (!fTestDots(vTargetAngles))
			return false;
	}
	else
	{
		if (Vars::Aimbot::Melee::BackstabFlags.Value & Vars::Aimbot::Melee::BackstabFlagsEnum::DoubleTest && !fTestDots(vTargetAngles))
			return false;

		vTargetAngles.y += H::Entities.GetDeltaAngles(pTarget->entindex()).y;
		if (!fTestDots(vTargetAngles))
			return false;
	}

	return true;
}

int CAimbotMelee::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	float flRange = SDK::AttribHookValue(m_flRange, "melee_range_multiplier", pWeapon);
	float flHull = SDK::AttribHookValue(18, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	if (pWeapon->GetWeaponID() == TF_WEAPON_WRENCH && tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
	{
		flRange = 70;
		flHull = 18;
	}
	Vec3 vSwingMins = { -flHull, -flHull, -flHull };
	Vec3 vSwingMaxs = { flHull, flHull, flHull };
	auto& vSimRecords = m_mRecordMap[tTarget.m_pEntity->entindex()];

	std::vector<TickRecord*> vRecords = {};
	bool currentFallback=false,currentBones=false;
	if (F::Backtrack.GetRecords(tTarget.m_pEntity, vRecords))
	{
		const auto vTimeMods = GetTimeMods();
		for (auto& record : vSimRecords) vRecords.push_back(&record);
		for (auto timeMod : vTimeMods)
			vRecords=F::Backtrack.GetValidRecords(vRecords,pLocal,true,-TICKS_TO_TIME(timeMod));
	}
	if(vRecords.empty())
	{
		// History may exist but contain no valid rewind pose. Melee collision
		// only needs current bounds; missing historical bones must not veto it.
		currentFallback=true;
		auto& record=F::Backtrack.m_tRecord;
		record={tTarget.m_pEntity->m_flSimulationTime(),tTarget.m_pEntity->m_vecOrigin(),
			tTarget.m_pEntity->m_vecMins(),tTarget.m_pEntity->m_vecMaxs()};
		if(!vSimRecords.empty())
		{
			const auto& predicted=vSimRecords.front();
			record.m_flSimTime=predicted.m_flSimTime;record.m_vOrigin=predicted.m_vOrigin;
			record.m_vMins=predicted.m_vMins;record.m_vMaxs=predicted.m_vMaxs;
		}
		if(Vars::Visuals::Hitbox::BonesEnabled.Value&Vars::Visuals::Hitbox::BonesEnabledEnum::OnShot)
			currentBones=tTarget.m_pEntity->SetupBones(record.m_aBones,MAXSTUDIOBONES,BONE_USED_BY_ANYTHING,tTarget.m_pEntity->m_flSimulationTime());
		vRecords.push_back(&record);
	}

	CGameTrace trace = {};
	CTraceFilterHitscan filter(pLocal);
	for (auto pRecord : vRecords)
	{

		tTarget.m_vPos = m_vEyePos.Clamp(pRecord->m_vOrigin + pRecord->m_vMins, pRecord->m_vOrigin + pRecord->m_vMaxs);
		if (Vars::Aimbot::Melee::AutoBackstab.Value && pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
			tTarget.m_vPos.x = pRecord->m_vOrigin.x, tTarget.m_vPos.y = pRecord->m_vOrigin.y;
		Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		if (!F::AimbotGlobal.ShouldAimAtAngle(tTarget.m_vAngleTo))
			continue;

		Vec3 vRestoreOrigin = tTarget.m_pEntity->GetAbsOrigin();
		Vec3 vRestoreMins = tTarget.m_pEntity->m_vecMins();
		Vec3 vRestoreMaxs = tTarget.m_pEntity->m_vecMaxs();

		tTarget.m_pEntity->SetAbsOrigin(pRecord->m_vOrigin);
		tTarget.m_pEntity->m_vecMins() = pRecord->m_vMins + PLAYER_ORIGIN_COMPRESSION;
		tTarget.m_pEntity->m_vecMaxs() = pRecord->m_vMaxs - PLAYER_ORIGIN_COMPRESSION;

		Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo, &vForward);
		Vec3 vTraceEnd = m_vEyePos + vForward * flRange;

		SDK::TraceHull(m_vEyePos, vTraceEnd, {}, {}, MASK_SOLID, &filter, &trace);
		bool bReturn = trace.m_pEnt == tTarget.m_pEntity;
		if (!bReturn && Trolldier::HullFallback(trace.fraction,trace.startsolid,trace.allsolid))
		{
			SDK::TraceHull(m_vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			bReturn = trace.m_pEnt == tTarget.m_pEntity;
		}

		if (bReturn && Vars::Aimbot::Melee::AutoBackstab.Value && pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
			bReturn = CanBackstab(tTarget.m_pEntity, pLocal, tTarget.m_vAngleTo);

		tTarget.m_pEntity->SetAbsOrigin(vRestoreOrigin);
		tTarget.m_pEntity->m_vecMins() = vRestoreMins;
		tTarget.m_pEntity->m_vecMaxs() = vRestoreMaxs;

		if (bReturn)
		{
			const int index=tTarget.m_pEntity->entindex();
			if(m_bNextWindow&&index>0&&index<int(m_aNextDelta.size())&&m_aNextValid[index]
				&&Vars::Aimbot::General::AutoShoot.Value&&pWeapon->m_flSmackTime()<0)
			{
				const Vec3 mins=pRecord->m_vOrigin+pRecord->m_vMins,maxs=pRecord->m_vOrigin+pRecord->m_vMaxs;
				const float margin=flRange+flHull-AimPolicy::BoxDistance(AimGeometry::Point(m_vEyePos),AimGeometry::Point(mins),AimGeometry::Point(maxs));
				const float next=flRange+flHull-AimPolicy::BoxDistance(AimGeometry::Point(m_vNextEyePos),
					AimGeometry::Point(mins+m_aNextDelta[index]),AimGeometry::Point(maxs+m_aNextDelta[index]));
				const float drift=(std::max)(2.f,(m_vNextEyePos-m_vEyePos).Length()*.15f);
				if(margin>0&&!AimPolicy::PreferWindow(margin,next,drift))
				{
					struct RestoreWindow
					{
						CBaseEntity* entity;Vec3 origin,mins,maxs;
						~RestoreWindow() {entity->SetAbsOrigin(origin);entity->m_vecMins()=mins;entity->m_vecMaxs()=maxs;}
					} restore{tTarget.m_pEntity,tTarget.m_pEntity->GetAbsOrigin(),tTarget.m_pEntity->m_vecMins(),tTarget.m_pEntity->m_vecMaxs()};
					tTarget.m_pEntity->SetAbsOrigin(pRecord->m_vOrigin+m_aNextDelta[index]);
					tTarget.m_pEntity->m_vecMins()=pRecord->m_vMins+PLAYER_ORIGIN_COMPRESSION;
					tTarget.m_pEntity->m_vecMaxs()=pRecord->m_vMaxs-PLAYER_ORIGIN_COMPRESSION;
					const Vec3 point=m_vNextEyePos.Clamp(mins+m_aNextDelta[index],maxs+m_aNextDelta[index]);
					Vec3 angle;Aim(G::CurrentUserCmd->viewangles,Math::CalcAngle(m_vNextEyePos,point),angle);
					Vec3 forward;Math::AngleVectors(angle,&forward);
					CGameTrace hit={};
					SDK::TraceHull(m_vNextEyePos,m_vNextEyePos+forward*flRange,{}, {},MASK_SOLID,&filter,&hit);
					if(hit.m_pEnt!=tTarget.m_pEntity&&Trolldier::HullFallback(hit.fraction,hit.startsolid,hit.allsolid))
						SDK::TraceHull(m_vNextEyePos,m_vNextEyePos+forward*flRange,vSwingMins,vSwingMaxs,MASK_SOLID,&filter,&hit);
					if(hit.m_pEnt==tTarget.m_pEntity&&!hit.startsolid&&!hit.allsolid&&F::AimbotGlobal.ShouldAimAtAngle(angle)
						&&m_swingDeferral.Wait(tTarget.m_pEntity->As<IHandleEntity>()->GetRefEHandle().ToInt(),G::OriginalCmd.command_number)) return 2;
				}

			}
			tTarget.m_pRecord = !currentFallback||currentBones?pRecord:nullptr;
			tTarget.m_bBacktrack = !currentFallback&&tTarget.m_iTargetType == TargetEnum::Player;

			return true;
		}
		else switch (Vars::Aimbot::General::AimType.Value)
		{
		case Vars::Aimbot::General::AimTypeEnum::Smooth:
		case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
		case Vars::Aimbot::General::AimTypeEnum::Assistive:
		case Vars::Aimbot::General::AimTypeEnum::Legit:
		{
			auto vAngle = Math::CalcAngle(m_vEyePos, tTarget.m_vPos);

			Math::AngleVectors(vAngle, &vForward);
			vTraceEnd = m_vEyePos + vForward * flRange;

			SDK::TraceHull(m_vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			if (trace.m_pEnt == tTarget.m_pEntity)
				return 2;
		}
		}
	}

	return false;
}

bool CAimbotMelee::Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut, int iMethod)
{

	bool bReturn = false;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Legit:
		vOut = vCurAngle;
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
		vOut = vCurAngle.LerpAngle(vToAngle, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		float flMouseDelta = vMouseDelta.Length2DSqr(), flTargetDelta = vTargetDelta.Length2DSqr();
		vTargetDelta = vTargetDelta.Normalized() * sqrtf(std::min(flMouseDelta, flTargetDelta));
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	}

	if (iMethod != Vars::Aimbot::General::AimTypeEnum::Silent || F::AntiCheatCompatibility.Active())
		Math::ClampAngles(vOut);
	return bReturn;
}

void CAimbotMelee::Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod)
{
	bool bUnsure = F::Ticks.IsTimingUnsure();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAngles;
		I::EngineClient->SetViewAngles(vAngles);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::PSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Legit:
	{
		auto pLocal = H::Entities.GetLocal();
		if (pLocal && G::AimPoint.m_iTickCount == I::GlobalVars->tickcount)
		{
			F::BotUtils.LookLegit(pLocal, pCmd, G::AimPoint.m_vOrigin, false);
			vAngles = pCmd->viewangles;
			if (G::AimbotSteering)
				return;
			Vec3 vOldView = I::EngineClient->GetViewAngles();
			Vec3 vDelta = vAngles.DeltaAngle(vOldView);
			if (std::fabs(vDelta.x) > 0.01f || std::fabs(vDelta.y) > 0.01f || std::fabs(vDelta.z) > 0.01f)
				G::AimbotSteering = true;
		}
		break;
	}
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngles);
		pCmd->viewangles = vAngles;
		G::SilentAngles = true;
	}
}

static inline void DrawVisuals(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, Target_t& tTarget, std::unordered_map<int, std::vector<Vec3>>& mPaths)
{
	G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
	G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

	bool bPath = Vars::Visuals::Prediction::SwingLines.Value && Vars::Visuals::Prediction::PlayerPath.Value;

	bool bBoxes = Vars::Visuals::Hitbox::BonesEnabled.Value & Vars::Visuals::Hitbox::BonesEnabledEnum::OnShot;
	bool bRealPath = Vars::Visuals::Prediction::RealPath.Value;
	if (bPath   || bBoxes || bRealPath)
	{
		if (pCmd->buttons & IN_ATTACK && G::CanPrimaryAttack && pWeapon->m_flSmackTime() < 0.f)
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			if (bPath)
			{
				float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value;
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[pLocal->entindex()], !flDuration ? -int(mPaths[pLocal->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], !flDuration ? -int(mPaths[tTarget.m_pEntity->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
				}
				if (Vars::Colors::PlayerPath.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[pLocal->entindex()], !flDuration ? -int(mPaths[pLocal->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], !flDuration ? -int(mPaths[tTarget.m_pEntity->entindex()].size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
				}
			}
			if (int iSwingTime = GetSwingTime(pWeapon, false); bRealPath && iSwingTime)
			{
				F::Aimbot.Store(pLocal, iSwingTime);
				F::Aimbot.Store(tTarget.m_pEntity, iSwingTime);
			}
		}
		if (G::Attacking == 1)
		{

			if (bBoxes&&tTarget.m_pRecord)
			{
				auto vBoxes = F::Visuals.GetHitboxes(tTarget.m_pRecord->m_aBones, tTarget.m_pEntity->As<CBaseAnimating>());
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());

			}
		}
	}
}

std::pair<float, float> CAimbotMelee::GetClosestRecord(std::vector<TickRecord*> vRecords)
{
	float flSimTime = -1.f;
	float flMaxDist = 300.f;
	TickRecord* pBest = nullptr;
	for (auto pRecord : vRecords)
	{
		float flDist = pRecord->m_vOrigin.DistTo(m_vEyePos);
		if (flDist < flMaxDist)
		{
			flMaxDist = flDist;
			flSimTime = pRecord->m_flSimTime;
		}
	}
	return { flMaxDist, flSimTime };
}

void CAimbotMelee::BacktrackToCrosshair(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	float flBestSimTime = -1.f;
	float flMaxDist = 300.f;
	m_vEyePos = pLocal->GetShootPos();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!pEntity->As<CTFPlayer>()->IsAlive()
			|| pEntity->As<CTFPlayer>()->IsAGhost()
			|| pEntity->As<CTFPlayer>()->IsInvulnerable()
			|| pEntity->GetAbsOrigin().DistTo(m_vEyePos) > 1000.f)
			continue;

		std::vector<TickRecord*> vRecords;
		if (!F::Backtrack.GetRecords(pEntity, vRecords))
			continue;

		vRecords = F::Backtrack.GetValidRecords(vRecords);
		auto tInfo = GetClosestRecord(vRecords);
		if (tInfo.first < flMaxDist)
		{
			flBestSimTime = tInfo.second;
			flMaxDist = tInfo.first;
		}
	}

	if (flBestSimTime == -1.f)
		return;

	pCmd->tick_count = TIME_TO_TICKS(flBestSimTime) + TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
}

void CAimbotMelee::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	KnifeTraceScope diagnostic(pWeapon,pCmd);
	const bool committed = F::AutoTrolldier.HasCommittedStrike(pLocal, pWeapon, pCmd);
	if (F::AutoTrolldier.IsActive(pCmd) || committed)
	{
		const bool automatic = Vars::Misc::Movement::AutoTrolldier::AutoAimbot.Value;
		const float smack = pWeapon->m_flSmackTime();
		const bool pending = smack > 0.f;
		if (automatic && !pending) pCmd->buttons &= ~IN_ATTACK;
		if (F::AutoTrolldier.AwaitingSwing() && !pending) return;
		if (!automatic && (!Vars::Aimbot::General::AimType.Value
			|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players))) return;
		Trolldier::AimSettingsGuard aimSettings(Vars::Aimbot::General::AimType.Value, Vars::Aimbot::General::AutoShoot.Value,
			Vars::Aimbot::General::Target.Value, Vars::Aimbot::General::LeadAndRestrict.Value, automatic,
			automatic ? F::AutoTrolldier.AutomaticAimType() : Vars::Aimbot::General::AimType.Value,
			Vars::Aimbot::General::TargetEnum::Players);
		auto* pTarget = F::AutoTrolldier.GetStrikeTarget(pLocal, pWeapon, pCmd);
		if (!pTarget || pTarget->IsDormant() || !pTarget->IsAlive() || pTarget->IsInvulnerable()
			|| pTarget->m_iTeamNum() == pLocal->m_iTeamNum()
			|| F::AimbotGlobal.ShouldIgnore(pTarget, pLocal, pWeapon) || (!G::CanPrimaryAttack && !pending)) return;

		const float now = TICKS_TO_TIME(pLocal->m_nTickBase());
		const bool due = pending && now > smack;
		Vec3 eye = pLocal->GetShootPos(), origin = pTarget->m_vecOrigin();
		// Forecasts authorize a NEW swing. The actual impact uses the current
		// predicted shooter and an authoritative target snapshot, even if the
		// steering solver lost its future intercept during this committed swing.
		if (!pending && !F::AutoTrolldier.StrikePrediction(pLocal, pWeapon, pCmd, pWeapon->GetSmackDelay(), eye, origin)) return;
		if (pending && !due)
		{
			Vec3 predictedEye, predictedOrigin;
			if (F::AutoTrolldier.StrikePrediction(pLocal, pWeapon, pCmd, std::max(0.f, smack - now), predictedEye, predictedOrigin))
			{ eye = predictedEye; origin = predictedOrigin; }
		}

		const float scale = std::max(1.f, pLocal->m_flModelScale());
		const float range = SDK::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon) * scale;
		const float hull = SDK::AttribHookValue(18.f, "melee_bounds_multiplier", pWeapon) * scale;
		if (!std::isfinite(range) || !std::isfinite(hull) || range <= 2.f || hull <= 3.f) return;
		const Vec3 mins = origin + pTarget->m_vecMins() + PLAYER_ORIGIN_COMPRESSION;
		const Vec3 maxs = origin + pTarget->m_vecMaxs() - PLAYER_ORIGIN_COMPRESSION;
		if (mins.x > maxs.x || mins.y > maxs.y || mins.z > maxs.z) return;
		const auto interiorPoint = Trolldier::InteriorPoint({ eye.x, eye.y, eye.z },
			{ mins.x, mins.y, mins.z }, { maxs.x, maxs.y, maxs.z });
		const Vec3 center = (mins + maxs) * .5f;
		const Vec3 points[] = { { interiorPoint.x, interiorPoint.y, interiorPoint.z }, center,
			{ center.x, center.y, mins.z + (maxs.z - mins.z) * .7f },
			{ center.x, center.y, mins.z + (maxs.z - mins.z) * .3f } };
		Vec3 angles, point;
		bool hit = false;
		{
			struct RestoreTarget
			{
				CTFPlayer* player;
				Vec3 origin, mins, maxs;
				~RestoreTarget()
				{
					player->SetAbsOrigin(origin);
					player->m_vecMins() = mins;
					player->m_vecMaxs() = maxs;
				}
			} restore{ pTarget, pTarget->GetAbsOrigin(), pTarget->m_vecMins(), pTarget->m_vecMaxs() };
			pTarget->SetAbsOrigin(origin);
			pTarget->m_vecMins() += PLAYER_ORIGIN_COMPRESSION;
			pTarget->m_vecMaxs() -= PLAYER_ORIGIN_COMPRESSION;
			for (const auto& candidate : points)
			{
				Aim(pCmd->viewangles, Math::CalcAngle(eye, candidate), angles);
				if (!F::AimbotGlobal.ShouldAimAtAngle(angles)) continue;
				if (pending && !due) { point = candidate; hit = true; break; }
				Vec3 forward; Math::AngleVectors(angles, &forward);
				const float safeRange = pending ? range : range - 2.f;
				const float safeHull = pending ? hull : hull - 3.f;
				const Vec3 end = eye + forward * safeRange;
				CTraceFilterHitscan filter(pLocal);
				CGameTrace trace = {};
				SDK::TraceHull(eye, end, {}, {}, MASK_SOLID, &filter, &trace);
				hit = trace.m_pEnt == pTarget && !trace.startsolid && !trace.allsolid;
				// Native melee only tries its hull after an unobstructed ray.
				// A wall or a different player hit by the ray owns that strike.
				if (!hit && Trolldier::HullFallback(trace.fraction, trace.startsolid, trace.allsolid))
				{
					SDK::TraceHull(eye, end, { -safeHull, -safeHull, -safeHull }, { safeHull, safeHull, safeHull }, MASK_SOLID, &filter, &trace);
					hit = trace.m_pEnt == pTarget && !trace.startsolid && !trace.allsolid;
				}
				if (hit) { point = candidate; break; }
			}
		}
		if (!hit) return;
		if (!pending && Vars::Aimbot::General::AutoShoot.Value && G::CanPrimaryAttack)
			pCmd->buttons |= IN_ATTACK;
		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (G::Attacking) F::Aimbot.m_eRanType = EWeaponType::MELEE;
		if (due && !F::AntiCheatCompatibility.Active())
		{
			// Match the very snapshot traced above, like the regular melee aim.
			// This also matters at 0 ping: interpolation still rewinds victims.
			const float simTime = pTarget->m_flSimulationTime();
			if (std::isfinite(simTime) && std::abs(TICKS_TO_TIME(F::Backtrack.GetRewindTick()) - simTime) < .2f)
				pCmd->tick_count = TIME_TO_TICKS(simTime) + TIME_TO_TICKS(F::Backtrack.GetFakeInterp());
		}
		G::AimTarget = { pTarget->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { point, I::GlobalVars->tickcount };
		const int method = Vars::Aimbot::General::AimType.Value;
		if (pending && (method == Vars::Aimbot::General::AimTypeEnum::Silent || method == Vars::Aimbot::General::AimTypeEnum::Plain))
		{
			SDK::FixMovement(pCmd, angles);
			pCmd->viewangles = angles;
			if (method == Vars::Aimbot::General::AimTypeEnum::Silent) G::SilentAngles = true;
			else I::EngineClient->SetViewAngles(angles);
		}
		else Aim(pCmd, angles);
		return;
	}

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	if (pWeapon->m_flSmackTime() > 0.f && !iRealAimType && iLastAimType)
		Vars::Aimbot::General::AimType.Value = iLastAimType;
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

	if (F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && pWeapon->m_flSmackTime() < 0.f)
	{
		if(s_knife.active) s_knife.stage="aim-disabled";
		return;
	}

	m_mRecordMap.clear(); m_mPaths.clear();
	m_iDoubletapTicks = F::Ticks.GetTicks(pWeapon);

	if(!F::SpyKnife.Handles(pLocal,pWeapon)&&TryReadyKnife(pLocal,pWeapon,pCmd)) return;

	if (RunSapper(pLocal, pWeapon, pCmd))
		return;

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionMelee.Value);
	if(s_knife.active) {s_knife.stage="target-selection";s_knife.targets=int(vTargets.size());}
	if (vTargets.empty())
	{
		F::SpyKnife.Run(pLocal,pWeapon,pCmd,vTargets);
		return;
	}

	if(F::SpyKnife.Run(pLocal,pWeapon,pCmd,vTargets))
	{
		if(s_knife.active) {s_knife.stage=F::SpyKnife.Active()?"knife-automation":"precise-contact";s_knife.result=G::Attacking==1?1:0;}
		return;
	}

	UpdateInfo(pLocal, pWeapon, pCmd, vTargets);
	for (auto& tTarget : vTargets)
	{
		const auto iResult = CanHit(tTarget, pLocal, pWeapon);
		if(s_knife.active) {s_knife.stage="can-hit";s_knife.result=iResult;}
		if (!iResult) continue;
		if (iResult == 2)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (Vars::Aimbot::General::AutoShoot.Value && (pWeapon->GetWeaponID()==TF_WEAPON_KNIFE||pWeapon->m_flSmackTime() < 0.f))
		{
			if (m_bShouldSwing)
				pCmd->buttons |= IN_ATTACK;
			if (m_iDoubletapTicks)
				F::Ticks.m_bDoubletap = true;
		}

		if (G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true))
			F::Aimbot.m_eRanType = EWeaponType::MELEE;
		if (G::Attacking == 1)
		{
			if (tTarget.m_bBacktrack)
				pCmd->tick_count = TIME_TO_TICKS(tTarget.m_pRecord->m_flSimTime) + TIME_TO_TICKS(F::Backtrack.GetFakeInterp());

		}
		else
		{
			m_vEyePos = pLocal->GetShootPos();
			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		}
		DrawVisuals(pLocal, pWeapon, pCmd, tTarget, m_mPaths);

		Aim(pCmd, tTarget.m_vAngleTo);
		break;
	}
}

static inline int GetAttachment(CBaseObject* pBuilding, int i)
{
	int iAttachment = pBuilding->GetBuildPointAttachmentIndex(i);
	if (pBuilding->IsSentrygun() && pBuilding->m_iUpgradeLevel() > 1)
		iAttachment = 3;
	return iAttachment;
}

bool CAimbotMelee::FindNearestBuildPoint(CBaseObject* pBuilding, CTFPlayer* pLocal, Vec3& vPoint)
{
	bool bFoundPoint = false;

	m_vEyePos = pLocal->GetShootPos();
	static auto tf_obj_max_attach_dist = H::ConVars.FindVar("tf_obj_max_attach_dist");
	float flNearestPoint = tf_obj_max_attach_dist->GetFloat();

	for (int i = 0; i < pBuilding->GetNumBuildPoints(); i++)
	{
		Vector vOrigin;
		if (pBuilding->GetAttachment(GetAttachment(pBuilding, i), vOrigin))
		{
			if (!SDK::VisPos(pLocal, pBuilding, m_vEyePos, vOrigin))
				continue;

			float flDist = (vOrigin - pLocal->m_vecOrigin()).Length();
			if (flDist < flNearestPoint)
			{
				flNearestPoint = flDist;
				vPoint = vOrigin;
				bFoundPoint = true;
			}
		}
	}

	return bFoundPoint;
}

bool CAimbotMelee::RunSapper(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (pWeapon->GetWeaponID() != TF_WEAPON_BUILDER)
		return false;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	std::vector<Target_t> vTargets;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
			continue;

		auto pBuilding = pEntity->As<CBaseObject>();
		if (pBuilding->m_bHasSapper() || !pBuilding->IsInValidTeam())
			continue;

		Vec3 vPoint;
		if (!FindNearestBuildPoint(pBuilding, pLocal, vPoint))
			continue;

		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
		const float flDistTo = vLocalPos.DistToSqr(vPoint);
		if (!F::AimbotGlobal.InAimFOV(flFOVTo))
			continue;

		vTargets.emplace_back(pBuilding, TargetEnum::Unknown, vPoint, vAngleTo, flFOVTo, flDistTo);
	}
	F::AimbotGlobal.SortTargetsPre(vTargets, Vars::Aimbot::General::TargetSelectionMeleeEnum::Distance);
	if (vTargets.empty())
		return true;

	auto& tTarget = vTargets.front();

	bool bShouldAim = true;
	if (Vars::Aimbot::General::AutoShoot.Value)
		pCmd->buttons |= IN_ATTACK;
	else
		bShouldAim = pCmd->buttons & IN_ATTACK;
	if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		bShouldAim &= !I::ClientState->chokedcommands && F::Ticks.CanChoke(true);

	if (bShouldAim)
	{
		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		G::Attacking = true;

		Aim(pCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		tTarget.m_vAngleTo.x = pCmd->viewangles.x;
		Aim(pCmd, tTarget.m_vAngleTo);
	}

	return true;
}
