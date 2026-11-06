#pragma once
#include "../../SDK/SDK.h"

// Copy only value state. Never shallow-copy the animstate's owning CUtlVector,
// interpolation history, vtable, or pointers to the player/gesture layers.
#define SIXBOX_ANIM_VALUES(X) \
    X(m_angRender) X(m_bPoseParameterInit) X(m_PoseParameterData) X(m_DebugAnimData) \
    X(m_bForceAimYaw) X(m_bCurrentFeetYawInitialized) X(m_flLastAnimationStateClearTime) \
    X(m_flEyeYaw) X(m_flEyePitch) X(m_flGoalFeetYaw) X(m_flCurrentFeetYaw) X(m_flLastAimTurnTime) \
    X(m_MovementData) X(m_bJumping) X(m_flJumpStartTime) X(m_bFirstJumpFrame) X(m_bInSwim) \
    X(m_bFirstSwimFrame) X(m_bDying) X(m_bFirstDyingFrame) X(m_eCurrentMainSequenceActivity) \
    X(m_nSpecificMainSequence) X(m_hActiveWeapon) X(m_flLastGroundSpeedUpdateTime) \
    X(m_flMaxGroundSpeed) X(m_nMovementSequence) X(m_LegAnimType) X(m_bInAirWalk) \
    X(m_flHoldDeployedPoseUntilTime) X(m_flTauntMoveX) X(m_flTauntMoveY) \
    X(m_flVehicleLeanVel) X(m_flVehicleLeanPos) X(m_vecSmoothedUp)

struct AnimationPose
{
#define SIXBOX_ANIM_DECLARE(name) decltype(CTFPlayerAnimState::name) name{};
    SIXBOX_ANIM_VALUES(SIXBOX_ANIM_DECLARE)
#undef SIXBOX_ANIM_DECLARE
    struct Gesture {GestureSlot_t slot{};CAnimationLayer layer{};bool hasLayer=false;};
    std::array<Gesture,GESTURE_SLOT_COUNT> gestures{};
    std::array<float,24> poses{};
    float cycle=0,animTime=0,playback=1;
    int sequence=0,gestureCount=0;
    bool valid=false;
    bool Store(CTFPlayer* player)
    {
        auto* native=player->m_PlayerAnimState();valid=false;
        if(!native||native->m_aGestureSlots.Count()>int(gestures.size())) return false;
#define SIXBOX_ANIM_STORE(name) name=native->name;
        SIXBOX_ANIM_VALUES(SIXBOX_ANIM_STORE)
#undef SIXBOX_ANIM_STORE
        gestureCount=native->m_aGestureSlots.Count();
        for(int i=0;i<gestureCount;++i)
        {
            auto& saved=gestures[i];saved.slot=native->m_aGestureSlots[i];
            saved.hasLayer=saved.slot.m_pAnimLayer!=nullptr;
            if(saved.hasLayer) saved.layer=*saved.slot.m_pAnimLayer;
        }
        poses=player->m_flPoseParameter();cycle=player->m_flCycle();sequence=player->m_nSequence();
        animTime=player->m_flAnimTime();playback=player->m_flPlaybackRate();valid=true;return true;
    }
    bool Restore(CTFPlayer* player) const
    {
        auto* native=player->m_PlayerAnimState();
        if(!valid||!native||native->m_aGestureSlots.Count()!=gestureCount) return false;
        for(int i=0;i<gestureCount;++i)
            if(native->m_aGestureSlots[i].m_pAnimLayer!=gestures[i].slot.m_pAnimLayer) return false;
#define SIXBOX_ANIM_RESTORE(name) native->name=name;
        SIXBOX_ANIM_VALUES(SIXBOX_ANIM_RESTORE)
#undef SIXBOX_ANIM_RESTORE
        for(int i=0;i<gestureCount;++i)
        {
            native->m_aGestureSlots[i]=gestures[i].slot;
            if(gestures[i].hasLayer) *native->m_aGestureSlots[i].m_pAnimLayer=gestures[i].layer;
        }
        player->m_flPoseParameter()=poses;player->m_flCycle()=cycle;player->m_nSequence()=sequence;
        player->m_flAnimTime()=animTime;player->m_flPlaybackRate()=playback;return true;
    }
};
#undef SIXBOX_ANIM_VALUES
