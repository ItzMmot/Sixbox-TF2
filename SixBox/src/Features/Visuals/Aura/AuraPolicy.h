#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace Aura
{
    constexpr int Modes = 14, Layers = 8, Programs = Modes + 2;
    inline constexpr const char* ModeNames[] = { "Fire", "Hologram", "Embers", "Plasma", "Lightning", "Aurora", "Frost", "Circuit", "Hex shield", "Vortex", "Radar", "Digital rain", "Liquid metal", "Rainbow" };
    constexpr int CatalogVersion = 2;
    inline int UpgradeMode(int mode,int catalog)
    {
        mode=std::clamp(mode,0,Modes-1);
        if(catalog>=CatalogVersion) return mode;
        switch(mode)
        {
        case 1: case 7: case 8: case 12: return 5; // old mist/cloud variants -> Aurora
        case 10: return 0; // Solar -> Fire
        case 11: return 4; // Spirit -> Lightning
        default: return mode;
        }
    }
    inline bool DefaultAdditive(int mode) {return mode!=12;}
#define AURA_FLOAT_SETTINGS(X) \
    X(Scale, "Scale", 1.f, .1f, 4.f, .1f) \
    X(Velocity, "Velocity", 24.f, 0.f, 150.f, 2.f) \
    X(Thickness, "Coating thickness", 16.f, 1.f, 80.f, 1.f) \
    X(Height, "Height", 1.6f, .25f, 5.f, .1f) \
    X(Width, "Width", 1.f, .2f, 3.f, .1f) \
    X(Fill, "Surface fill", .3f, 0.f, 1.f, .05f) \
    X(Outline, "Outer coating", 1.f, 0.f, 3.f, .1f) \
    X(Softness, "Edge softness", .65f, .1f, 2.f, .05f) \
    X(Opacity, "Opacity", .65f, 0.f, 1.f, .05f) \
    X(Intensity, "Intensity", 1.4f, .1f, 4.f, .1f) \
    X(Glow, "Soft glow", .35f, 0.f, 2.f, .05f) \
    X(Turbulence, "Turbulence", 1.f, 0.f, 3.f, .1f) \
    X(NoiseScale, "Noise scale", 3.f, .5f, 10.f, .25f) \
    X(Animation, "Animation speed", 1.f, 0.f, 5.f, .1f) \
    X(Wind, "Wind strength", 10.f, 0.f, 100.f, 1.f) \
    X(WindSpeed, "Wind speed", 1.5f, 0.f, 8.f, .1f) \
    X(WindDirection, "Wind direction", 0.f, -180.f, 180.f, 5.f) \
    X(Motion, "Movement response", .35f, 0.f, 2.f, .05f) \
    X(CameraSway, "Camera sway", .6f, 0.f, 3.f, .05f) \
    X(Inertia, "Motion smoothing", .15f, .01f, 1.f, .01f) \
    X(Flicker, "Flicker", .25f, 0.f, 1.f, .05f) \
    X(Pulse, "Pulse", 0.f, 0.f, 1.f, .05f) \
    X(Spin, "Swirl", .4f, -3.f, 3.f, .1f) \
    X(Distance, "Maximum distance", 2500.f, 128.f, 8000.f, 128.f)
    struct Settings
    {
        bool Enabled = false, Additive = true, GroupColor = false, PresetColors = true, VisibleOnly = true;
        int Mode = 0, Quality = 1, Coverage = 0;
#define AURA_DECLARE(n,l,d,lo,hi,step) float n = d;
        AURA_FLOAT_SETTINGS(AURA_DECLARE)
#undef AURA_DECLARE
        void Sanitize()
        {
            Mode = std::clamp(Mode,0,Modes-1); Quality = std::clamp(Quality,0,2); Coverage = std::clamp(Coverage,0,2);
#define AURA_CLAMP(n,l,d,lo,hi,step) n = std::isfinite(n) ? std::clamp(n,lo,hi) : d;
            AURA_FLOAT_SETTINGS(AURA_CLAMP)
#undef AURA_CLAMP
        }
    };
    struct Vector
    {
        float x = 0.f, y = 0.f, z = 0.f;
        Vector operator+(Vector b) const { return {x+b.x,y+b.y,z+b.z}; }
        Vector operator-(Vector b) const { return {x-b.x,y-b.y,z-b.z}; }
        Vector operator*(float f) const { return {x*f,y*f,z*f}; }
        bool Finite() const { return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z); }
    };
    inline float Wrap(float degrees)
    {
        if(!std::isfinite(degrees)) return 0.f;
        return std::remainder(degrees,360.f);
    }
    struct MotionFilter
    {
        float x=0.f,y=0.f;
        void Reset() { x=y=0.f; }
        void Update(float dx,float dy,float dt,float smoothing)
        {
            if(!std::isfinite(dt)||dt<=0.f) return;
            if(dt>.25f || !std::isfinite(dx) || !std::isfinite(dy)) {Reset();return;}
            const float blend=1.f-std::exp(-dt/std::clamp(smoothing,.01f,1.f));
            x+=(std::clamp(dx,-1.f,1.f)-x)*blend;
            y+=(std::clamp(dy,-1.f,1.f)-y)*blend;
        }
    };
    // Fixed command storage: slots cannot be reused until the material queue's
    // final Release. QueueFunctorInternal uses the same ABI as RenderSync.
    template<int Count> class CommandSlots
    {
        std::array<std::atomic_bool,Count> m_busy{};
    public:
        int Acquire()
        {
            for(int i=0;i<Count;++i)
            {
                bool expected=false;
                if(m_busy[i].compare_exchange_strong(expected,true,std::memory_order_acq_rel)) return i;
            }
            return -1;
        }
        void Release(int slot) { m_busy[slot].store(false,std::memory_order_release); }
        bool Busy() const {for(const auto& slot:m_busy) if(slot.load(std::memory_order_acquire)) return true;return false;}
    };
}
