#pragma once
#include "../../../SDK/SDK.h"
#include "DodgePolicy.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"

class CAutoDodge
{
	CHandle<CTFPlayer> m_owner;
	int m_spawn = -1, m_command = -1, m_control = -1, m_previousCommand = -1;
	int m_moves = 0, m_traces = 0, m_terrainTraces = 0, m_ticks = 0, m_leadTicks = 0;
	Dodge::TimeBudget<> m_budget;
	std::chrono::microseconds m_movementReserve{};
	bool m_minimumUsed = false, m_lateMinimumUsed = false;
	Vec3 m_checkedWish = {};
	int m_checkedButtons = 0;
	float m_checkedUpmove = 0;
	bool m_checkedInput = false;
	Vec3 m_wish = {}, m_previousWish = {};
	Vec3 m_escapeWish = {};
	int m_buttons = 0, m_previousButtons = 0;
	std::vector<Dodge::Threat> m_threats;
	// Never overwrite F::ProjSim's physics state or its current-info pointer.
	CProjectileSimulation m_sim;
	bool Eligible(CTFPlayer* local, const CUserCmd* command) const;
	bool Gather(CTFPlayer* local);
public:
	void Prepare(CTFPlayer* local, CUserCmd* command, bool predictionBase = false);
	bool Controls(const CUserCmd* command) const { return command && m_control == command->command_number; }
	void ApplyMovement(CUserCmd* command) const;
	void Reset();
	void Unload();
};

ADD_FEATURE(CAutoDodge, AutoDodge);
