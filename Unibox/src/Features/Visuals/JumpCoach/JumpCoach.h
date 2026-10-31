#pragma once
#include "../../../SDK/SDK.h"
#ifndef TEXTMODE
#include "CoachPolicy.h"
#include "../../ImGui/IndicatorCache.h"

class CJumpCoach
{
    enum class RocketState {Hidden,Grounded,Equip,Unsupported,NoAmmo,Reload,Cooldown,NoLanding,NoWindow,Wait,Now,Aim,Automatic};
    struct Display
    {
        std::array<Vec3,JumpCoach::MaxTicks+1> path{},ideal{};
        int count=0,idealCount=0;
        JumpCoach::End end=JumpCoach::End::Invalid;
        Vec3 origin,landing,normal,shotPoint,wish;
        float speed=0,vertical=0,launchGain=0,peakLoss=0,airtime=0;
        float landTime=0,landSpeed=0,landDistance=0,turn=0,efficiency=0,shotDelay=0;
        float horizon=3,hudX=50,hudY=70,scale=1,markerRadius=24;
        Color_t color={122,132,229,220};
        RocketState rocket=RocketState::Hidden;
        uint64_t expires=0;
        bool active=false,grounded=false,guide=false,automated=false,showHUD=true,depth=true,landingMarker=true;
        bool showShot=false,showStrafe=true;
        int side=1;
    };
    CIndicatorCache<Display> m_display;
    JumpCoach::Plan m_plan,m_ideal;
    JumpCoach::Physics m_physics;
    CHandle<CTFPlayer> m_owner;
    int m_command=-1,m_side=1;
    float m_takeoffSpeed=0,m_peakSpeed=0,m_airStart=0,m_lastGroundSpeed=0,m_efficiency=0;
    bool m_airborne=false;
    static JumpCoach::Trace Trace(void* context,JumpCoach::Vector from,JumpCoach::Vector to,JumpCoach::Vector mins,JumpCoach::Vector maxs);
    void RocketAdvice(CTFPlayer* local,CTFWeaponBase* weapon,const CUserCmd* cmd,const JumpCoach::Body& body,Display& display);
public:
    void Update(CTFPlayer* local,CTFWeaponBase* weapon,const CUserCmd* cmd);
    void Reset();
    void RenderWorld();
    void DrawHUD();
};
ADD_FEATURE(CJumpCoach, JumpCoach);
#endif
