#pragma once
#include "../../../SDK/SDK.h"

class CAutoSapper
{
private:
	bool HasSappableBuilding(CTFPlayer* pLocal, bool bCheckView);
	bool IsSeenByEnemy(CTFPlayer* pLocal, CBaseObject* pBuilding);
	CTFWeaponBase* FindWeapon(CTFPlayer* pLocal, int iEntIndex = 0);
	void Reset();

	int m_iPrevWeapon = 0;
	float m_flEquipTime = 0.f;
	bool m_bSeenSapper = false;

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CAutoSapper, AutoSapper);
