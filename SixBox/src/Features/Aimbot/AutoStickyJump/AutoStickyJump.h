#pragma once
#include "../../../SDK/SDK.h"
#include "StickyJumpPolicy.h"
#include "../../ImGui/IndicatorCache.h"

class CAutoStickyJump
{
public:
    struct Display
    {
        std::array<Vec3,96> trajectory{},approach{};
        int trajectoryCount=0,approachCount=0,count=0,wanted=1,burst=0;
        Vec3 takeoff,wish,blast;
        float armRemaining=0;
        StickyJump::Phase phase=StickyJump::Phase::Plan;
        const char* status="";
        uint64_t expires=0;
        bool active=false,valid=false;
    };
    void Prepare(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    void AfterNoSpread(CUserCmd* cmd);
    void Finalize(CTFPlayer* local,CUserCmd* cmd);
    void Reset();
    bool Controls(const CUserCmd* cmd) const {return cmd&&StickyJump::OwnsCommand(m_controlCommand,G::OriginalCmd.command_number);}
    bool FlightPreview() const;
    bool Busy() const {return m_controlCommand>=0;}
    Display Snapshot() {return m_display.Get();}
private:
    CIndicatorCache<Display> m_display;
    std::unordered_set<int> m_baseline;
    Display m_plan;
    StickyJump::FireGate m_fire;
    StickyJump::FlushGate m_flush;
    bool m_flushDone=false,m_flushHeading=false;
    Vec3 m_flushWish{};
    std::unordered_map<int,float> m_firstSeen;
    const char* m_reason=nullptr;
    struct Diagnostics
    {
        bool attached=false,armed=false,clear=false;
        float rawCreation=0,rawSpeed=0,current=0,best=0,ahead=0;
    } m_diagnostics;
    StickyJump::Phase m_loggedPhase=StickyJump::Phase::Failed;
    const char* m_loggedStatus=nullptr;
    uint64_t m_nextDiagnostics=0;
    CHandle<CTFPlayer> m_owner;
    CHandle<CTFWeaponBase> m_weapon;
    StickyJump::Phase m_phase=StickyJump::Phase::Plan;
    Vec3 m_angles{},m_rotation{},m_worldMove{},m_beforeBlastVelocity{},m_aimOrigin{};
    float m_lastShotCheck=-100,m_pitchSpeed=0,m_yawSpeed=0,m_started=0,m_phaseTime=0,m_lastSearch=-100,m_lastRelease=0;
    int m_mode=-1,m_rotationMode=-1,m_lastCommand=-1,m_controlCommand=-1,m_chargeCommand=-1,m_released=0,m_jumpCommand=-1,m_spawn=-1;
    int m_setButtons=0,m_clearButtons=0,m_shotCommand=-1;
    bool m_rotationValid=false,m_airborne=false,m_burstTried=false;
    bool Flush(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    float CreationTime(CTFGrenadePipebombProjectile* sticky,float now);
    float ArmTime(CTFPlayer* local,CTFWeaponBase* weapon) const;
    bool Plan(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd);
    bool Shot(CTFPlayer* local,CTFWeaponBase* weapon,const Vec3& point,Vec3& angle,Display* display=nullptr,bool validate=true);
    bool Route(CTFPlayer* local,const Vec3& to,Display* display=nullptr);
    bool Rotate(CUserCmd* cmd,const Vec3& target);
    void Stage(StickyJump::Phase phase,float now);
    void Publish(float remaining=0,const char* status=nullptr);
};
ADD_FEATURE(CAutoStickyJump, AutoStickyJump);
