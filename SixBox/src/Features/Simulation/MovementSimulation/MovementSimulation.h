#pragma once
#include "../../../SDK/SDK.h"
#include <functional>
#include <array>

Enum(Move, Ground, Air, Swim)

using RunTickCallback = const std::function<void(CMoveData&)>;

struct MoveStorage
{
	CTFPlayer* m_pPlayer = nullptr;
	CMoveData m_MoveData = {};
	byte* m_pData = nullptr;
	byte* m_pExternalData=nullptr;
	size_t m_nExternalBytes=0;
	bool m_bStorePath=true;

	float m_flAverageYaw = 0.f;
	bool m_bBunnyHop = false;

	float m_flSimTime = 0.f;
	float m_flPredictedDelta = 0.f;
	float m_flPredictedSimTime = 0.f;
	bool m_bDirectMove = true;

	bool m_bPredictNetworked = true;
	Vec3 m_vPredictedOrigin = {};

	std::vector<Vec3> m_vPath = {};

	bool m_bFailed = false;
	bool m_bInitFailed = false;
};

struct MoveData
{
	Vec3 m_vDirection = {};
	float m_flSimTime = 0.f;
	int m_iMode = 0;
	Vec3 m_vVelocity = {};
	Vec3 m_vOrigin = {};
	bool m_bInputDirection = false;
};

struct MoveYawState
{
	int m_iChanges = 0;
	int m_iStart = 0;
	int m_iLastSign = 0;
	bool m_bLastZero = false;
};

class CMovementSimulation
{
private:
	bool Store(MoveStorage& tMoveStorage);
	void Reset(MoveStorage& tMoveStorage);

	void SetupMoveData(MoveStorage& tMoveStorage);
	bool CheckStuck(MoveStorage& tMoveStorage);
	void GetAverageYaw(MoveStorage& tMoveStorage, int iSamples);
	bool StrafePrediction(MoveStorage& tMoveStorage, bool bStrafe = true, bool bHitchance = false);

	void SetBounds(CTFPlayer* pPlayer);
	void RestoreBounds(CTFPlayer* pPlayer);

	bool m_bOldInPrediction = false;
	bool m_bOldFirstTimePredicted = false;
	float m_flOldFrametime = 0.f;

	std::unordered_map<int, std::deque<MoveData>> m_mRecords = {};
	std::unordered_map<int, std::deque<float>> m_mSimTimes = {};

public:
	void Store();
	void StorePlayer(CTFPlayer* pPlayer, CMoveData& tMoveData, float flTime);

	bool Initialize(CBaseEntity* pEntity, MoveStorage& tMoveStorage, bool bHitchance = true, bool bStrafe = true, bool bPredict = true, const Vec3* pvOrigin = nullptr);
	bool SetDuck(MoveStorage& tMoveStorage, bool bDuck);
	void RunTick(MoveStorage& tMoveStorage, bool bPath = true, RunTickCallback* pCallback = nullptr);
	void RunTick(MoveStorage& tMoveStorage, bool bPath, RunTickCallback fCallback);
	void Restore(MoveStorage& tMoveStorage);

	float GetPredictedDelta(CBaseEntity* pEntity);
	float GetMaxSimulationTime(float flDesiredMaxTime) const { return flDesiredMaxTime; }
};

ADD_FEATURE(CMovementSimulation, MoveSim);

// Exact, command-driven local probes. Unlike opponent extrapolation this does
// not average velocity, clear duck timers, predict strafes or force bunnyhop.
// A scope may start from EnginePrediction's pre-command snapshot; destruction
// always restores the caller's (possibly already predicted) state.
class CScopedLocalMovement
{
	CTFPlayer* m_player;
	datamap_t* m_map;
	std::vector<byte> m_saved, m_base;
	std::array<byte, sizeof(CTFGameMovement)> m_movement;
	std::array<byte, sizeof(CTFGameMovement)> m_baseMovement;
	CGlobalVarsBase m_clock;
	CUserCmd* m_oldCommand;
	CUserCmd m_command;
	CMoveData m_move = {};
	Vec3 m_absolute, m_velocity, m_angles;
	bool m_inPrediction, m_first, m_valid = false;
	int m_buttons = 0, m_tick = 0, m_baseTick = 0;
public:
	struct Snapshot
	{
		std::vector<byte> player;
		std::array<byte, sizeof(CTFGameMovement)> movement;
		CGlobalVarsBase clock;
	};
	static Snapshot Capture(CTFPlayer* player);
	CScopedLocalMovement(CTFPlayer* player, bool predictionBase, const Snapshot* snapshot = nullptr);
	~CScopedLocalMovement();
	CScopedLocalMovement(const CScopedLocalMovement&) = delete;
	CScopedLocalMovement& operator=(const CScopedLocalMovement&) = delete;
	bool Valid() const { return m_valid; }
	void Checkpoint();
	void Rewind();
	bool Step(const CUserCmd& command);
};
