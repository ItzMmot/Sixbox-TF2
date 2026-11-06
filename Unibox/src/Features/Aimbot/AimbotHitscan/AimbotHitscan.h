#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include "../Shared/BurstPlanner.h"

struct HitscanHitbox_t
{
	const mstudiobbox_t* m_pBox = nullptr;
	std::optional<Vec3> m_vPos = std::nullopt;

	int m_iHitbox = 0;
};

class CAimbotHitscan
{
private:
	std::vector<HitscanHitbox_t> GetHitboxes(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget);
	int GetHitboxPriority(int nHitbox, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget);
	void GetHitboxInfo(HitscanHitbox_t& tHitbox, CBaseEntity* pTarget, matrix3x4* aBones = nullptr, const matrix3x4** pTransform = nullptr, Vec3* pMins = nullptr, Vec3* pMaxs = nullptr);
	void GetHullInfo(CBaseEntity* pTarget, TickRecord* pRecord = nullptr, const matrix3x4** pTransform = nullptr, Vec3* pMins = nullptr, Vec3* pMaxs = nullptr, float* pModelScale = nullptr);
	std::vector<Vec3> GetHitboxPoints(CBaseEntity* pTarget, CTFWeaponBase* pWeapon, const Vec3& vMins, const Vec3& vMaxs, int nHitbox = 0);
	int CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void OptimizePattern(Target_t& target,CTFPlayer* local,CTFWeaponBase* weapon,const std::vector<HitscanHitbox_t>& hitboxes,TickRecord* record);

	bool Aim(const Vec3& vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
	void Aim(CUserCmd* pCmd, Vec3& vAngles, int iMethod = Vars::Aimbot::General::AimType.Value);
	bool ShouldFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, const Target_t& tTarget);

	struct CrosshairRecordInfo_t
	{
		float m_flMinDist = -1.f;
		float m_flFov = -1.f;
		float m_flSimTime = -1.f;

		bool m_bInsideThisRecord = false;
	};
	CrosshairRecordInfo_t GetCrosshairRecord(const Vec3 vAngles, const Vec3 vPos, std::vector<TickRecord*> vRecords, const std::vector<const mstudiobbox_t*> vHitboxes);

	Vec3 m_vEyePos = {};
	int m_iBurstDelayCommand=-1,m_iPreparedCommand=-1,m_iAimedCommand=-1;
	Burst::Plan m_preparedBurst;
	CHandle<CTFWeaponBase> m_hPreparedWeapon;
	CHandle<CBaseEntity> m_hPreparedTarget,m_hAimedTarget;
	alignas(16) std::array<byte,65536> m_aBurstScratch{};
	alignas(16) std::array<byte,131072> m_aBurstPredictedState{};
	using BurstMotion = std::array<Vec3,Burst::MaxCommands+Burst::MaxDelay>;
	std::array<BurstMotion,Burst::MaxDelay+1> m_aBurstEyes{};
	BurstMotion m_aBurstTargets{};
	bool CaptureBurstMotion(CTFPlayer* player,int horizon,bool local,BurstMotion& output,int startDelay=0);

	matrix3x4 m_mMatrix = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
	matrix3x4 m_mHullMatrix = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };

	std::deque<TickRecord> m_vForwardRecords = {};
	void BuildForwardRecords(CTFPlayer* pTarget, TickRecord* pBaseRecord, std::vector<TickRecord*>& vRecords);

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	const Burst::Plan* PreparedBurst(int command,CTFWeaponBase* weapon,CBaseEntity*& target) const;
	bool AimedAt(int command,CBaseEntity* target) const {return m_iAimedCommand==command&&m_hAimedTarget.Get()==target;}

	void BacktrackToCrosshair(CUserCmd* pCmd);
};

ADD_FEATURE(CAimbotHitscan, AimbotHitscan);
