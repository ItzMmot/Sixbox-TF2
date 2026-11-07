#pragma once
#include "../Shared/SeamGeometry.h"
#include "../Shared/SplashWindow.h"
#include "../Shared/StickySearch.h"
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "ProjProfiler.h"

struct PasstimeGoalInfo;

Enum(PointFlags, None = 0, Regular = 1 << 0, Lob = 1 << 1)
Enum(PointType, Direct, Geometry, Air)
Enum(CalculateFlags, None = 0, TwoPass = 1 << 0, SetupClip = 1 << 1, AccountDrag = 1 << 2, LobAngle = 1 << 3, Accuracy = TwoPass | SetupClip | AccountDrag)
Enum(CalculateResult, Pending, Good, Time, Bad)
Enum(ZombiePhase, Idle, Charge, Windup, Burst, Tail)
Enum(PasstimePhase, Idle, Aim, Hold)
Enum(PasstimeKind, None, Goal, Pass)

struct Info_t
{
	CTFPlayer* m_pLocal = nullptr;
	CTFWeaponBase* m_pWeapon = nullptr;
	Target_t* m_pTarget = nullptr;
	CBaseEntity* m_pProjectile = nullptr;

	Vec3 m_vLocalEye = {};
	Vec3 m_vTargetEye = {};

	float m_flLatency = 0.f;
	Vec3 m_vHull = {};
	Vec3 m_vOffset = {};
	Vec3 m_vAngFix = {};
	float m_flVelocity = 0.f;
	float m_flGravity = 0.f;
	float m_flRadius = 0.f;
	float m_flRadiusTime = 0.f;
	float m_flBoundsTime = 0.f;
	float m_flOffsetTime = 0.f;
	int m_iSplashRestrict = 0;
	int m_iArmTime = 0;
	float m_flNormalOffset = 0.f;
	bool m_bIgnoreTiming = false;

	const SpecialProjectile_t* m_pSpecial = nullptr;
	Vec3 m_vVelAdd = {};
	float m_flSpawnDist = 0.f;
	float m_flSpawnHeight = 0.f;
	float m_flDrag = 0.f;
	int m_iLaunchDelay = 0;
};

#pragma pack(1)
struct Solution_t
{
	float m_flPitch = 0.f;
	float m_flYaw = 0.f;
	float m_flTime = 0.f;
	uint8_t m_iCalculated = CalculateResultEnum::Pending;
};
#pragma pack()

struct Setup_t
{
	Vec3 m_vPoint = {};
	uint8_t m_iType = PointTypeEnum::Geometry;
};
struct Point_t
{
	Vec3 m_vPoint = {};
	Solution_t m_tSolution = {};
	uint8_t m_iType = PointTypeEnum::Direct;
};

struct Offset_t
{
	Vec3 m_vOffset;
	uint8_t m_iFlags;
	uint8_t m_iSamples=0;
};
using Directs_t = std::unordered_map<uint8_t, Offset_t>;
using Splashes_t = std::vector<uint8_t>;

struct History_t
{
	Vec3 m_vOrigin;
	int m_iSimtime;
};
struct Direct_t : History_t
{
	float m_flPitch;
	float m_flYaw;
	float m_flTime;
	Vec3 m_vPoint;
	int m_iPriority;
	int m_iAttempt=0;
};
struct Splash_t : History_t
{
	float m_flTimeTo;
};
using DirectHistory_t = std::unordered_map<uint8_t, std::vector<Direct_t>>;
using SplashHistory_t = std::unordered_map<uint8_t, std::vector<Splash_t>>;

struct PasstimePlan_t
{
	int m_iKind = PasstimeKindEnum::None;
	int m_iEntity = 0;
	Vec3 m_vAngle = {};
	Vec3 m_vPoint = {};
	float m_flTime = 0.f;
	int m_iTier = 0;
	float m_flKey1 = 0.f;
	float m_flKey2 = 0.f;
};

bool SolveLaunch(const Vec3& vDelta, float flSpeed, const Vec3& vAdd, float flGravity, bool bLob, Vec3& vOut, float& flTime, float flDrag = 0.f);

class CAimbotProjectile
{
private:
	Directs_t GetDirects();
	void BuildDirects(Directs_t* output);
	Splashes_t GetSplashes();
	void SetupSplashPoints(Vec3& vOrigin, std::vector<Setup_t>& vSplashPoints, uint8_t iFlags = CalculateFlagsEnum::None);
	std::vector<Point_t> GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, int iSimTime, uint8_t iFlags = CalculateFlagsEnum::Accuracy, bool bFirst = false);

	void CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, uint8_t iFlags = CalculateFlagsEnum::Accuracy, int iTolerance = -1);
	bool TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags, bool bSecondTest = false);

	bool HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, uint8_t iType = PointTypeEnum::Direct, uint8_t iFlags = PointFlagsEnum::Regular,float roll=0.f);
	bool HandleDirect(DirectHistory_t& vDirectHistory);
	bool HandleSplash(SplashHistory_t& vSplashHistory);
	std::array<SeamGeometry::Face,SeamGeometry::MaxFaces> m_aSeamFaces{};
	struct SeamAim {Vec3 angles{},start{},velocity{},direction{},hull{};float speed=0,lifetime=0;};
	bool AimSplashContact(const Vec3& point,SeamAim& result);
	bool SplashDamageVisible(const Vec3& origin,const Vec3& targetOrigin);
	bool SearchSplashSeams(const Splash_t& frame,uint8_t mode,uint8_t flags);
	bool m_bSeamValidation=false;
	float m_flVerifiedContactTime=0;
	Vec3 m_vVerifiedContactTarget{};
	int m_iSeamSetups=0,m_iSeamShots=0;
	bool HandleSplashLegacy(SplashHistory_t& vSplashHistory);

	int CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate = true);
	bool RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool RunMechArm(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void ResetSolution();

	std::vector<Target_t> GetProjectiles(CTFPlayer* pLocal);

	bool CanHitProjectile(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, Target_t& tTarget, int iSimTicks);
	bool CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pProjectile);
	bool TestAngle(CBaseEntity* pProjectile, const Vec3& vOrigin, const Vec3& vPoint, Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags);

	bool Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut);
	void Aim(CUserCmd* pCmd, Vec3& vAngles);

	void BeginSearch();
	bool ExtraSearchAvailable();
	int64_t m_iExtraDeadline=0;
	int m_iExtraFrame=-1;
	bool HeadFirst(const Vec3& point,const Vec3& velocity,const Vec3& predictedOrigin,bool contact=false) const;
	bool FindHeadPoint(const Vec3& predictedOrigin,int tick,uint8_t flags,Vec3& point,Solution_t& solution);
	matrix3x4* m_pHeadBones=nullptr;
	std::array<matrix3x4,MAXSTUDIOBONES> m_aHeadSnapshot{};
	mstudiohitboxset_t* m_pHeadSet=nullptr;
	Vec3 m_vHeadOrigin={};
	int m_iWorkCommand=-1,m_iAngleTests=0,m_iRefineTraces=0,m_iSeamPairs=0;
	float m_flBestSplashScore=0,m_flTargetSpeed=0,m_flTargetTurnRate=0;
	Vec3 m_vVerifiedImpact={},m_vVerifiedNormal={},m_vBestImpact={},m_vBestNormal={};
	struct SplashSeed
	{
		CHandle<CBaseEntity> target;
		Vec3 point,origin;
		int command=-1,weapon=-1;
	};
	std::array<SplashSeed,8> m_aSplashSeeds{};
	struct StickyCandidate {Vec3 point{},normal{};uint8_t type=0,arcs=0;bool relative=false;int priority=0;};
	struct StickyFlight
	{
		std::array<Vec3,StickySearch::MaxTicks+1> path{};
		Vec3 error{},contact{},normal{};
		float distance=std::numeric_limits<float>::max(),impactTime=0.f;
		int count=0,closest=0,command=-1,airSamples=0;
		std::array<int,8> airTicks{};
		bool hit=false,sky=false;
	};
	struct StickyState
	{
		StickySearch::Cursor cursor{};
		std::array<StickyCandidate,StickySearch::MaxCandidates> candidates{};
		int count=0,horizon=0,bestAirCursor=0;
		Vec3 angles{},pitchError{},yawError{},proposed{};
		StickyFlight flight{};
	};
	std::array<StickyState,16> m_aStickySearch{};
	StickyFlight m_stickyTrial{};
	std::array<StickyCandidate,16> m_aStickyDirectSeeds{};
	int m_iStickyDirectSeedCount=0;
	StickySearch::Work m_stickyWork{};
	int m_iStickyPredictionBase=0;
	bool m_bStickyValidation=false,m_bStickyPathBorrowed=false;
	std::vector<Vec3> m_stickyValidationPath;
	alignas(16) std::array<byte,131072> m_aStickyMoveScratch{};
	void ResetMoveStorage();
	void ReleaseStickyPath();
	Vec3 StickyTargetAt(int tick) const;
	void DiscoverSticky(StickyState& state);
	bool FlySticky(StickyState& state,const StickyCandidate& candidate,const Vec3& angles,StickyFlight& flight);
	bool SearchSticky();

	Info_t m_tInfo = {};
	MoveStorage m_tMoveStorage = {};
	ProjectileInfo m_tProjInfo = {};
	std::vector<Setup_t> m_vSplashPoints = {};

	bool m_bLastTickHeld = false;

	int m_iLaunchDelay = 0;
	float m_flSpecialDrag = 0.f;

	struct ZombieState_t
	{
		int m_iPhase = ZombiePhaseEnum::Idle;
		int m_iSpecial = ProjSpecialEnum::None;
		int m_iStartTick = 0;
		int m_iEndTick = 0;
		int m_iTargetEnt = 0;
		Vec3 m_vAngle = {};
		bool m_bHasAngle = false;
		int m_iNextIdleTick = 0;
		int m_iIdleFails = 0;
		int m_iLastSolveTick = -1000;
		std::unordered_map<int, int> m_mFailTick = {};

		void Reset()
		{
			*this = {};
		}
	} m_tZombie;
	bool IsZombieMap();
	bool IsAbilityReady(CTFPlayer* pLocal);
	bool SolveAbility(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iSpecial, int iDelayTicks, Target_t& tOut);
	void HoldAngles(CUserCmd* pCmd, const Vec3& vAngles);

	struct PasstimeState_t
	{
		int m_iPhase = PasstimePhaseEnum::Idle;
		int m_iKind = PasstimeKindEnum::None;
		int m_iEntity = 0;
		int m_iTicks = 0;
		int m_iFailTicks = 0;
		int m_iLastTick = -1;
		bool m_bLastResult = false;
		float m_flCooldownUntil = 0.f;
		int m_iBadEntity = 0;
		float m_flBadUntil = 0.f;
		int m_iNextPlanTick = 0;
		int m_iIdleFails = 0;
		int m_iLastPlanTick = -1000;
		bool m_bPlanned = false;
		PasstimePlan_t m_tPlan = {};

		void Reset(float flCooldown = 0.f)
		{
			PasstimeState_t tKeep = {};
			tKeep.m_iLastTick = m_iLastTick, tKeep.m_bLastResult = m_bLastResult, tKeep.m_iBadEntity = m_iBadEntity, tKeep.m_flBadUntil = m_flBadUntil;
			tKeep.m_flCooldownUntil = flCooldown;
			*this = tKeep;
		}
	} m_tPasstime;
	bool PlanPasstime(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, PasstimePlan_t& tPlan, int iForceKind = PasstimeKindEnum::None, int iForceEntity = 0);
	bool SolvePasstimeGoal(CTFPlayer* pLocal, CBaseEntity* pGoal, PasstimePlan_t& tPlan);
	bool SolvePasstimePass(CTFPlayer* pLocal, CTFPlayer* pTeammate, PasstimePlan_t& tPlan);

	float m_flTimeTo = std::numeric_limits<float>::max();
	std::vector<Vec3> m_vBestPlayerPath = {};
	std::vector<Vec3> m_vPlayerPath = {};
	std::vector<Vec3> m_vProjectilePath = {};
	std::vector<DrawBox_t> m_vBoxes = {};
	Vec3 m_vAngleTo = {};
	Vec3 m_vPredicted = {};
	Vec3 m_vTarget = {};
	Vec3 m_vShootPos = {};
	Vec3 m_vPlainAngles = {};

	int m_iWeaponID = -1;
	int m_iMethod = -1;
	int m_iResult = false;
	bool m_bUpdate = true;
	bool m_bBestPlayerPathSet = false;
	bool m_bBlockAimAnglesDraw = false;
	bool m_bMainSearchComplete = false;

	struct GrappleInfo_t
	{
		float m_flRanTime = 0.f;
		float m_flLastTimeTo = std::numeric_limits<float>::max();
		bool m_bGrapplingHookShot = false;
		bool m_bWallOnMiss = false;
		bool m_bFail = false;
		Vec3 m_vLastGrapplePoint = {};
		Vec3 m_vLastAngleTo = {};

		inline void Fail()
		{
			m_flRanTime = 0.f;
			m_flLastTimeTo = std::numeric_limits<float>::max();
			m_bGrapplingHookShot = false;
			m_bWallOnMiss = false;
			m_bFail = true;
			m_vLastGrapplePoint = {};
			m_vLastAngleTo = {};
		}
	} m_tGrappleInfo;
	CTFGrapplingHook* m_pGrapplingHook = nullptr;
	CObjectSentrygun* m_pSentryGun = nullptr;

public:
	CAimbotProjectile()
	{
		m_tMoveStorage.m_vPath.reserve(StickySearch::MaxTicks+256);
		m_stickyValidationPath.reserve(StickySearch::MaxTicks+1);
		m_vPlayerPath.reserve(StickySearch::MaxTicks+256);
		m_vBestPlayerPath.reserve(StickySearch::MaxTicks+256);
		m_vProjectilePath.reserve(StickySearch::MaxTicks+1);
	}
	void ResetMainSearch() { m_bMainSearchComplete = false; }
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void RunPreview(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void RunGrapplingHook(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool AimPasstimePass(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool RunPasstime(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool RunZombieAbility(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	std::vector<Target_t> GetAbilityTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void ShowTarget(int iResult, Target_t& tTarget);
	float GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale = 1.f);

	bool AutoAirblast(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile);
	float GetSplashRadius(CBaseEntity* pProjectile, CTFWeaponBase* pWeapon = nullptr, CTFPlayer* pPlayer = nullptr, float flScale = 1.f);

	int m_iLastTickCancel = 0;
	int m_iAimLock = 0;
	Vec3 m_vAimAngles = {};
	float m_flAimAnglesSetTime = 0.f;
};

ADD_FEATURE(CAimbotProjectile, AimbotProjectile);
