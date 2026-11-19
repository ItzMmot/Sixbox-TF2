#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// All velocities are HU/s and angles are degrees.
namespace StickyControl
{
    constexpr float Pi = 3.14159265358979323846f;
    struct Vec
    {
        float x = 0, y = 0, z = 0;
        Vec operator+(Vec b) const { return {x+b.x, y+b.y, z+b.z}; }
        Vec operator-(Vec b) const { return {x-b.x, y-b.y, z-b.z}; }
        Vec operator*(float s) const { return {x*s, y*s, z*s}; }
        float Dot(Vec b) const { return x*b.x+y*b.y+z*b.z; }
        float Length() const { return std::sqrt(Dot(*this)); }
        bool Finite() const { return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z); }
    };
    enum class Direction { Horizontal, Diagonal, Vertical };
    inline Direction DirectionForElevation(float elevation)
    {
        if (elevation>=70.f) return Direction::Vertical;
        if (elevation>15.f) return Direction::Diagonal;
        return Direction::Horizontal;
    }
    inline bool SupportedLauncher(bool pipebombLauncher, int detonateMode)
    { return pipebombLauncher && detonateMode==0; }
    inline Vec DesiredDirection(Direction d, float yaw)
    {
        const float elevation = (d == Direction::Horizontal ? 20.f : d == Direction::Vertical ? 90.f : 45.f)*Pi/180.f;
        yaw *= Pi/180.f;
        return {std::cos(yaw)*std::cos(elevation), std::sin(yaw)*std::cos(elevation), std::sin(elevation)};
    }
    struct Bomb
    {
        uint32_t id = 0;
        Vec position, velocity;
        float created = 0, armTime = .8f, damage = 120, falloff = .5f, pushMultiplier = 1;
        bool touched = true, ageKnown = true;
    };
    struct Body
    {
        Vec origin, velocity;
        Vec mins {-24,-24,0}, maxs {24,24,82};
        bool ducked = false, grounded = true;
    };
    inline bool Armed(const Bomb& bomb, float time)
    {
        return bomb.ageKnown && std::isfinite(time) && std::isfinite(bomb.created)
            && time-bomb.created >= bomb.armTime;
    }
    // Valve: tf_weaponbase_grenadeproj.cpp (RJ radius), tf_gamerules.cpp
    // (self radius damage), tf_player.cpp (DamageForce / ApplyPushFromDamage).
    // The RJ path restores base weapon damage even for Sticky Jumper/Quickie.
    inline Vec BlastImpulse(const Bomb& bomb, const Body& body, bool visible)
    {
        if (!visible || !bomb.position.Finite() || !body.origin.Finite()
            || !body.mins.Finite() || !body.maxs.Finite() || !std::isfinite(bomb.falloff)
            || !std::isfinite(bomb.damage) || !std::isfinite(bomb.pushMultiplier)) return {};
        const Vec center = body.origin+(body.mins+body.maxs)*.5f;
        const Vec nearest {
            std::clamp(bomb.position.x,body.origin.x+body.mins.x,body.origin.x+body.maxs.x),
            std::clamp(bomb.position.y,body.origin.y+body.mins.y,body.origin.y+body.maxs.y),
            std::clamp(bomb.position.z,body.origin.z+body.mins.z,body.origin.z+body.maxs.z)};
        constexpr float radius = 146.f;
        if ((nearest-bomb.position).Length()>radius) return {};
        const float distance = (std::min)((body.origin-bomb.position).Length(),(center-bomb.position).Length());
        const float damage = bomb.damage*(1.f+(bomb.falloff-1.f)*std::clamp(distance/radius,0.f,1.f))*.75f;
        const Vec size = body.maxs-body.mins;
        const float volume = size.x*size.y*(body.ducked ? 55.f : size.z);
        if (!(volume>0) || !(damage>0)) return {};
        const float force = (std::min)(1000.f,damage*(48.f*48.f*82.f/volume)*9.f)*bomb.pushMultiplier;
        const Vec direction = center-bomb.position+Vec{0,0,10};
        const float length = direction.Length();
        return length>0 && std::isfinite(force) ? direction*(force/length) : Vec{};
    }
    inline float DirectionError(Vec delta, Vec desired)
    {
        const float length = delta.Length();
        if (!delta.Finite() || !(length>0)) return 180.f;
        return std::acos(std::clamp(delta.Dot(desired)/length,-1.f,1.f))*180.f/Pi;
    }
    inline bool MeetsTarget(Vec delta, Vec desired, float target)
    {
        return std::isfinite(target) && target>0 && delta.Finite()
            && std::abs(delta.Length()-target)<=(std::max)(20.f,target*.15f)
            && DirectionError(delta,desired)<=15.f;
    }
    struct Request
    {
        Direction direction = Direction::Diagonal;
        float power = 50, yaw = 0;
        Vec start;
        bool automatic = false;
        Vec Wish() const { return DesiredDirection(direction,yaw); }
    };
    struct Candidate
    {
        Vec takeoff, delta;
        int jumpTicks = 1;
        bool valid = false;
    };

    // Normalize against local geometry before checking live movement.
    class Search
    {
        float m_maximum = 0;
    public:
        void Clear() { m_maximum=0; }
        void Add(const Candidate& candidate, Vec desired)
        {
            if (candidate.valid && candidate.delta.Finite() && DirectionError(candidate.delta,desired)<=15.f)
                m_maximum=(std::max)(m_maximum,candidate.delta.Length());
        }
        float Maximum() const { return m_maximum; }
    };
}
