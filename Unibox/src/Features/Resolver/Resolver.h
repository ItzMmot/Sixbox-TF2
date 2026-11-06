#pragma once
#include "../../SDK/SDK.h"
#include "../Aimbot/Shared/AimPolicy.h"
#include "../Aimbot/Shared/AnimationTimeline.h"
#include "AnimationPose.h"

struct TickRecord;

struct ResolveData
{
	float m_flYaw = 0.f; // to be added
	float m_flPitch = 0.f; // to be set, if pitch oob

	bool m_bYaw = false;
	bool m_bPitch = false;
	bool m_bMinwalk = true;
	bool m_bView = false; // instead of offsetting from sent angle, offset from view to local

	bool m_bAutoSetYaw = true;
	bool m_bAutoSetPitch = true;
	bool m_bFirstOOBPitch = false;
	bool m_bInversePitch = false;
};

class CResolver
{
private:
	void StoreSniperDots(CTFPlayerResource* pResource);
	std::optional<float> GetPitchForSniperDot(CTFPlayer* pEntity, CTFPlayerResource* pResource);
	
	std::unordered_map<int, ResolveData> m_mResolverData = {};
	std::unordered_map<int, Vec3> m_mSniperDots = {};

	AimPolicy::ShotLedger m_shots;
	AimPolicy::PendingShot m_preparedShot{};
	CHandle<CTFWeaponBase> m_hPreparedWeapon;
	int m_iPreparedCommand=-1;
	CHandle<CTFPlayer> m_hShotOwner;
	void CycleMiss(int userID);
	std::uint32_t m_iNextRevision=1;
	struct AnimationHypotheses
	{
		CHandle<CTFPlayer> player;
		std::array<AimPolicy::FeetYaw,8> feet{};
		AnimationTimeline::Observation observed{};
		AnimationTimeline::Identity identity{};
		AnimationPose pose{};
		AnimationTimeline::LayerOrder<GESTURE_SLOT_COUNT> layerOrder{};
		struct Event {PlayerAnimEvent_t type{};int data=0,tick=0;};
		std::array<Event,32> events{};
		int eventCount=0;
		bool overflow=false;
		int active=-1;
	};
	std::array<AnimationHypotheses,129> m_aAnimation{};
	
	float m_flLastYawCycle = 0.f;
	float m_flLastPitchCycle = 0.f;
	float m_flLastMinwalkCycle = 0.f;
	float m_flLastViewCycle = 0.f;

public:
	void InvalidateIdentity(CTFPlayer* player);
	void FrameStageNotify();
	bool UsesAnimationReplay(CTFPlayer* player) const;
	bool ReconstructAnimation(CTFPlayer* player);
	bool QueueAnimationEvent(CTFPlayer* player,PlayerAnimEvent_t event,int data);
	void DiscardAnimation(CTFPlayer* player);
	AnimationTimeline::Identity AnimationIdentity(CTFPlayer* player) const;
	void CreateMove();
	void BeginCommand();
	void CommitShot(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command);
	void HitscanRan(CTFPlayer* pLocal, CTFPlayer* pTarget, CTFWeaponBase* pWeapon, int nHitbox = HITBOX_MAX,const TickRecord* record=nullptr);
	void PlayerHurt(IGameEvent* pEvent);
	void SetYaw(int iUserID, float flValue, bool bAuto = false);
	void SetPitch(int iUserID, float flValue, bool bInverse = false, bool bAuto = false);
	void SetMinwalk(int iUserID, bool bValue);
	void SetView(int iUserID, bool bValue);
	bool GetAngles(CTFPlayer* pPlayer, float* pYaw = nullptr, float* pPitch = nullptr, bool* pMinwalk = nullptr, bool bFake = false);
	void Reset();
};

ADD_FEATURE(CResolver, Resolver);