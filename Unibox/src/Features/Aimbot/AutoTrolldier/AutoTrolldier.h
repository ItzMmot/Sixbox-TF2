#pragma once
#include "../../../SDK/SDK.h"
#include "Intercept.h"
#include "Rotation.h"
#include "Automation.h"
#ifndef TEXTMODE
#include "../../ImGui/IndicatorCache.h"
#endif

class CAutoTrolldier
{
	Trolldier::Solver m_solver;
	Trolldier::Plan m_plan;
	Trolldier::Settings m_settings;
	std::array<Trolldier::Actor, Trolldier::MaxTargets> m_targets = {};
	std::array<CHandle<CTFPlayer>, Trolldier::MaxTargets> m_handles = {};
	CHandle<CTFPlayer> m_owner, m_target;
	CHandle<CTFWeaponBase> m_weapon;
	Vec3 m_worldWish;
	int m_command = -1;
	bool m_active = false;
	Vec3 m_rotationAngles;
	float m_pitchVelocity = 0.f, m_yawVelocity = 0.f;
	int m_rotationMode = -1;
	bool m_rotationInitialized = false;
	enum class SwapPhase { Idle, SelectingGardener, HoldingGardener, ReturningLauncher };
	SwapPhase m_swapPhase = SwapPhase::Idle;
	CHandle<CTFPlayer> m_swapOwner;
	CHandle<CTFWeaponBase> m_launcher, m_gardener;
	Trolldier::SwingTracker m_swing;
	bool m_swapConsumed = false;
	float m_swapRequested = 0.f;
	CHandle<CTFPlayer> m_strikeTarget, m_strikeOwner;
	CHandle<CTFWeaponBase> m_strikeWeapon;
	float m_strikeUntil = 0.f;
#ifndef TEXTMODE
	struct DrawData
	{
		std::array<Vec3, Trolldier::MaxTicks + 1> points = {};
		int count = 0;
		Color_t color = {};
		bool depth = true;
		uint64_t expires = 0;
	};
	CIndicatorCache<DrawData> m_drawCache;
#endif

	bool Eligible(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd) const;
	void ResetControl();
	void CancelSwap();
	static Trolldier::Trace TraceWorld(void* pContext, Trolldier::Vector from, Trolldier::Vector to,
		Trolldier::Vector mins, Trolldier::Vector maxs, int actor);

public:
	void UpdateAutomation(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void AfterAim(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Event(IGameEvent* pEvent, uint32_t hash);
	bool SuppressCombat() const { return m_swapPhase == SwapPhase::SelectingGardener || m_swapPhase == SwapPhase::ReturningLauncher; }
	bool AwaitingSwing() const { return m_swapPhase == SwapPhase::HoldingGardener && m_swing.Result() != Trolldier::SwingResult::None; }
	bool HasCommittedStrike(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const CUserCmd* pCmd) const;
	int AutomaticAimType() const;
	void Prepare(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void ApplyMovement(CUserCmd* pCmd) const;
	void RotateCamera(CTFPlayer* pLocal, CUserCmd* pCmd);
	void RenderWorld();
	void Reset();
	bool IsActive(const CUserCmd* pCmd) const;
	CTFPlayer* GetTarget() const { return m_target.Get(); }
	bool StrikePrediction(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd,
		float flDelay, Vec3& vEye, Vec3& vTargetOrigin) const;
};

ADD_FEATURE(CAutoTrolldier, AutoTrolldier);
