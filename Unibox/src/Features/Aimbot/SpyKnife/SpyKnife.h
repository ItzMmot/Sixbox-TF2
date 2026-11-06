#pragma once
#include "../../../SDK/SDK.h"
#include "../AimbotGlobal/AimbotGlobal.h"
#include "KnifePolicy.h"
#include "../../ImGui/IndicatorCache.h"

class CSpyKnife
{
    CHandle<CTFPlayer> m_owner,m_target;
    CHandle<CTFWeaponBase> m_weapon;
    std::array<Vec3,SpyKnife::MaxTicks+1> m_moves{},m_positions{};
    std::array<byte,65536> m_saved{},m_moveScratch{};
    int m_start=-1,m_count=0,m_ownerSpawn=0,m_targetSpawn=0,m_route=0,m_searchFrame=-1;
    int m_prepared=-1,m_holdCommand=-1;
    Vec3 m_targetOrigin{},m_targetAngles{},m_worldMove{};
    bool m_active=false,m_settling=false,m_trick=false;
    int m_finish=-1,m_expiry=-1,m_trickStart=-1,m_trickRoute=0,m_trickFrame=-1,m_nextCommand=-1;
    Vec3 m_nextMove{},m_nextOrigin{};
    std::array<Vec3,SpyKnife::TrickTicks+1> m_forecast{},m_trickPath{};
    int m_trickCount=0,m_trickPlanCommand=-1,m_warpBarrier=-1,m_rejectedCommand=-1;
    std::array<Vec3,SpyKnife::TrickTicks+1> m_trickMoves{};
    uint64_t m_settleDeadline=0;
    struct Observation
    {
        CHandle<CTFPlayer> target;int spawn=0,stable=3;
        float simulation=0,yaw=0,rate=0;uint64_t seen=0;bool unpredictable=false;
    };
    std::array<Observation,129> m_observations{};
    struct Attack
    {CHandle<CTFPlayer> target;CHandle<CTFWeaponBase> weapon;int command=-1,spawn=0;Vec3 origin{},angles{};float simulation=0;};
    Attack m_attack;
    CHandle<CTFPlayer> m_visualTarget;
    enum class Status {NoTarget,Ready,Front,Range,Blocked,Uncertain,Shield,Cooldown,Warp,Settling,Side,Feint};
    struct Display
    {
        std::array<Vec3,SpyKnife::TrickTicks+1> path{};
        Vec3 origin{},owner{},normal{0,0,1},forward{};
        float radius=80,safety=.75f,margin=0,yawError=0;int count=0;
        Status status=Status::NoTarget;Color_t color{};uint64_t expires=0;bool active=false,route=false;
    };
    CIndicatorCache<Display> m_display;
    Status m_status=Status::NoTarget;
    Observation* Observe(CTFPlayer* target);
    bool Reliable(CTFPlayer* target);
    bool PlanTrick(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target,CUserCmd* command);
    void FinishApproach(CTFPlayer* local,CUserCmd* command);
    void CacheVisual(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command);
    bool Eligible(CTFPlayer* local,CTFWeaponBase* weapon) const;
    bool ValidTarget(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target) const;
    bool Contact(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target,const Vec3& origin,
        const Vec3& currentAngles,Vec3& angles,Vec3& point,bool planning=false,const Vec3* predictedOwner=nullptr,bool fixedAngles=false,float yawOffset=0);
    bool PlanWarp(CTFPlayer* local,CTFWeaponBase* weapon,CTFPlayer* target,CUserCmd* command);
public:
    void Reset(bool clearTransport=false);
    bool Handles(CTFPlayer* local,CTFWeaponBase* weapon) const {return Eligible(local,weapon);}
    void Finalize(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command);
    void RenderWorld();
    void DrawHUD();
    bool Active() const {return m_active||m_settling||m_trick;}
    bool Controls(const CUserCmd* command) const {return Active()||(command&&command->command_number==m_holdCommand);}
    void Prepare(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command);
    void ApplyMovement(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command);
    void AfterAim(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command);
    bool Run(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* command,const std::vector<Target_t>& targets);
};
ADD_FEATURE(CSpyKnife, SpyKnife);
