#pragma once
#include <algorithm>
#include <cmath>
#include "Intercept.h"

namespace Trolldier
{
	// ItemPostFrame smacks strictly AFTER the deadline. Sample 1 is the current
	// command's movement, because flight planning runs before engine prediction.
	inline int ImpactSample(int tickBase, float interval, float deadline)
	{
		if (tickBase < 0 || tickBase > std::numeric_limits<int>::max() - MaxTicks
			|| !std::isfinite(interval) || interval <= 0.f || !std::isfinite(deadline)) return 0;
		for (int offset = 0; offset < MaxTicks; ++offset)
			if (float(tickBase + offset) * interval > deadline) return offset + 1;
		return 0;
	}
	inline bool StableStrikeWindow(const Plan& plan, int sample)
	{
		return plan.target >= 0 && plan.windowTicks >= 3 && sample > plan.hitTick
			&& sample < plan.endTick && sample <= MaxTicks;
	}
	inline Vector InteriorPoint(Vector eye, Vector mins, Vector maxs, float inset = 3.f)
	{
		const Vector pad = { std::min(inset, (maxs.x - mins.x) * .5f),
			std::min(inset, (maxs.y - mins.y) * .5f), std::min(inset, (maxs.z - mins.z) * .5f) };
		return Nearest(eye, mins + pad, maxs - pad);
	}
	inline bool HullFallback(float rayFraction, bool startSolid, bool allSolid)
	{
		return rayFraction >= 1.f && !startSolid && !allSolid;
	}
	enum class SwingResult { None, Waiting, Hit, Unconfirmed, Cancelled };
	class SwingTracker
	{
		float m_requested = 0.f, m_impact = 0.f;
		bool m_windup = false, m_hit = false, m_completed = false;
		SwingResult m_result = SwingResult::None;
	public:
		void Reset() { *this = {}; }
		bool Active() const { return m_result == SwingResult::Waiting; }
		SwingResult Result() const { return m_result; }
		void Request(float now, float delay)
		{
			if (Active() || !std::isfinite(now) || !std::isfinite(delay) || delay < 0.f) return;
			Reset(); m_requested = now; m_impact = now + delay; m_result = SwingResult::Waiting;
		}
		void ConfirmHit() { if (Active()) m_hit = true; }
		SwingResult Observe(float now, float smackTime, float interval, float acknowledgment)
		{
			if (!Active()) return m_result;
			if (!std::isfinite(now) || !std::isfinite(smackTime) || !std::isfinite(interval) || interval <= 0.f)
				return m_result = SwingResult::Cancelled;
			if (now + interval < m_requested) return m_result = SwingResult::Cancelled;
			if (smackTime > 0.f)
			{
				if (!m_windup) m_impact = smackTime;
				m_windup = true;
			}
			// A confirmed server hit proves the swing happened even if a slow
			// frame skipped every positive smack-time sample. Still wait until
			// the weapon has cleared its windup before allowing a swap.
			if ((m_hit || m_windup) && smackTime <= 0.f && now >= m_impact + interval) m_completed = true;
			if (m_completed && m_hit && smackTime <= 0.f) return m_result = SwingResult::Hit;
			if (!m_windup)
			{
				// A request is not proof that the weapon actually started a swing.
				if (now > std::max(m_impact + 0.25f, m_requested + 0.5f)) m_result = SwingResult::Cancelled;
				return m_result;
			}
			const float wait = std::isfinite(acknowledgment) ? std::clamp(acknowledgment, 0.15f, 1.f) : 1.f;
			if (m_completed && smackTime <= 0.f && now >= m_impact + wait) return m_result = SwingResult::Unconfirmed;
			// A permanently stale/aborted windup must not leave the swap locked.
			if (smackTime > 0.f && now > m_impact + 1.5f) m_result = SwingResult::Cancelled;
			return m_result;
		}
	};

	// Scope the effective settings to the melee adapter, including early returns.
	// Bind key/toggle states and ConfigVar maps are never changed.
	class AimSettingsGuard
	{
		int &m_aim, &m_targets;
		bool &m_shoot, &m_restrict;
		int m_oldAim, m_oldTargets;
		bool m_oldShoot, m_oldRestrict, m_enabled;
	public:
		AimSettingsGuard(int& aim, bool& shoot, int& targets, bool& restrictAngle,
			bool enabled, int mode, int playerFlag)
			: m_aim(aim), m_targets(targets), m_shoot(shoot), m_restrict(restrictAngle),
			m_oldAim(aim), m_oldTargets(targets), m_oldShoot(shoot), m_oldRestrict(restrictAngle), m_enabled(enabled)
		{
			if (enabled) { aim = mode; shoot = true; targets |= playerFlag; restrictAngle = false; }
		}
		~AimSettingsGuard()
		{
			if (m_enabled) { m_aim = m_oldAim; m_shoot = m_oldShoot; m_targets = m_oldTargets; m_restrict = m_oldRestrict; }
		}
		AimSettingsGuard(const AimSettingsGuard&) = delete;
		AimSettingsGuard& operator=(const AimSettingsGuard&) = delete;
	};
}
