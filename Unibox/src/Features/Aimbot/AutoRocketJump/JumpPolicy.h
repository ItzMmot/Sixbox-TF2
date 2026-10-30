#pragma once
#include <algorithm>
#include <cmath>

namespace RocketJump
{
    enum class ReloadAction { Idle, WaitingForShell, Interrupt, Ready, TimedOut };
    class ReloadGate
    {
        float m_start = 0.f;
        bool m_waiting = false, m_expired = false;
    public:
        void Reset() { *this = {}; }
        ReloadAction Update(float now, bool ready, bool reloading, bool ammo, bool reserve)
        {
            if (!std::isfinite(now)) { Reset(); return ReloadAction::Idle; }
            if (ready && ammo) { Reset(); return ReloadAction::Ready; }
            if ((!reloading && !m_waiting) || (!ammo && !reserve)) { Reset(); return ReloadAction::Idle; }
            if (m_expired) return ReloadAction::TimedOut;
            if (!m_waiting) { m_waiting = true; m_start = now; }
            if (now < m_start || now - m_start > 5.f) { m_expired = true; return ReloadAction::TimedOut; }
            return ammo ? ReloadAction::Interrupt : ReloadAction::WaitingForShell;
        }
    };
    // Solve x*x + b*x + c = 0 without allocating a temporary vector.
    inline bool PositiveRoot(float b, float c, float& root)
    {
        if (!std::isfinite(b) || !std::isfinite(c)) return false;
        const float discriminant = b * b - 4.f * c;
        if (!std::isfinite(discriminant) || discriminant < 0.f) return false;
        const float d = std::sqrt(discriminant);
        const float first = (-b-d)*.5f, second = (-b+d)*.5f;
        if (second >= 0.f) { root = second; return true; }
        if (first >= 0.f) { root = first; return true; }
        return false;
    }
    inline bool WallNormal(float x, float y, float z)
    {
        const float horizontal = x*x+y*y;
        return std::isfinite(horizontal) && std::isfinite(z) && horizontal > .8f && std::abs(z) < .25f;
    }
    inline int ImpactTicks(float distance, float speed, float interval)
    {
        if (!std::isfinite(distance) || !std::isfinite(speed) || !std::isfinite(interval)
            || distance < 0.f || speed <= 0.f || interval <= 0.f) return 0;
        const float ticks = distance / (speed*interval);
        return ticks <= 10.f ? std::max(1,int(std::ceil(ticks))) : 0;
    }
}
