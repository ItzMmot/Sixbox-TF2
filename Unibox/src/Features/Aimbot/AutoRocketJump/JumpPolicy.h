#pragma once
#include <algorithm>
#include <cmath>

namespace RocketJump
{
    enum class Mode { Diagonal, Vertical, Horizontal };
    enum class Surface { None, Floor, Wall };
    // Immediate callbacks are stack-only: no std::function or heap allocation.
    // A valid floor owns the tick even if its launch awaits stable grounding.
    template<class Floor, class Wall> Surface SelectSurface(Floor&& floor, Wall&& wall)
    {
        if (floor()) return Surface::Floor;
        return wall() ? Surface::Wall : Surface::None;
    }
    inline Mode GetMode(int value)
    {
        return value >= 0 && value <= 2 ? static_cast<Mode>(value) : Mode::Diagonal;
    }
    inline bool FloorNormal(float x, float y, float z)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && z >= .7f && z <= 1.01f;
    }
    // Relative impulse projection, not a promised server-side velocity. TF2 pushes
    // away from inflictor center - (0,0,10); caller supplies the resulting vector.
    // Common weapon/server force multipliers cancel when ranking one launcher's shots.
    inline float BoostScore(Mode mode, float x, float y, float z, float nearestDistance,
        float radius, float wishX, float wishY)
    {
        const float lengthSqr = x*x + y*y + z*z;
        if (!std::isfinite(lengthSqr) || lengthSqr < .001f || !std::isfinite(nearestDistance)
            || !std::isfinite(radius) || radius <= 0.f || nearestDistance < 0.f || nearestDistance >= radius)
            return -1.f;
        const float falloff = 1.f - .5f * nearestDistance / radius;
        if (mode == Mode::Vertical) return z > 0.f ? falloff * z / std::sqrt(lengthSqr) : -1.f;
        const float wishLengthSqr = wishX*wishX + wishY*wishY;
        if (!std::isfinite(wishLengthSqr) || wishLengthSqr < .001f) return -1.f;
        const float forward = (x*wishX + y*wishY) / std::sqrt(wishLengthSqr);
        if (mode == Mode::Horizontal) return forward > 0.f ? falloff * forward / std::sqrt(lengthSqr) : -1.f;
        return falloff * (std::max(forward, 0.f) + std::max(z, 0.f)) / std::sqrt(2.f * lengthSqr);
    }

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
