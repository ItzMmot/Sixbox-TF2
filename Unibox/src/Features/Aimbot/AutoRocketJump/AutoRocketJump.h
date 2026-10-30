#pragma once
#include "../../../SDK/SDK.h"
#include "JumpPolicy.h"

class CAutoRocketJump
{
private:
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool FindFloorJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iImpact);
	bool AimAtPoint(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& vPoint);
	bool FindWallJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iImpact);
	bool RateBlast(CTFPlayer* pLocal, const CGameTrace& impact, const Vec3& origin,
		const Vec3& mins, const Vec3& maxs, const Vec3& view, const Vec3& wish, float& score);

	Vec3 m_vAngles = {};
	bool m_bFull = false;
	int m_iFrame = -1;
	int m_iDelay = 0;
	int m_iChoke = 0;
	bool m_bRunning = false;
	bool m_bReloadWaiting = false, m_bDuckAfterLaunch = false, m_bLastGrounded = false;
	CHandle<CTFPlayer> m_owner;
	CHandle<CTFWeaponBase> m_weapon;
	RocketJump::ReloadGate m_reload;

public:
	void Prepare(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool IsRunning() { return m_bRunning; }
	bool IsLaunching() const { return m_iFrame != -1; }
};

ADD_FEATURE(CAutoRocketJump, AutoRocketJump);
