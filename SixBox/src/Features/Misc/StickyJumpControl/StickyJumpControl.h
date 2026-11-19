#pragma once
#include "../../../SDK/SDK.h"
#include "JumpController.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include <memory>

class CStickyJumpWorld;
class CStickyJumpControl
{
    StickyControl::Controller m_controller;
    std::unique_ptr<CStickyJumpWorld> m_world;
    KeyStorage m_key;
    int m_command = 0;
    CUserCmd m_intent;
    bool m_controls = false;
public:
    CStickyJumpControl();
    ~CStickyJumpControl();
    void Prepare(CTFPlayer* local, CTFWeaponBase* weapon, CUserCmd* command);
    void Apply(CTFPlayer* local, CUserCmd* command);
    void Reset();
    void Unload();
    bool Busy() const { return m_controller.Busy(); }
    bool Controls(const CUserCmd* cmd) const { return cmd && m_controls && cmd->command_number==m_command; }
    const StickyControl::Display& Snapshot() const { return m_controller.Snapshot(); }
    void Draw();
};

ADD_FEATURE(CStickyJumpControl, StickyJumpControl);
