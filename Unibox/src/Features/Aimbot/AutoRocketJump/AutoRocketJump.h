#pragma once
#include "../../../SDK/SDK.h"
#include "JumpPolicy.h"

class CAutoRocketJump
{
private:
	bool ShouldRun(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool SetAngles(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool AimAtPoint(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3& vPoint);
	bool FindWallJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iImpact);

	Vec3 m_vAngles = {};
	bool m_bFull = false;
	int m_iFrame = -1;
	int m_iDelay = 0;
	int m_iChoke = 0;
	bool m_bRunning = false;
	bool m_bReloadWaiting = false, m_bWallJump = false, m_bLastGrounded = false;
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
