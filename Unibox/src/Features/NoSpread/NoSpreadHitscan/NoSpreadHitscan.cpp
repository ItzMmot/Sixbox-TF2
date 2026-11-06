#include "NoSpreadHitscan.h"

#include "../SpreadMath.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../ImGui/IndicatorPanel.h"
#include <regex>
#include <numeric>

MAKE_SIGNATURE(CTFWeaponBaseGun_GetWeaponSpread, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 4C 63 91", 0x0);

static_assert(offsetof(IMatchGroupDescription, m_bFixedWeaponSpread) == 0x70);

void CNoSpreadHitscan::Reset()
{
	m_bWaitingForPlayerPerf = false;
	m_flServerTime = 0.f;
	m_vTimeDeltas.clear();
	m_dTimeDelta = 0.0;

	m_iSeed = 0;
	m_iPatternCommand=m_iPatternTick=-1;m_pPatternWeapon=nullptr;
	m_flMantissaStep = 0.f;

	m_bSynced = false;
}

bool CNoSpreadHitscan::ShouldRun(CTFWeaponBase* pWeapon)
{
	if (pWeapon
		? G::PrimaryWeaponType != EWeaponType::HITSCAN || pWeapon->GetWeaponSpread() <= 0.f
		: !Vars::Aimbot::General::NoSpread.Value || !I::EngineClient->IsInGame())
		return false;

	if (F::AntiCheatCompatibility.Active())
		return false;

	return true;
}

int CNoSpreadHitscan::GetSeed(CUserCmd* pCmd)
{
	static auto sv_usercmd_custom_random_seed = H::ConVars.FindVar("sv_usercmd_custom_random_seed");
	if (sv_usercmd_custom_random_seed && !sv_usercmd_custom_random_seed->GetBool())
		return pCmd->random_seed & 255;

	double dFloatTime = SDK::PlatFloatTime() + m_dTimeDelta;
	float flTime = float(dFloatTime * 1000.0);
	return *reinterpret_cast<int*>((char*)&flTime) & 255;
}

float CNoSpreadHitscan::CalcMantissaStep(float flV)
{
	float flMs = float(double(flV) * 1000.0);
	return std::nextafter(flMs, std::numeric_limits<float>::infinity()) - flMs;
}

bool CNoSpreadHitscan::IsFixedSpread(CTFWeaponBase* pWeapon)
{
	if (pWeapon->GetBulletsPerShot(false) <= 1 || !(pWeapon->GetDamageType() & DMG_BUCKSHOT))
		return false;

	static auto tf_use_fixed_weaponspreads = H::ConVars.FindVar("tf_use_fixed_weaponspreads");
	bool bFixed = tf_use_fixed_weaponspreads && tf_use_fixed_weaponspreads->GetBool();
	if (auto pGameRules = I::TFGameRules())
	{
		if (auto pMatchDesc = pGameRules->GetMatchGroupDescription())
			bFixed = pMatchDesc->m_bFixedWeaponSpread;
	}
	return bFixed || int(SDK::AttribHookValue(0.f, "fixed_shot_pattern", pWeapon));
}

std::string CNoSpreadHitscan::GetFormat(int iServerTime)
{
	int iDays = iServerTime / 86400;
	int iHours = iServerTime / 3600 % 24;
	int iMinutes = iServerTime / 60 % 60;
	int iSeconds = iServerTime % 60;

	if (iDays)
		return std::format("{}d {}h", iDays, iHours);
	else if (iHours)
		return std::format("{}h {}m", iHours, iMinutes);
	else
		return std::format("{}m {}s", iMinutes, iSeconds);
}

void CNoSpreadHitscan::AskForPlayerPerf()
{
	if (!ShouldRun())
		return Reset();

	if (G::Choking)
		return;

	static Timer tTimer = {};
	if (!m_bWaitingForPlayerPerf ? tTimer.Run(Vars::Aimbot::General::NoSpreadInterval.Value) : tTimer.Run(Vars::Aimbot::General::NoSpreadBackupInterval.Value))
	{
		I::ClientState->SendStringCmd("playerperf");
		m_bWaitingForPlayerPerf = true;
		m_dRequestTime = SDK::PlatFloatTime();
	}
}

bool CNoSpreadHitscan::ParsePlayerPerf(const std::string& sMsg)
{
	if (!ShouldRun())
		return false;

	std::smatch tMatch; std::regex_match(sMsg, tMatch, std::regex(R"((\d+.\d+)\s\d+\s\d+\s\d+.\d+\s\d+.\d+\svel\s\d+.\d+)"));

	if (tMatch.size() == 2)
	{
		m_bWaitingForPlayerPerf = false;

		float flNewServerTime = std::stof(tMatch[1].str());
		if (flNewServerTime < m_flServerTime)
			return true;

		bool bLoopback = SDK::IsLoopback();
		m_flServerTime = flNewServerTime;

		if (bLoopback)
			m_dTimeDelta = 0.f;
		else
		{
			m_vTimeDeltas.push_back(m_flServerTime - m_dRequestTime + TICKS_TO_TIME(1));
			while (!m_vTimeDeltas.empty() && m_vTimeDeltas.size() > Vars::Aimbot::General::NoSpreadAverage.Value)
				m_vTimeDeltas.pop_front();
			m_dTimeDelta = std::reduce(m_vTimeDeltas.begin(), m_vTimeDeltas.end()) / m_vTimeDeltas.size();
		}
		m_dTimeDelta += TICKS_TO_TIME(Vars::Aimbot::General::NoSpreadOffset.Value);

		float flMantissaStep = CalcMantissaStep(m_flServerTime);
		m_bSynced = flMantissaStep >= 1.f || bLoopback;

		if (flMantissaStep > m_flMantissaStep && (m_bSynced || !m_flMantissaStep))
		{
			SDK::Output("Seed Prediction", m_bSynced ? std::format("Synced ({})", m_dTimeDelta).c_str() : "Not synced, step too low", Vars::Menu::Theme::Accent.Value);
			SDK::Output("Seed Prediction", std::format("Age {}; Step {}", GetFormat(m_flServerTime), flMantissaStep).c_str(), Vars::Menu::Theme::Accent.Value);
		}
		m_flMantissaStep = flMantissaStep;

		return true;
	}

	return std::regex_match(sMsg, std::regex(R"(\d+.\d+\s\d+\s\d+)"));
}

int CNoSpreadHitscan::BuildAimPattern(CTFPlayer* local,CTFWeaponBase* weapon,CUserCmd* cmd,
	SpreadMath::Pellet_t* output,int capacity,SpreadMath::Pellet_t& correction,bool& exact)
{
	correction={};exact=false;
	if(!local||!weapon||!cmd||!output||capacity<=0) return 0;
	const int bullets=weapon->GetBulletsPerShot();
	const float spread=weapon->GetWeaponSpread();
	if(bullets<=0||bullets>32||!std::isfinite(spread)||spread<=0) return 0;
	const bool fixed=IsFixedSpread(weapon);
	static auto customSeed=H::ConVars.FindVar("sv_usercmd_custom_random_seed");
	exact=fixed||(m_bSynced&&Vars::Aimbot::General::NoSpread.Value)||(customSeed&&!customSeed->GetBool());
	if(m_iPatternCommand!=cmd->command_number || m_iPatternTick!=local->m_nTickBase() || m_pPatternWeapon!=weapon)
	{
		m_iSeed=GetSeed(cmd);m_iPatternCommand=cmd->command_number;
		m_iPatternTick=local->m_nTickBase();m_pPatternWeapon=weapon;
	}
	SpreadMath::Shot_t shot={m_iSeed,spread,bullets,fixed};
	if(!fixed&&TICKS_TO_TIME(local->m_nTickBase())-weapon->m_flLastFireTime()>(bullets>1?.25f:1.25f))
	{
		shot.m_flFirstRange=SDK::AttribHookValue(0.f,"mult_spread_scale_first_shot",weapon);
		shot.m_bFirstPerfect=shot.m_flFirstRange==0;
	}
	if(!std::isfinite(shot.m_flFirstRange)||shot.m_flFirstRange<0) return 0;
	const int seeds=exact?1:16;
	if(capacity<bullets*seeds) return 0;
	for(int seed=0;seed<seeds;++seed)
	{
		// Uniform prior over seeds when server timing is unknown; never used as exact nospread.
		if(!exact) shot.m_iSeed=seed*17;
		SpreadMath::GetPellets(shot,output+seed*bullets,bullets);
	}
	if(exact&&Vars::Aimbot::General::NoSpread.Value&&ShouldRun(weapon))
	{
		const int chosen=SpreadMath::ChoosePellet(output,bullets,{},shot.m_bFirstPerfect);
		correction=output[chosen];
	}
	return bullets*seeds;
}

void CNoSpreadHitscan::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_iPredictionBullet=-1;
	if(!ShouldRun(pWeapon)) return;
	std::array<SpreadMath::Pellet_t,512> pattern{};
	SpreadMath::Pellet_t pellet;bool exact=false;
	if(!BuildAimPattern(pLocal,pWeapon,pCmd,pattern.data(),int(pattern.size()),pellet,exact)||!exact) return;
	const int bullets=pWeapon->GetBulletsPerShot();
	const Vec3 punch=pLocal->m_vecPunchAngle(),shotAngles=pCmd->viewangles+punch;
	const bool firstPerfect=pellet.m_flRight==0&&pellet.m_flUp==0;
	m_iPredictionBullet=SpreadMath::ChoosePellet(pattern.data(),bullets,{},firstPerfect);
	if(!pellet.m_flRight&&!pellet.m_flUp) return;
	Vec3 forward;Math::AngleVectors(shotAngles,&forward);
	Vec3 angles=SpreadMath::SolveAngles(forward,pellet,shotAngles.z,shotAngles.x)-punch;
	SDK::FixMovement(pCmd,angles);pCmd->viewangles=angles;Math::ClampAngles(pCmd->viewangles);
	G::SilentAngles=true;
}

void CNoSpreadHitscan::CacheDrawInfo(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::SeedPrediction)
		|| !pLocal || !ShouldRun() || !pLocal->IsAlive())
	{
		m_tDrawCache.Set({});
		return;
	}

	m_tDrawCache.Set({
		std::format("Uptime {}", GetFormat(m_flServerTime)),
		std::format("Mantissa step {}", m_flMantissaStep),
		std::format("Delta {:.3f}", m_dTimeDelta),
		m_bSynced ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value,
		true
	});
}

void CNoSpreadHitscan::Draw()
{
	static IndicatorAnim_t tAnim;
	static DrawCache_t s_tLastValidCache;

	const DrawCache_t tCache = m_tDrawCache.Get();
	if (tCache.m_bValid)
		s_tLastValidCache = tCache;

	const bool bEnabled = (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::SeedPrediction) != 0;
	const bool bVisible = bEnabled && I::EngineClient->IsInGame() && tCache.m_bValid;

	bool bOpening = true;
	const float flAnim = tAnim.Update(bVisible, bOpening);
	if (!tAnim.ShouldDraw(bVisible))
		return;

	const auto& tDraw = tCache.m_bValid ? tCache : s_tLastValidCache;

	int x = Vars::Menu::SeedPredictionDisplay.Value.x;
	int y = Vars::Menu::SeedPredictionDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.IndicatorScale(1);
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.IndicatorScale(50, Scale_Round))
	{
		x -= H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.IndicatorScale(50, Scale_Round))
	{
		x += H::Draw.IndicatorScale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const int iVtxStart = pDrawList->VtxBuffer.Size;
	const int iCmdStart = pDrawList->CmdBuffer.Size;

	DrawIndicatorText(pDrawList, x, y, tDraw.m_tColor, Vars::Menu::Theme::Background.Value, align, tDraw.m_sUptime);
	DrawIndicatorText(pDrawList, x, y += nTall, tDraw.m_tColor, Vars::Menu::Theme::Background.Value, align, tDraw.m_sMantissaStep);
	if (Vars::Debug::Info.Value)
		DrawIndicatorText(pDrawList, x, y += nTall, tDraw.m_tColor, Vars::Menu::Theme::Background.Value, align, tDraw.m_sDelta);

	TransformIndicatorVertices(pDrawList, iVtxStart, pDrawList->VtxBuffer.Size, iCmdStart, pDrawList->CmdBuffer.Size, flAnim, bOpening, ImGui::GetIO().DisplaySize);
}
