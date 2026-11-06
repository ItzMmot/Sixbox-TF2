#include "EnginePrediction.h"

#include "../Ticks/Ticks.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"

// account for interp and origin compression when simulating local player
void CEnginePrediction::AdjustPlayers(CBaseEntity* pLocal)
{
	m_mRestore.clear();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer == pLocal || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
			continue;

		m_mRestore[pPlayer] = { pPlayer->GetAbsOrigin(), pPlayer->m_vecMins(), pPlayer->m_vecMaxs() };

		pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
		pPlayer->m_vecMins() += PLAYER_ORIGIN_COMPRESSION;
		pPlayer->m_vecMaxs() -= PLAYER_ORIGIN_COMPRESSION;
	}
}
void CEnginePrediction::RestorePlayers()
{
	for (auto& [pPlayer, tRestore] : m_mRestore)
	{
		pPlayer->SetAbsOrigin(tRestore.m_vOrigin);
		pPlayer->m_vecMins() = tRestore.m_vMins;
		pPlayer->m_vecMaxs() = tRestore.m_vMaxs;
	}
}

void CEnginePrediction::Simulate(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	const int nOldTickBase = pLocal->m_nTickBase();
	const bool bOldIsFirstPrediction = I::Prediction->m_bFirstTimePredicted;
	const bool bOldInPrediction = I::Prediction->m_bInPrediction;

	I::MoveHelper->SetHost(pLocal);
	pLocal->m_pCurrentCommand() = pCmd;
	G::RandomSeed() = MD5_PseudoRandom(pCmd->command_number) & std::numeric_limits<int>::max();

	I::Prediction->m_bFirstTimePredicted = false;
	I::Prediction->m_bInPrediction = true;
	I::Prediction->SetLocalViewAngles(pCmd->viewangles);

	AdjustPlayers(pLocal);
	I::Prediction->SetupMove(pLocal, pCmd, I::MoveHelper, &m_tMoveData);
	I::GameMovement->ProcessMovement(pLocal, &m_tMoveData);
	I::Prediction->FinishMove(pLocal, pCmd, &m_tMoveData);
	RestorePlayers();
	
	I::MoveHelper->SetHost(nullptr);
	pLocal->m_pCurrentCommand() = nullptr;
	G::RandomSeed() = -1;

	pLocal->m_nTickBase() = nOldTickBase;
	I::Prediction->m_bFirstTimePredicted = bOldIsFirstPrediction;
	I::Prediction->m_bInPrediction = bOldInPrediction;

	if (static int nLastTickBase = 0; nLastTickBase != nOldTickBase)
		F::MoveSim.StorePlayer(pLocal, m_tMoveData, TICKS_TO_TIME(nOldTickBase)), nLastTickBase = nOldTickBase;
}



void CEnginePrediction::Start(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bSnapshotValid = false;
	m_bInPrediction = true;
	if (!pLocal->IsAlive())
		return;

	auto pMap = pLocal->GetPredDescMap();
	if (!pMap)
		return;

	m_nOldTickCount = I::GlobalVars->tickcount;
	m_flOldCurrentTime = I::GlobalVars->curtime;
	m_flOldFrameTime = I::GlobalVars->frametime;

	I::GlobalVars->tickcount = pLocal->m_nTickBase();
	I::GlobalVars->curtime = TICKS_TO_TIME(I::GlobalVars->tickcount);
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;

	size_t iSize = pLocal->GetIntermediateDataSize();
	if (!m_tLocal.m_pData) 
	{
		m_tLocal.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));
		m_tLocal.m_iSize = iSize;
	}
	else if (m_tLocal.m_iSize != iSize)
	{
		m_tLocal.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Realloc(m_tLocal.m_pData, iSize));
		m_tLocal.m_iSize = iSize;
	}

	CPredictionCopy copy = { PC_EVERYTHING, m_tLocal.m_pData, PC_DATA_PACKED, pLocal, PC_DATA_NORMAL };
	copy.TransferData("EnginePredictionStart", pLocal->entindex(), pMap);
	m_hSnapshotOwner = pLocal;
	m_bSnapshotValid = true;

	Simulate(pLocal, pCmd);
}

bool CEnginePrediction::RestoreSimulationBase(CTFPlayer* player) const
{
	if (!m_bInPrediction || !m_bSnapshotValid || !player || m_hSnapshotOwner.Get() != player
		|| !m_tLocal.m_pData || player->GetIntermediateDataSize() != m_tLocal.m_iSize)
		return false;
	auto map = player->GetPredDescMap();
	if (!map) return false;
	CPredictionCopy copy = { PC_EVERYTHING, player, PC_DATA_NORMAL, m_tLocal.m_pData, PC_DATA_PACKED };
	copy.TransferData("EnginePredictionSimulationBase", player->entindex(), map);
	return true;
}

void CEnginePrediction::End(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bSnapshotValid)
	{
		RestoreSimulationBase(pLocal);
		I::GlobalVars->tickcount = m_nOldTickCount;
		I::GlobalVars->curtime = m_flOldCurrentTime;
		I::GlobalVars->frametime = m_flOldFrameTime;
	}
	m_bSnapshotValid = false;
	m_bInPrediction = false;
}

void CEnginePrediction::Unload()
{
	m_bSnapshotValid = m_bInPrediction = false;
	if (m_tLocal.m_pData)
	{
		I::MemAlloc->Free(m_tLocal.m_pData);
		m_tLocal = {};
	}
}