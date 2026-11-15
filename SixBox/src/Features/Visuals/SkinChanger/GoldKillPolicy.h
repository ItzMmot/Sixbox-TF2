#pragma once
#include <array>
#include <cstdint>

namespace GoldKillPolicy
{
    enum class Kind { None, Australium, Saxxy, Wrench, Pan };
    constexpr Kind Identify(int definition, bool australium)
    {
        if (definition == 423) return Kind::Saxxy;
        if (definition == 169) return Kind::Wrench;
        if (definition == 1071) return Kind::Pan;
        return australium ? Kind::Australium : Kind::None;
    }
    constexpr const char* Icon(Kind kind)
    {
        switch (kind)
        {
        case Kind::Saxxy: return "saxxy";
        case Kind::Wrench: return "wrench_golden";
        case Kind::Pan: return "golden_fryingpan";
        default: return nullptr; // Australiums use their normal weapon's native icon.
        }
    }
    struct Death
    {
        int userID = 0;
        float time = -100.f;
        bool gold = false;
        bool consumed = false;
    };
    class Deaths
    {
        std::array<Death, 129> deaths = {};
    public:
        void Clear() { deaths = {}; }
        void Spawn(int index) { if (index > 0 && index < int(deaths.size())) deaths[index] = {}; }
        void Record(int index, int userID, float time, bool gold)
        {
            if (index > 0 && index < int(deaths.size())) deaths[index] = {userID, time, gold, false};
        }
        bool Ready(int index, int userID, float now) const
        {
            if (index <= 0 || index >= int(deaths.size())) return false;
            const auto& d = deaths[index];
            return !d.consumed && d.userID == userID && now >= d.time && now - d.time <= 1.f;
        }
        bool Consume(int index, int userID, float now)
        {
            if (!Ready(index, userID, now)) return false;
            auto& d = deaths[index];
            d.consumed = true;
            return d.gold;
        }
    };
}
