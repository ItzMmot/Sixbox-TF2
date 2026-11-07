#pragma once
#include "../../../SDK/SDK.h"
#include "../../ImGui/IndicatorCache.h"

namespace SpreadMath {struct Pellet_t;}

//#define SEEDPRED_DEBUG

class CNoSpreadHitscan
{
private:
	struct DrawCache_t
	{
		std::string m_sUptime = {};
		std::string m_sMantissaStep = {};
		std::string m_sDelta = {};
		Color_t m_tColor = {};
		bool m_bValid = false;
	};
	CIndicatorCache<DrawCache_t> m_tDrawCache = {};

	bool ShouldRun(CTFWeaponBase* pWeapon = nullptr);
	int GetSeed(CUserCmd* pCmd);
	float CalcMantissaStep(float flV);
	bool IsFixedSpread(CTFWeaponBase* pWeapon);
	std::string GetFormat(int iServerTime);

	int m_iPatternCommand=-1,m_iPatternTick=-1;
	CTFWeaponBase* m_pPatternWeapon=nullptr;
	bool m_bWaitingForPlayerPerf = false;
	int m_bSynced = 0;
	double m_dRequestTime = 0.0;
	float m_flServerTime = 0.f;
	double m_dTimeDelta = 0.0;
	std::deque<double> m_vTimeDeltas = {};

public:
	void Reset();
	int BuildAimPattern(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,SpreadMath::Pellet_t* output,int capacity,SpreadMath::Pellet_t& correction,bool& exact);

	void AskForPlayerPerf();
	bool ParsePlayerPerf(const std::string& sMsg);

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void CacheDrawInfo(CTFPlayer* pLocal);
	void Draw();

	int m_iSeed = 0;
	float m_flMantissaStep = 0.f;
	int m_iPredictionBullet = -1;
};

ADD_FEATURE(CNoSpreadHitscan, NoSpreadHitscan);