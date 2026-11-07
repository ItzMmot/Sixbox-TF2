#include "AutoSapper.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include "../AimbotMelee/AimbotMelee.h"
#include "../../Ticks/Ticks.h"

bool CAutoSapper::IsSeenByEnemy(CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	const int iBuilder = Vars::Aimbot::AutoSapper::IgnoreEngineer.Value ? pBuilding->m_hBuilder().GetEntryIndex() : -1;
	const Vec3 vLocalCenter = pLocal->GetCenter();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsDormant())
			continue;

		const int iIndex = pPlayer->entindex();
		if (iIndex == iBuilder)
			continue;

		const Vec3 vEnemyEye = pPlayer->GetEyePosition();
		const Vec3 vAngleTo = Math::CalcAngle(vEnemyEye, vLocalCenter);
		if (Math::CalcFov(H::Entities.GetEyeAngles(iIndex), vAngleTo) > Vars::Aimbot::AutoSapper::EnemyViewFOV.Value * 0.5f)
			continue;

		if (SDK::VisPos(pPlayer, pLocal, vEnemyEye, vLocalCenter))
			return true;
	}

	return false;
}

bool CAutoSapper::HasSappableBuilding(CTFPlayer* pLocal, bool bCheckView)
{
	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, nullptr))
			continue;

		auto pBuilding = pEntity->As<CBaseObject>();
		if (pBuilding->m_bHasSapper() || !pBuilding->IsInValidTeam())
			continue;

		Vec3 vPoint;
		if (!F::AimbotMelee.FindNearestBuildPoint(pBuilding, pLocal, vPoint))
			continue;

		if (!F::AimbotGlobal.InAimFOV(Math::CalcFov(vLocalAngles, Math::CalcAngle(vLocalPos, vPoint))))
			continue;

		if (bCheckView && Vars::Aimbot::AutoSapper::CheckEnemyView.Value && IsSeenByEnemy(pLocal, pBuilding))
			continue;

		return true;
	}

	return false;
}

void CAutoSapper::Reset()
{
	m_iPrevWeapon = 0;
	m_flEquipTime = 0.f;
	m_bSeenSapper = false;
}

// the server looks the weapon up by classname and subtype, and the sapper's subtype is its object type rather than 0
static int GetSapperSubType(CTFWeaponBase* pSapper)
{
	static int nOffset = U::NetVars.GetNetVar("CTFWeaponBuilder", "m_iObjectType");
	if (!nOffset)
		return OBJ_ATTACHMENT_SAPPER;

	const int iType = *reinterpret_cast<int*>(uintptr_t(pSapper) + nOffset);
	return iType >= 0 && iType < BUILDER_INVALID_OBJECT ? iType : OBJ_ATTACHMENT_SAPPER;
}

CTFWeaponBase* CAutoSapper::FindWeapon(CTFPlayer* pLocal, int iEntIndex)
{
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto pSwap = pLocal->GetWeaponFromSlot(i);
		if (!pSwap)
			continue;

		if (!iEntIndex)
		{
			if (pSwap->GetWeaponID() == TF_WEAPON_BUILDER)
				return pSwap;
		}
		else if (pSwap->entindex() == iEntIndex && pSwap->CanBeSelected())
			return pSwap;
	}
	return nullptr;
}

void CAutoSapper::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Aimbot::AutoSapper::AutoEquip.Value || !Vars::Aimbot::General::AimType.Value
		|| pLocal->m_iClass() != TF_CLASS_SPY || pCmd->weaponselect)
		return Reset();

	if (pWeapon->GetWeaponID() == TF_WEAPON_BUILDER)
	{
		if (!m_iPrevWeapon)
			return;

		m_bSeenSapper = true;
		if (HasSappableBuilding(pLocal, false))
			return;

		if (Vars::Aimbot::AutoSapper::SwitchBack.Value)
		{
			if (auto pPrev = FindWeapon(pLocal, m_iPrevWeapon))
				pCmd->weaponselect = pPrev->entindex(), pCmd->weaponsubtype = 0;
		}
		return Reset();
	}

	// the switch to the sapper takes a few ticks to show up, after that it leaving means the sapper is done or the player switched away
	if (m_iPrevWeapon && (m_bSeenSapper || I::GlobalVars->curtime - m_flEquipTime > 1.f))
		Reset();

	auto pSapper = FindWeapon(pLocal);
	if (!pSapper || !HasSappableBuilding(pLocal, true))
		return;

	if (!m_iPrevWeapon)
	{
		m_iPrevWeapon = pWeapon->entindex();
		m_flEquipTime = I::GlobalVars->curtime;
	}
	pCmd->weaponselect = pSapper->entindex();
	pCmd->weaponsubtype = GetSapperSubType(pSapper);
}
