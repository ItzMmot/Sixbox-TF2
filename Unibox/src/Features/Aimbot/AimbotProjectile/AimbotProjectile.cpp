#include "AimbotProjectile.h"
#include "../Shared/AimGeometry.h"

#include "../Aimbot.h"
#include "../../Ticks/Ticks.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../World/World.h"
#include "../AutoAirblast/AutoAirblast.h"
#include "../AutoHeal/AutoHeal.h"
#include "../../NavBot/BotUtils.h"
#include "../../NavBot/Objectives.h"
#include <array>
#include <bit>
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include <numeric>
#include "../../Configs/Configs.h"
#include <cstdio>

//#define SPLASH_DEBUG1 // trace splash visualization
//#define SPLASH_DEBUG2 // plane splash visualization
//#define SPLASH_DEBUG3 // points visualization
//#define SPLASH_DEBUG4 // test visualization
//#define SPLASH_DEBUG5 // trace/face count

#ifdef SPLASH_DEBUG5
static std::map<std::string, int> s_mTraceCount = {};
#endif
#ifdef SPLASH_DEBUG2
//#include "../../Debug/Debug.h"
#endif

namespace
{
	// Bounded local diagnostics for Rescue Ranger / grenade launcher regressions.
	// No format/IO in the trace loop: at most 32 small records, one per two seconds.
	struct BoltDiagnostic
	{
		bool active=false;
		const char* stage="target-init";
		int directs=0,solutions=0,tests=0,target=0,hit=-1,tick=0,limit=0,result=0;
		float fraction=1,flight=0,tolerance=0,interval=0,gap=0;
		int weapon=0;
	} s_boltDiagnostic;
	struct BoltDiagnosticScope
	{
		bool enabled;CUserCmd* command;
		BoltDiagnosticScope(bool enable,int weapon,int target,CUserCmd* cmd):enabled(enable),command(cmd)
		{
			if(enabled) {s_boltDiagnostic={};s_boltDiagnostic.active=true;s_boltDiagnostic.target=target;s_boltDiagnostic.weapon=weapon;}
		}
		~BoltDiagnosticScope()
		{
			if(!enabled) return;
			auto& d=s_boltDiagnostic;d.active=false;
			static ULONGLONG next=0;static int records=0;
			const auto now=GetTickCount64();if(now<next||records>=32) return;
			next=now+2000;++records;
			char path[MAX_PATH],line[768];
			const int pathSize=std::snprintf(path,sizeof(path),"%sProjectileRescue.txt",F::Configs.m_sConfigPath.c_str());
			if(pathSize<0||pathSize>=int(sizeof(path))) return;
			const int size=std::snprintf(line,sizeof(line),
				"projectile-fix-2 cmd=%d weapon=%d target=%d result=%d stage=%s directs=%d solutions=%d tests=%d hit=%d fraction=%.5f tick=%d/%d flight=%.5f tolerance=%.5f ready=%d autoshoot=%d requested_attack=%d original_attack=%d interval=%.6f end_gap=%.3f\r\n",
				G::OriginalCmd.command_number,d.weapon,d.target,d.result,d.stage,d.directs,d.solutions,d.tests,d.hit,d.fraction,d.tick,d.limit,d.flight,d.tolerance,
				int(G::CanPrimaryAttack),int(Vars::Aimbot::General::AutoShoot.Value),
				int(command&&bool(command->buttons&IN_ATTACK)),int(bool(G::OriginalCmd.buttons&IN_ATTACK)),d.interval,d.gap);
			if(size<=0||size>=int(sizeof(line))) return;
			const HANDLE file=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
			if(file==INVALID_HANDLE_VALUE) return;
			DWORD written;WriteFile(file,line,DWORD(size),&written,nullptr);CloseHandle(file);
		}
	};
}

// Use one time base: server samples when compensating network latency,
// rendered origin otherwise. Matches upstream's projectile prediction seed.
static Vec3 ProjectileTargetOrigin(CBaseEntity* entity,bool compensate)
{
	if(compensate&&entity->IsPlayer())
		if(const Vec3* networked=entity->m_iv_vecOrigin().m_LastNetworkedValue) return *networked;
	return entity->m_vecOrigin();
}

static inline std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	{
		auto eGroup = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
			eGroup = !SDK::FriendlyFire() || Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Team ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;

		bool bHeal = false, bCrossbow = false;
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_CROSSBOW:
			if (Vars::Aimbot::Healing::AutoArrow.Value)
				eGroup = eGroup != EntityEnum::Invalid ? EntityEnum::PlayerAll : EntityEnum::PlayerTeam;
			bHeal = bCrossbow = true;
			break;
		case TF_WEAPON_LUNCHBOX:
			if (Vars::Aimbot::Healing::AutoSandvich.Value)
				eGroup = EntityEnum::PlayerTeam;
			bHeal = true;
			break;
		}

		int iFunctionFlags = ShouldIgnoreEnum::Dormant | ShouldIgnoreEnum::Ignored;
		if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::TargetDormant)
			iFunctionFlags &= ~ShouldIgnoreEnum::Dormant;

		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon, iFunctionFlags))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && bHeal)
			{
				if (pEntity->As<CTFPlayer>()->m_iHealth() >= pEntity->As<CTFPlayer>()->GetMaxHealth() || (bCrossbow && pEntity->As<CTFPlayer>()->IsUbered())
					|| Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly && !H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex()))
					continue;
			}

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			if (bTeam && bHeal)
			{
				iPriority = pEntity->entindex() == F::AutoHeal.m_iTargetIdx ? std::numeric_limits<int>::max() : 0;
				switch (Vars::Aimbot::Healing::HealPriority.Value)
				{
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeFriends:
					if (H::Entities.IsFriend(pEntity->entindex()))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeParty:
					if (H::Entities.InParty(pEntity->entindex()))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeTeam:
					iPriority = std::numeric_limits<int>::max();
				}
			}
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}

		if (pWeapon->GetWeaponID() == TF_WEAPON_LUNCHBOX)
			return vTargets;
	}

	{
		auto eGroup = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
			eGroup = EntityEnum::BuildingEnemy;
		if (Vars::Aimbot::AutoEngie::AutoRepair.Value && pWeapon->GetWeaponID() == TF_WEAPON_SHOTGUN_BUILDING_RESCUE)
			eGroup = eGroup != EntityEnum::Invalid ? EntityEnum::BuildingAll : EntityEnum::BuildingTeam;
		for (auto pEntity : H::Entities.GetGroup(eGroup))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && (pEntity->As<CBaseObject>()->m_iHealth() >= pEntity->As<CBaseObject>()->m_iMaxHealth() || pEntity->As<CBaseObject>()->m_bBuilding()))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			int iPriority = 0;
			if (bTeam)
			{
				int iOwner = pEntity->As<CBaseObject>()->m_hBuilder().GetEntryIndex();
				switch (Vars::Aimbot::Healing::HealPriority.Value)
				{
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeFriends:
					if (iOwner == I::EngineClient->GetLocalPlayer() || H::Entities.IsFriend(iOwner))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeParty:
					if (H::Entities.InParty(iOwner))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeTeam:
					iPriority = std::numeric_limits<int>::max();
				}
			}

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies)
	{
		bool bShouldAim = false;
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_PIPEBOMBLAUNCHER:
			if (SDK::AttribHookValue(0, "stickies_detonate_stickies", pWeapon) == 1)
				bShouldAim = true;
			break;
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_FLAREGUN_REVENGE:
			if (pWeapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
				bShouldAim = true;
		}

		if (bShouldAim)
		{
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
			{
				if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				float flFOVTo; Vec3 vPos, vAngleTo;
				if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
					continue;

				float flDistTo = vLocalPos.DistToSqr(vPos);
				vTargets.emplace_back(pEntity, TargetEnum::Sticky, vPos, vAngleTo, flFOVTo, flDistTo);
			}
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs) // does not predict movement
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.EntityCenterInFOV(pEntity, vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistToSqr(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

static inline std::vector<Target_t> GetPlayers(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	{
		auto eGroupType = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
			eGroupType = !SDK::FriendlyFire() || Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Team ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;

		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistTo(vPos);
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	return vTargets;
}

float CAimbotProjectile::GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale)
{
	float flRadius = 0.f;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_LASER_POINTER:
		flRadius = TF_ROCKET_RADIUS;
		break;
	case TF_WEAPON_FLAREGUN:
	case TF_WEAPON_FLAREGUN_REVENGE:
		if (pWeapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
			flRadius = TF_FLARE_DET_RADIUS;
		break;
	case TF_WEAPON_JAR:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_JAR_GAS:
		return JAR_EXPLODE_RADIUS * flScale;
	}
	if (!flRadius)
		return 0.f;

	flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
			flRadius *= 0.8f;
	}
	return flRadius * flScale;
}

float CAimbotProjectile::GetSplashRadius(CBaseEntity* pProjectile, CTFWeaponBase* pWeapon, CTFPlayer* pPlayer, float flScale)
{
	float flRadius = 0.f;
	switch (pProjectile->GetClassID())
	{
	case ETFClassID::CTFWeaponBaseGrenadeProj:
	case ETFClassID::CTFWeaponBaseMerasmusGrenade:
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_SentryRocket:
	case ETFClassID::CTFProjectile_EnergyBall:
		flRadius = TF_ROCKET_RADIUS;
		break;
	case ETFClassID::CTFGrenadePipebombProjectile:
		if (pProjectile->As<CTFGrenadePipebombProjectile>()->HasStickyEffects())
			flRadius = TF_ROCKET_RADIUS;
		break;
	case ETFClassID::CTFProjectile_Flare:
		if (pWeapon && pWeapon->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_SCORCHSHOT)
			flRadius = TF_FLARE_DET_RADIUS;
		break;
	case ETFClassID::CTFProjectile_Jar:
	case ETFClassID::CTFProjectile_JarMilk:
	case ETFClassID::CTFProjectile_JarGas:
		return JAR_EXPLODE_RADIUS * flScale;
	}
	if (pPlayer && pWeapon)
	{
		flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
		switch (pProjectile->GetClassID())
		{
		case ETFClassID::CTFProjectile_Rocket:
		case ETFClassID::CTFProjectile_SentryRocket:
			if (pPlayer->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
				flRadius *= 0.8f;
		}
	}
	return flRadius * flScale;
}

static inline float ArmTime(CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::UseArmTime && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
		const float flLiveTime = tf_grenadelauncher_livetime->GetFloat();
		return SDK::AttribHookValue(flLiveTime, "sticky_arm_time", pWeapon);
	}

	return 0.f;
}

static inline bool ShouldLob(Info_t& tInfo)
{
	return tInfo.m_flGravity && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles;
}

static inline bool ShouldLob(MoveStorage& tMoveStorage, Info_t& tInfo)
{
	return tInfo.m_bIgnoreTiming && (tMoveStorage.m_bFailed || tMoveStorage.m_pPlayer->IsOnGround());
}

static inline bool AirSplash(CTFWeaponBase* pWeapon, Info_t& tInfo)
{
	if (!(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::AirSplash) || tInfo.m_pProjectile)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_FLAREGUN:
		return true;
	}

	return false;
}

static inline int GetHitboxPriority(int nHitbox, Target_t& tTarget, Info_t& tInfo, CBaseEntity* pProjectile = nullptr)
{
	if (!F::AimbotGlobal.IsHitboxValid(nHitbox, Vars::Aimbot::Projectile::Hitboxes.Value))
		return -1;

	int iHeadPriority = 0;
	int iBodyPriority = 1;
	int iFeetPriority = 2;

	if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Auto)
	{
		bool bHeadshot = Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::Head
			&& tTarget.m_iTargetType == TargetEnum::Player;
		if (bHeadshot)
		{
			if (!pProjectile)
				bHeadshot = tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
			else
				bHeadshot = pProjectile->GetClassID() == ETFClassID::CTFProjectile_Arrow && pProjectile->As<CTFProjectile_Arrow>()->CanHeadshot();

			if (Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::BodyaimIfLethal
				&& bHeadshot && !pProjectile && tInfo.m_pWeapon->m_hOwner().GetEntryIndex() == I::EngineClient->GetLocalPlayer())
			{
				float flCharge = I::GlobalVars->curtime - tInfo.m_pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
				float flDamage = Math::RemapVal(flCharge, 0.f, 1.f, 50.f, 120.f);
				if (tInfo.m_pLocal->IsMiniCritBoosted())
					flDamage *= 1.36f;
				if (flDamage >= tTarget.m_pEntity->As<CTFPlayer>()->m_iHealth())
					bHeadshot = false;

				if (tInfo.m_pLocal->IsCritBoosted()) // for reliability
					bHeadshot = false;
			}
		}
		if (bHeadshot)
			tTarget.m_nAimedHitbox = HITBOX_HEAD;

		bool bLower = Vars::Aimbot::Projectile::Hitboxes.Value & Vars::Aimbot::Projectile::HitboxesEnum::PrioritizeFeet
			&& tTarget.m_iTargetType == TargetEnum::Player && tTarget.m_pEntity->As<CTFPlayer>()->IsOnGround() /*&& tInfo.m_flRadius*/;

		iHeadPriority = bHeadshot ? 0 : 2;
		iBodyPriority = bHeadshot ? -1 : bLower ? 1 : 0;
		iFeetPriority = bHeadshot ? -1 : bLower ? 0 : 1;
	}

	switch (nHitbox)
	{
	case BOUNDS_HEAD: return iHeadPriority;
	case BOUNDS_BODY: return iBodyPriority;
	case BOUNDS_FEET: return iFeetPriority;
	}

	return -1;
};

void CAimbotProjectile::BeginSearch()
{
	const int command=G::CurrentUserCmd?G::CurrentUserCmd->command_number:-1;
	if(command!=m_iWorkCommand) {m_iWorkCommand=command;m_iAngleTests=0;m_iRefineTraces=0;m_iSeamPairs=0;m_iSeamSetups=0;m_iSeamShots=0;m_stickyWork={};}
}
bool CAimbotProjectile::ExtraSearchAvailable()
{
	LARGE_INTEGER now;QueryPerformanceCounter(&now);
	static const int64_t frequency=[](){LARGE_INTEGER value;QueryPerformanceFrequency(&value);return value.QuadPart;}();
	// Share the optional-search budget across all commands/targets in a rendered
	// frame, including Doubletap. One atomic native flight may finish past it.
	if(m_iExtraFrame!=I::GlobalVars->framecount)
	{
		m_iExtraFrame=I::GlobalVars->framecount;
		m_iExtraDeadline=now.QuadPart+(std::max)(frequency*3/2000,int64_t(1));
	}
	return now.QuadPart<m_iExtraDeadline;
}
bool CAimbotProjectile::HeadFirst(const Vec3& point,const Vec3& velocity,const Vec3& predictedOrigin,bool contact) const
{
	if(!m_pHeadBones||!m_pHeadSet||velocity.IsZero()||m_pHeadSet->numhitboxes>HITBOX_MAX) return false;
	Vec3 touch=point;
	if(!contact)
	{
		const Vec3 direction=velocity.Normalized();
		const Vec3 start=point-direction*128.f;
		const Vec3 hull=m_tInfo.m_vHull;
		const float entry=AimPolicy::RayBox(AimGeometry::Point(start),AimGeometry::Point(direction),
			AimGeometry::Point(predictedOrigin+m_tInfo.m_pTarget->m_pEntity->m_vecMins()-hull),
			AimGeometry::Point(predictedOrigin+m_tInfo.m_pTarget->m_pEntity->m_vecMaxs()+hull),256.f);
		if(entry<0) return false;
		touch=start+direction*entry;
	}
	const Vec3 position=touch-(predictedOrigin-m_vHeadOrigin);
	const int hit=AimPolicy::ArrowContactHitbox(AimGeometry::Point(position),AimGeometry::Point(velocity),TICK_INTERVAL,
		m_pHeadSet->numhitboxes,[&](int index) {
			auto* box=m_pHeadSet->pHitbox(index);
			if(!box||box->bone<0||box->bone>=MAXSTUDIOBONES) return AimPolicy::ArrowBox{};
			return AimPolicy::ArrowBox{AimGeometry::Point(box->bbmin),AimGeometry::Point(box->bbmax),m_pHeadBones[box->bone]};
		});
	return hit==HITBOX_HEAD;
}
bool CAimbotProjectile::FindHeadPoint(const Vec3& predictedOrigin,int tick,uint8_t flags,Vec3& point,Solution_t& solution)
{
	if(!m_pHeadBones||!m_pHeadSet||m_pHeadSet->numhitboxes<=HITBOX_HEAD) return false;
	auto* head=m_pHeadSet->pHitbox(HITBOX_HEAD);
	if(!head||head->bone<0||head->bone>=MAXSTUDIOBONES) return false;
	const Vec3 centre=Math::VectorTransform((head->bbmin+head->bbmax)*.5f,m_pHeadBones[head->bone])+predictedOrigin-m_vHeadOrigin;
	Vec3 forward;Math::AngleVectors({solution.m_flPitch,solution.m_flYaw,0},&forward);
	const auto flight=AimPolicy::FlightVelocity(AimGeometry::Point(forward*m_tInfo.m_flVelocity),m_tInfo.m_flGravity,solution.m_flTime);
	const Vec3 velocity{flight.x,flight.y,flight.z};
	const Vec3 normal=velocity.Normalized();
	Vec3 right=normal.Cross(std::abs(normal.z)>.99f?Vec3(0,1,0):Vec3(0,0,1));right.Normalize();
	const Vec3 up=right.Cross(normal);
	std::array<bool,49> safe{};
	// Test cheap hitbox geometry first. A ballistic/muzzle solve for every cell
	// multiplied expensive GetInfo/SetupClip work by 49 for each prediction tick.
	for(int i=0;i<49;++i)
	{
		const Vec3 candidate=centre+right*float(i%7-3)*2.f+up*float(i/7-3)*2.f;
		safe[i]=HeadFirst(candidate,velocity,predictedOrigin);
	}
	Solution_t selected;
	const int best=AimPolicy::RefineHeadCell(safe,[&](int cell) {
		const Vec3 candidate=centre+right*float(cell%7-3)*2.f+up*float(cell/7-3)*2.f;
		selected={};CalculateAngle(m_tInfo.m_vLocalEye,candidate,tick,selected,flags);
		if(selected.m_iCalculated==CalculateResultEnum::Time) return -1;
		if(selected.m_iCalculated!=CalculateResultEnum::Good) return 0;
		Vec3 direction;Math::AngleVectors({selected.m_flPitch,selected.m_flYaw,0},&direction);
		const auto flight=AimPolicy::FlightVelocity(AimGeometry::Point(direction*m_tInfo.m_flVelocity),m_tInfo.m_flGravity,selected.m_flTime);
		return HeadFirst(candidate,{flight.x,flight.y,flight.z},predictedOrigin)?1:0;
	});
	if(best<0) return false;
	point=centre+right*float(best%7-3)*2.f+up*float(best/7-3)*2.f;solution=selected;return true;
}

void CAimbotProjectile::BuildDirects(Directs_t* output)
{
	auto store=[&](int priority,Offset_t offset) {
		if (output) {(*output)[priority]=offset;return;}
		for (int i=0;i<m_iStickyDirectSeedCount;++i)
			if (m_aStickyDirectSeeds[i].priority==priority)
			{m_aStickyDirectSeeds[i]={offset.m_vOffset,{},PointTypeEnum::Direct,offset.m_iFlags,true,priority};return;}
		if (m_iStickyDirectSeedCount<int(m_aStickyDirectSeeds.size()))
			m_aStickyDirectSeeds[m_iStickyDirectSeedCount++]={offset.m_vOffset,{},PointTypeEnum::Direct,offset.m_iFlags,true,priority};
	};

	if (Vars::Aimbot::Projectile::SplashPrediction.Value == Vars::Aimbot::Projectile::SplashPredictionEnum::Only && m_tInfo.m_flRadius)
		return;

	auto& tTarget = *m_tInfo.m_pTarget;
	uint8_t iFlags = PointFlagsEnum::Regular;
	if (ShouldLob(m_tInfo))
		iFlags |= PointFlagsEnum::Lob;

	const Vec3 vMins = tTarget.m_pEntity->m_vecMins(), vMaxs = tTarget.m_pEntity->m_vecMaxs();
	for (int i = 0; i < 3; i++)
	{
		int iPriority = GetHitboxPriority(i, tTarget, m_tInfo, m_tInfo.m_pProjectile);
		if (iPriority == -1)
			continue;

		switch (i)
		{
		case BOUNDS_HEAD:
			if (tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				auto aBones = F::Backtrack.GetBones(tTarget.m_pEntity);
				if (!aBones)
					break;

				//Vec3 vOff = tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxOrigin(aBones, HITBOX_HEAD) - tTarget.m_pEntity->m_vecOrigin();

				// https://www.youtube.com/watch?v=_PSGD-pJUrM, might be better??
				Vec3 vCenter, vBBoxMins, vBBoxMaxs; tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxInfo(aBones, HITBOX_HEAD, &vCenter, &vBBoxMins, &vBBoxMaxs);
				Vec3 vOff = vCenter + (vBBoxMins + vBBoxMaxs) / 2 - tTarget.m_pEntity->m_vecOrigin();

				float flLow = 0.f;
				Vec3 vDelta = tTarget.m_vPos + m_tInfo.m_vTargetEye - m_tInfo.m_vLocalEye;
				if (vDelta.z > 0)
				{
					float flXY = vDelta.Length2D();
					if (flXY)
						flLow = Math::RemapVal(vDelta.z / flXY, 0.f, 0.5f, 0.f, 1.f);
					else
						flLow = 1.f;
				}

				float flLerp = (Vars::Aimbot::Projectile::HuntsmanLerp.Value + (Vars::Aimbot::Projectile::HuntsmanLerpLow.Value - Vars::Aimbot::Projectile::HuntsmanLerp.Value) * flLow) / 100.f;
				float flAdd = Vars::Aimbot::Projectile::HuntsmanAdd.Value + (Vars::Aimbot::Projectile::HuntsmanAddLow.Value - Vars::Aimbot::Projectile::HuntsmanAdd.Value) * flLow;
				vOff.z += flAdd;
				vOff.z = vOff.z + (vMaxs.z - vOff.z) * flLerp;

				vOff.x = std::clamp(vOff.x, vMins.x + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.x - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.y = std::clamp(vOff.y, vMins.y + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.y - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				vOff.z = std::clamp(vOff.z, vMins.z + Vars::Aimbot::Projectile::HuntsmanClamp.Value, vMaxs.z - Vars::Aimbot::Projectile::HuntsmanClamp.Value);
				store(iPriority,{vOff,iFlags});
			}
			else
				store(iPriority,{Vec3(0,0,vMaxs.z-Vars::Aimbot::Projectile::VerticalShift.Value),iFlags});
			break;
		case BOUNDS_BODY:
			store(iPriority,{Vec3(0,0,(vMaxs.z-vMins.z)/2),iFlags});
			break;
		case BOUNDS_FEET:
			store(iPriority,{Vec3(0,0,vMins.z+Vars::Aimbot::Projectile::VerticalShift.Value),iFlags});
			break;
		}
	}

	return;
}

Directs_t CAimbotProjectile::GetDirects()
{
	Directs_t result;BuildDirects(&result);return result;
}

Splashes_t CAimbotProjectile::GetSplashes()
{
	Splashes_t vSplashes = {};

	if (Vars::Aimbot::Projectile::SplashPrediction.Value == Vars::Aimbot::Projectile::SplashPredictionEnum::Off || !m_tInfo.m_flRadius)
		return vSplashes;

	vSplashes.push_back(PointFlagsEnum::Regular);
	if (ShouldLob(m_tInfo))
		vSplashes.push_back(PointFlagsEnum::Lob);

	return vSplashes;
}

static inline std::vector<Vec3> ComputePoints(float flRadius, int iSamples)
{
	std::vector<Vec3> vPoints = { { Vec3(0.f, 0.f, -1.f) * flRadius } };
	if (!iSamples)
		return vPoints;

	vPoints.reserve(iSamples + 1);

	float flRotateX = Vars::Aimbot::Projectile::SplashRotateX.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateX.Value;
	float flRotateY = Vars::Aimbot::Projectile::SplashRotateY.Value < 0.f ? SDK::StdRandomFloat(0.f, 360.f) : Vars::Aimbot::Projectile::SplashRotateY.Value;

	float a = Math::PI * (3.f - sqrtf(5.f));
	for (int n = 0; n < iSamples; n++)
	{
		float t = a * n;
		float y = 1 - (n / (iSamples - 1.f)) * 2;
		float r = sqrtf(1 - powf(y, 2));
		float x = cosf(t) * r;
		float z = sinf(t) * r;

		Vec3 vPoint = Vec3(x, y, z) * flRadius;
		vPoint = Math::RotatePoint(vPoint, {}, { flRotateX, flRotateY });

		vPoints.push_back(vPoint);
	}

	return vPoints;
};

#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2)
static inline void DrawTrace(bool bSuccess, Color_t tColor, CGameTrace& trace)
{
	Vec3 vMins = -Vec3::Get(bSuccess ? 1.f : 0.5f), vMaxs = Vec3::Get(bSuccess ? 1.f : 0.5f);
	Vec3 vAngles = Math::VectorAngles(trace.plane.normal);
	G::BoxStorage.emplace_back(trace.endpos, vMins, vMaxs, vAngles, I::GlobalVars->curtime + 60.f, tColor.Alpha(tColor.a / (bSuccess ? 1 : 10)), Color_t(0, 0, 0, 0));
	G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace.startpos, trace.endpos), I::GlobalVars->curtime + 60.f, tColor.Alpha(tColor.a / (bSuccess ? 1 : 10)));
};
#endif

static inline void HandleTrace(const Vec3& vPoint, std::vector<Setup_t>& vPoints, const Vec3& vTargetEye, Info_t& tInfo, std::function<bool()> fCheckPointTrace, std::function<bool()> fCheckPointAir, CGameTrace& trace, ITraceFilter& filter, int i = 0)
{
	// out
	SDK::TraceHull(vTargetEye, vPoint, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
	s_mTraceCount[__FUNCTION__": splash out"]++;
#endif

	if (fCheckPointAir())
		return;

	if (fCheckPointTrace())
#ifndef SPLASH_DEBUG1
		vPoints.emplace_back(trace.endpos);
#else
	{
		vPoints.emplace_back(trace.endpos);
		DrawTrace(true, Vars::Colors::IndicatorGood.Value, trace);
	}
	else
		DrawTrace(false, Vars::Colors::IndicatorGood.Value, trace);
#endif

	// in
	if ((tInfo.m_vLocalEye - vTargetEye).Dot(vTargetEye - vPoint) > 0.f)
		return;

	SDK::Trace(vPoint, vTargetEye, MASK_SHOT, &filter, &trace);
#ifdef SPLASH_DEBUG5
	s_mTraceCount[__FUNCTION__": splash in check"]++;
#endif
#ifdef SPLASH_DEBUG1
	DrawTrace(!trace.DidHit(), Vars::Colors::IndicatorMid.Value, trace);
#endif
	if (trace.DidHit())
		return;

	SDK::TraceHull(vPoint, vTargetEye, -tInfo.m_vHull, tInfo.m_vHull, MASK_SOLID, &filter, &trace);
#ifdef SPLASH_DEBUG5
	s_mTraceCount[__FUNCTION__": splash in"]++;
#endif

	if (fCheckPointTrace())
#ifndef SPLASH_DEBUG1
		vPoints.emplace_back(trace.endpos);
#else
	{
		vPoints.emplace_back(trace.endpos);
		DrawTrace(true, Vars::Colors::IndicatorBad.Value, trace);
	}
	else
		DrawTrace(false, Vars::Colors::IndicatorBad.Value, trace);
#endif
}

static float s_flTotal = 0.f;
static inline void HandleFace(Face_t& tFace, std::vector<Setup_t>& vPoints, float flDensity, float flRadius, float flCutoff, const Vec3& vTargetEye, const Vec3& vTargetCenter, const Vec3& vTargetOrigin, Info_t& tInfo, CGameTrace& trace, ITraceFilter& filter)
{
	float flRadiusSqr = powf(flRadius, 2);
	float flRadius2Sqr = flRadiusSqr * 4;
	int nMask = F::ProjSim.m_bPhysics ? MASK_SHOT | CONTENTS_DISPSOLID : MASK_SHOT;

	std::vector<Vec3> vVertices, vEpsilon;
	if (tFace.m_iType != FaceTypeEnum::Prop)
		vVertices = tFace.m_vVertices;
	else
		Math::ExpandPolygon(vVertices, tFace.m_vVertices, tFace.m_vNormal, SDK::StdRandomFloat(0.f, DIST_EPSILON), &vTargetEye);

	for (int i = 0, n = int(tFace.m_vVertices.size()), o = SDK::StdRandomInt(0, n - 1); ++i < n - 1;)
	{
		Vec3& vVertex1 = tFace.m_vVertices[o], &vVertex2 = tFace.m_vVertices[(o + i) % n], &vVertex3 = tFace.m_vVertices[(o + i + 1) % n];

		Vec3 vDir21 = vVertex2 - vVertex1, vDir31 = vVertex3 - vVertex1;
		float flArea = vDir21.Cross(vDir31).Length() / 2;
		float flSamples = flDensity * flArea / flRadius2Sqr;
		int iSamples = flSamples > flCutoff ? ceilf(flSamples) : fmodf(s_flTotal += flSamples, flCutoff) < flSamples;
		if (!iSamples)
			continue;

		// don't particularly like the hacky random epsilons
		int iFaceClosest = SDK::StdRandomInt(iSamples < 2 ? 0 : iSamples < 4 ? 1 : 2, iSamples < 2 ? 1 : 2);
		//int iEdgeClosest = SDK::StdRandomInt(iSamples < 8 ? 0 : iSamples < 16 ? 1 : 2, iSamples < 8 ? 1 : 2); // eats up a bit too much performance for my liking
		int iEdgeRandom = SDK::StdRandomInt(iSamples < 8 ? 0 : iSamples < 16 ? 1 : 2, iSamples < 3 && iFaceClosest ? 0 : iSamples < 12 ? 1 : 2);
		iEdgeRandom += iFaceClosest; //iEdgeClosest += iFaceClosest, iEdgeRandom += iEdgeClosest; //, iSamples += iEdgeRandom;
#ifdef SPLASH_DEBUG2
		Color_t tColor = { byte(SDK::StdRandomInt(0, 255)), byte(SDK::StdRandomInt(0, 255)), byte(SDK::StdRandomInt(0, 255)) };
#ifdef WORLD_DEBUG
		F::World.DrawFace({ { vVertex1, vVertex2, vVertex3 }, tFace.m_vNormal, tFace.m_iType }, DrawTypeEnum::Edges | DrawTypeEnum::Faces, tColor);
#endif
#ifdef DEBUG_TEXT
		F::Debug.AddText(std::format("{}:{}:{}"/*:{}"*/, iFaceClosest, /*iEdgeClosest,*/ iEdgeRandom, iSamples), (vVertex1 + vVertex2 + vVertex3) / 3, tColor);
#endif
#endif
		for (int s = 0; s < iSamples; s++)
		{
			Vec3 vPoint, vNormal = tFace.m_vNormal; bool bInside = true;
			if (s < iFaceClosest) // closest point
			{
				float flEpsilon = s == 0 && (tFace.m_iType == FaceTypeEnum::BoxBrush || iFaceClosest != 1 || SDK::StdRandomBool()) ? CALC_EPSILON : DIST_EPSILON;
				vPoint = Math::ClosestPointOnTriangle(vTargetOrigin, vVertex1, vVertex2, vVertex3, &bInside);
				vPoint += { SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon) };
			}
			//else if (s < iEdgeClosest) // closest point on edge
			//{
			//	float flEpsilon = SDK::StdRandomBool() ? CALC_EPSILON : DIST_EPSILON; bInside = false;
			//	switch (SDK::StdRandomInt(0, 2))
			//	{
			//	case 0: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex1, vVertex2); break;
			//	case 1: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex2, vVertex3); break;
			//	case 2: vPoint = Math::ClosestPointOnLine(vTargetOrigin, vVertex3, vVertex1); break;
			//	}
			//	vPoint += { SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon) };
			//}
			else if (s < iEdgeRandom) // random point on edge
			{
				float flEpsilon = SDK::StdRandomBool() ? CALC_EPSILON : DIST_EPSILON; bInside = false;
				switch (SDK::StdRandomInt(0, 2))
				{
				case 0: vPoint = vVertex1.Lerp(vVertex2, SDK::StdRandomFloat()); break;
				case 1: vPoint = vVertex2.Lerp(vVertex3, SDK::StdRandomFloat()); break;
				case 2: vPoint = vVertex3.Lerp(vVertex1, SDK::StdRandomFloat()); break;
				}
				vPoint += { SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon), SDK::StdRandomFloat(-flEpsilon, flEpsilon) };
			}
			else // random point on face
			{
				float flRandom1 = SDK::StdRandomFloat(), flRandom2 = SDK::StdRandomFloat();
				if (flRandom1 + flRandom2 > 1)
					flRandom1 = 1 - flRandom1, flRandom2 = 1 - flRandom2;
				vPoint = vVertex1 + vDir21 * flRandom1 + vDir31 * flRandom2;
			}
			vPoint += vNormal * (tInfo.m_vHull + CALC_EPSILON);
			if (vPoint.DistToSqr(vTargetCenter) > flRadiusSqr)
				continue;

			if (!bInside)
			{
				if (vEpsilon.empty())
					Math::ExpandPolygon(vEpsilon = vVertices, tFace.m_vNormal, -DIST_EPSILON);
				Math::ClosestPointOnPolygon(vPoint, vEpsilon, vNormal, &bInside);

				if (tFace.m_iType == FaceTypeEnum::Prop && !bInside)
					vPoint += tFace.m_vNormal * SDK::StdRandomFloat(0.f, DIST_EPSILON);
			}

#ifdef SPLASH_DEBUG5
			if (bInside) s_mTraceCount[__FUNCTION__": point contents"]++;
#endif
			if (bInside && I::EngineTrace->GetPointContents(vPoint) & MASK_SOLID)
				continue;

			int nSubMask = nMask;
			if (!bInside)
				vNormal = (vTargetEye - vPoint).Normalized(), nSubMask &= ~CONTENTS_MOVEABLE;

			SDK::Trace(vPoint + vNormal * tInfo.m_flNormalOffset, vTargetEye, nSubMask, &filter, &trace);
#ifdef SPLASH_DEBUG5
			s_mTraceCount[__FUNCTION__": vispos"]++;
#endif
#ifdef SPLASH_DEBUG2
			DrawTrace(trace.fraction == 1.f, Vars::Colors::IndicatorMisc.Value, trace);
#endif
			if (trace.fraction != 1.f)
				continue;

			vPoints.emplace_back(vPoint);
		}
	}
}

void CAimbotProjectile::SetupSplashPoints(Vec3& vOrigin, std::vector<Setup_t>& vSplashPoints, uint8_t iFlags)
{
	PROJ_ZONE(SplashSetup);

	vSplashPoints.clear();

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	m_tInfo.m_pTarget->m_vPos = vOrigin;
	Vec3 vTargetEye = vOrigin + m_tInfo.m_vTargetEye;
	Vec3 vTargetCenter = vOrigin + m_tInfo.m_pTarget->m_pEntity->GetOffset() / 2;
	float flRadius = m_tInfo.m_flRadius + m_tInfo.m_pTarget->m_pEntity->GetSize().Length() / 2;
	bool bAirSplash = AirSplash(m_tInfo.m_pWeapon, m_tInfo);

	auto fCheckNormal = [&](const Vec3& vNormal, const Vec3& vPoint, Vec3* pAngle = nullptr)
		{
			if (pAngle)
			{
				Vec3 vForward, vRight, vUp; Math::AngleVectors(*pAngle, &vForward, &vRight, &vUp);
				Vec3 vShootPos = m_tInfo.m_vLocalEye + vForward * m_tInfo.m_vOffset.x + vRight * m_tInfo.m_vOffset.y + vUp * m_tInfo.m_vOffset.z;
				vForward = (vShootPos - vPoint).Normalized();
				return vForward.Dot(vNormal) > 0;
			}
			else
			{
				Vec3 vForward = (m_tInfo.m_vLocalEye - vPoint).Normalized();
				return vForward.Dot(vNormal) > 0;
			}
		};

	// Trace
	int iPoints = Vars::Aimbot::Projectile::SplashMode.Value == Vars::Aimbot::Projectile::SplashModeEnum::Face && !bAirSplash ? 0
		: !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashPointsDirect.Value : Vars::Aimbot::Projectile::SplashPointsArc.Value;
	{
		auto vPoints = ComputePoints(flRadius, iPoints);

		for (int i = 0; i < vPoints.size(); i++)
		{
			auto fCheckPointTrace = [&]()
				{
					if (!trace.m_pEnt || trace.fraction == 1.f || trace.surface.flags & SURF_SKY || !trace.m_pEnt->GetAbsVelocity().IsZero())
						return false;

					Vec3 vPoint = trace.endpos, vAngle;
					if (!m_tInfo.m_flGravity)
						vAngle = Math::CalcAngle(m_tInfo.m_vLocalEye, trace.endpos);
					else
					{
						Point_t tPoint = { vPoint, {} };
						CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, 0, tPoint.m_tSolution, iFlags);
						if (tPoint.m_tSolution.m_iCalculated == CalculateResultEnum::Bad)
							return false;
						vPoint -= Vec3(0, 0, m_tInfo.m_flGravity * powf(tPoint.m_tSolution.m_flTime, 2) / 2);
						vAngle = Vec3(tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw);
					}
					return fCheckNormal(trace.plane.normal, vPoint, &vAngle);
				};

			auto fCheckPointAir = [&]()
				{
					if (bAirSplash && !trace.DidHit())
					{
						if (!Vars::Aimbot::Projectile::SplashAirCount.Value)
							vSplashPoints.emplace_back(vPoints[i] * SDK::StdRandomFloat() + vTargetCenter, PointTypeEnum::Air);
						else for (float r = 1; r < Vars::Aimbot::Projectile::SplashAirCount.Value; r++)
							vSplashPoints.emplace_back(vPoints[i] * r / Vars::Aimbot::Projectile::SplashAirCount.Value + vTargetCenter, PointTypeEnum::Air);
					}
					return Vars::Aimbot::Projectile::SplashMode.Value == Vars::Aimbot::Projectile::SplashModeEnum::Face && i || !trace.DidHit();
				};

			Vec3 vPoint = vPoints[i] + vTargetCenter;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, vPoint, 0, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculateResultEnum::Bad)
				continue;

			HandleTrace(vPoint, vSplashPoints, vTargetEye, m_tInfo, fCheckPointTrace, fCheckPointAir, trace, filter);
		}
	}

	// Face
	if (float flDensity = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashDensityDirect.Value : Vars::Aimbot::Projectile::SplashDensityArc.Value;
		Vars::Aimbot::Projectile::SplashMode.Value == Vars::Aimbot::Projectile::SplashModeEnum::Face && flDensity)
	{
		Vec3 vMins = vTargetCenter - flRadius, vMaxs = vTargetCenter + flRadius;

		NormalValidCallback fNormalValid = [&](const std::vector<Vec3>& vVertices, const Vec3& vNormal)
			{
				Vec3 vPoint = std::reduce(vVertices.begin(), vVertices.end()) / vVertices.size(), vAngle;
				if (!m_tInfo.m_flGravity)
					vAngle = Math::CalcAngle(m_tInfo.m_vLocalEye, vPoint);
				else
				{
					Point_t tPoint = { vPoint, {} };
					CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, 0, tPoint.m_tSolution, iFlags);
					if (tPoint.m_tSolution.m_iCalculated == CalculateResultEnum::Bad)
						return false;
					vPoint -= Vec3(0, 0, m_tInfo.m_flGravity * powf(tPoint.m_tSolution.m_flTime, 2) / 2);
					vAngle = Vec3(tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw);
				}
				return fCheckNormal(vNormal, vPoint, &vAngle);
			};
		F::World.SetNormalValidCallback(&fNormalValid);
		std::vector<Face_t> vFaces = F::World.GetFacesInAABB(vMins, vMaxs, MASK_SOLID, &filter);
		F::World.SetNormalValidCallback();
#ifdef SPLASH_DEBUG5
		SDK::Output("Faces", std::format("{}", vFaces.size()).c_str(), {}, OUTPUT_CONSOLE);
#endif

		float flCutoff = Vars::Aimbot::Projectile::SplashSamplesCutoff.Value * powf(vFaces.size(), 2); s_flTotal = SDK::StdRandomFloat();
		for (auto& tFace : vFaces)
		{
			HandleFace(tFace, vSplashPoints, flDensity, flRadius, flCutoff, vTargetEye, vTargetCenter, vOrigin, m_tInfo, trace, filter);
			//#if defined(SPLASH_DEBUG2) && defined(WORLD_DEBUG)
			//			F::World.DrawFace(tFace, DrawTypeEnum::Edges | DrawTypeEnum::Faces);
			//#endif
		}
	}

	if (vSplashPoints.size() > 1)
		std::shuffle(vSplashPoints.begin() + 1, vSplashPoints.end(), SDK::Random);
	PROJ_COUNT_N(SplashSetupPoints, int64_t(vSplashPoints.size()));

#ifdef SPLASH_DEBUG3
	for (auto& tSetup : vSplashPoints)
		G::BoxStorage.emplace_back(tSetup.m_vPoint, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + 60.f, Vars::Colors::Local.Value, Color_t(0, 0, 0, 0));
#endif
}

std::vector<Point_t> CAimbotProjectile::GetSplashPoints(Vec3 vOrigin, std::vector<Setup_t>& vSplashPoints, int iSimTime, uint8_t iFlags, bool bFirst)
{
	PROJ_ZONE(SplashPoints);

	std::vector<Point_t> vPoints = {};

	iSimTime -= m_tInfo.m_iLaunchDelay;
	m_tInfo.m_pTarget->m_vPos = vOrigin;
	Vec3 vTargetEye = vOrigin + m_tInfo.m_vTargetEye;
	float flRadiusSqr = powf(m_tInfo.m_flRadius, 2), flRadiusAirSqr = flRadiusSqr;
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::AirSplash && !m_tInfo.m_pProjectile && m_tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = H::ConVars.FindVar("tf_grenadelauncher_livetime");
		static auto tf_sticky_radius_ramp_time = H::ConVars.FindVar("tf_sticky_radius_ramp_time");
		static auto tf_sticky_airdet_radius = H::ConVars.FindVar("tf_sticky_airdet_radius");
		float flLiveTime = tf_grenadelauncher_livetime->GetFloat();
		float flRampTime = tf_sticky_radius_ramp_time->GetFloat();
		float flAirdetRadius = tf_sticky_airdet_radius->GetFloat();
		flRadiusAirSqr = powf(m_tInfo.m_flRadius * Math::RemapVal(TICKS_TO_TIME(iSimTime), flLiveTime, flLiveTime + flRampTime, flAirdetRadius, 1.f), 2);
	}
	bool bLob = iFlags & CalculateFlagsEnum::LobAngle;
	int iTolerance = m_tInfo.m_bIgnoreTiming && bLob ? std::numeric_limits<int>::max() : m_tInfo.m_iArmTime ? -1 : 0;
	int iLimit = !bFirst ? m_tInfo.m_iSplashRestrict : std::max(m_tInfo.m_iSplashRestrict, Vars::Aimbot::Projectile::SplashRestrictFirst.Value);
	bool bSort = !bLob || !m_tInfo.m_bIgnoreTiming;

	for (auto it = vSplashPoints.begin(); it != vSplashPoints.end();)
	{
		Point_t tPoint = { it->m_vPoint, {}, it->m_iType };

		CalculateAngle(m_tInfo.m_vLocalEye, tPoint.m_vPoint, iSimTime, tPoint.m_tSolution, iFlags, iTolerance);
		if (tPoint.m_tSolution.m_iCalculated != CalculateResultEnum::Good)
		{
			if (tPoint.m_tSolution.m_iCalculated == CalculateResultEnum::Bad)
				it = vSplashPoints.erase(it);
			else
				++it;
			continue;
		}
		else if (tPoint.m_iType == PointTypeEnum::Air && tPoint.m_tSolution.m_flTime < TICKS_TO_TIME(m_tInfo.m_iArmTime))
		{
			it = vSplashPoints.erase(it);
			continue;
		}

		vPoints.push_back(tPoint);
		it = vSplashPoints.erase(it);
		if (!bSort && vPoints.size() == iLimit)
			break;
	}
	if (vPoints.empty())
		return vPoints;

	if (bSort)
	{
		std::sort(vPoints.begin(), vPoints.end(), [&](const auto& a, const auto& b) -> bool
			{
				return a.m_vPoint.DistToSqr(vOrigin) < b.m_vPoint.DistToSqr(vOrigin);
			});
		vPoints.resize(std::min(iLimit, int(vPoints.size())));
	}

	const Vec3 vOriginal = m_tInfo.m_pTarget->m_pEntity->GetAbsOrigin();
	m_tInfo.m_pTarget->m_pEntity->SetAbsOrigin(vOrigin);
	for (auto it = vPoints.begin(); it != vPoints.end();)
	{
		auto& tPoint = *it;
		bool bValid = tPoint.m_tSolution.m_iCalculated != CalculateResultEnum::Pending;
		if (bValid)
		{
			Vec3 vPos = {}; m_tInfo.m_pTarget->m_pEntity->m_Collision()->CalcNearestPoint(tPoint.m_vPoint, &vPos);
			float flRadiusSqrCheck = tPoint.m_iType == PointTypeEnum::Geometry ? flRadiusSqr : flRadiusAirSqr;
			bValid = tPoint.m_vPoint.DistToSqr(vPos) < flRadiusSqrCheck;
		}

		if (bValid)
			++it;
		else
			it = vPoints.erase(it);
	}
	m_tInfo.m_pTarget->m_pEntity->SetAbsOrigin(vOriginal);

	return vPoints;
}

static inline Vec3 PullPoint(const Vec3& vPoint, Vec3 vLocalPos, Info_t& tInfo, const Vec3& vTargetPos, const Vec3& vMins, const Vec3& vMaxs)
{
	auto fHeightenLocalPos = [&]()
		{	// basic trajectory pass
			float flGrav = tInfo.m_flGravity;
			if (!flGrav)
				return vPoint;

			Vec3 vDelta = vTargetPos - vLocalPos;
			float flDist = vDelta.Length2D();

			float flRoot = powf(tInfo.m_flVelocity, 4) - flGrav * (flGrav * powf(flDist, 2) + 2.f * vDelta.z * powf(tInfo.m_flVelocity, 2));
			if (flRoot < 0.f)
				return vPoint;
			float flPitch = atan((powf(tInfo.m_flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));

			float flTime = flDist / (cos(flPitch) * tInfo.m_flVelocity) - tInfo.m_flOffsetTime;
			return vLocalPos + Vec3(0, 0, flGrav * powf(flTime, 2) / 2);
		};

	vLocalPos = fHeightenLocalPos();
	Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vPoint), &vForward, &vRight, &vUp);
	vLocalPos += (vForward * tInfo.m_vOffset.x) + (vRight * tInfo.m_vOffset.y) + (vUp * tInfo.m_vOffset.z);
	return Math::PullPoint(vPoint, vLocalPos, vTargetPos, vMins, vMaxs);
}



static inline float GetDrag(float flVelocity, ProjectileInfo* pInfo, int iFlags)
{	// stupid, would like not to hardcode magic values
	if (!(iFlags & CalculateFlagsEnum::AccountDrag))
		return 0.f;

	static float flLastVelocity = 0.f, flRegularDrag = 0.f, flLobDrag = 0.f;
	static uint32_t uLastType = 0;
	if (flLastVelocity != flVelocity || uLastType != pInfo->m_uType)
	{
		auto fGetDrag = [&](std::function<float()> fGetTypeDrag)
			{
				if (!F::ProjSim.m_bPhysics)
					return 0.f;

				if (Vars::Aimbot::Projectile::DragOverride.Value)
					return Vars::Aimbot::Projectile::DragOverride.Value;

				return fGetTypeDrag();
			};
		auto fGetRegularDrag = [&]()
			{
				switch (pInfo->m_uType)
				{
				case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
					if (!SDK::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
						return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.120f, 0.200f); // 0.120 normal, 0.200 capped, 0.300 v3000
					else
						return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.060f, 0.085f); // 0.060 normal, 0.085 capped, 0.120 v3000
				case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
					return Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.385f, 0.530f); // 0.385 normal, 0.530 capped, 0.790 v3000
				case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
					return Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.090f, 0.190f); // 0.085 low, 0.190 capped, 0.230 v2400
				case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
					return 0.310f;
				case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
					return 0.180f;
				case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
					return 0.285f;
				case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
				case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
				case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
				case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
					return 0.057f;
				case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
					return 0.530f;
				}
				return 0.f;
			};
		auto fGetLobDrag = [&]()
			{
				if (!(Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::LobAngles))
					return 0.f;

				switch (pInfo->m_uType)
				{
				case FNV1A::Hash32Const("models/weapons/w_models/w_grenade_grenadelauncher.mdl"):
					if (!SDK::AttribHookValue(0, "grenade_no_spin", pInfo->m_pWeapon))
						return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.056f, 0.062f);
					else
						return Math::RemapVal(flVelocity, 1217.f, k_flMaxVelocity, 0.030f, 0.033f);
				case FNV1A::Hash32Const("models/weapons/w_models/w_cannonball.mdl"):
					return Math::RemapVal(flVelocity, 1454.f, k_flMaxVelocity, 0.099f, 0.092f);
				case FNV1A::Hash32Const("models/weapons/w_models/w_stickybomb.mdl"):
					return Math::RemapVal(flVelocity, 922.f, k_flMaxVelocity, 0.048f, 0.060f);
				case FNV1A::Hash32Const("models/workshop_partner/weapons/c_models/c_sd_cleaver/c_sd_cleaver.mdl"):
					return 0.075f;
				case FNV1A::Hash32Const("models/weapons/w_models/w_baseball.mdl"):
					return 0.057f;
				case FNV1A::Hash32Const("models/weapons/c_models/c_xms_festive_ornament.mdl"):
					return 0.072f;
				case FNV1A::Hash32Const("models/weapons/c_models/urinejar.mdl"):
				case FNV1A::Hash32Const("models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl"):
				case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster.mdl"):
				case FNV1A::Hash32Const("models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl"):
					return 0.030f;
				case FNV1A::Hash32Const("models/weapons/c_models/c_gascan/c_gascan.mdl"):
					return 0.089f;
				}
				return 0.f;
			};

		flLastVelocity = flVelocity;
		uLastType = pInfo->m_uType;
		flRegularDrag = fGetDrag(fGetRegularDrag);
		flLobDrag = fGetDrag(fGetLobDrag);
	}

	return !(iFlags & CalculateFlagsEnum::LobAngle) ? flRegularDrag : flLobDrag;
}

bool SolveLaunch(const Vec3& vDelta, float flSpeed, const Vec3& vAdd, float flGravity, bool bLob, Vec3& vOut, float& flTime, float flDrag)
{
	auto fReach = [&](float t) -> float
		{
			return flDrag > 0.f ? logf(1.f + flDrag * flSpeed * t) / flDrag : flSpeed * t;
		};
	auto fError = [&](float t) -> float
		{
			Vec3 vReach = vDelta - vAdd * t + Vec3(0.f, 0.f, flGravity * t * t / 2.f);
			return vReach.LengthSqr() - powf(fReach(t), 2);
		};

	constexpr float flStep = 0.01f, flMax = 6.f;
	float flLow = 0.f, flFlat = -1.f, flLob = -1.f;
	float flLast = fError(flLow);
	for (float t = flStep; t <= flMax; t += flStep)
	{
		float flNow = fError(t);
		if (flLast >= 0.f != flNow >= 0.f)
		{
			float flA = t - flStep, flB = t;
			for (int i = 0; i < 24; i++)
			{
				float flMid = (flA + flB) / 2.f;
				if (fError(flMid) >= 0.f == flLast >= 0.f)
					flA = flMid;
				else
					flB = flMid;
			}
			float flRoot = (flA + flB) / 2.f;
			if (flNow >= 0.f)
				flLob = flRoot;
			else if (flFlat < 0.f)
				flFlat = flRoot;
		}
		flLast = flNow;
	}

	float flRoot = bLob ? flLob : flFlat;
	if (flRoot <= 0.f)
		return false;

	vOut = (vDelta - vAdd * flRoot + Vec3(0.f, 0.f, flGravity * flRoot * flRoot / 2.f)).Normalized();
	flTime = flRoot;
	return true;
}

static inline bool GetSpecialAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iFlags, Info_t& tInfo, float& flPitch, float& flYaw, float& flTime)
{
	const bool bLob = iFlags & CalculateFlagsEnum::LobAngle;
	Vec3 vForward = (vTargetPos - vLocalPos).Normalized();
	for (int i = 0; i < 3; i++)
	{
		const Vec3 vDelta = vTargetPos - (vLocalPos + vForward * tInfo.m_flSpawnDist + Vec3(0.f, 0.f, tInfo.m_flSpawnHeight));
		Vec3 vAim;
		if (!SolveLaunch(vDelta, tInfo.m_flVelocity, tInfo.m_vVelAdd, tInfo.m_flGravity, bLob, vAim, flTime, tInfo.m_flDrag))
			return false;

		if ((vAim * tInfo.m_flVelocity + tInfo.m_vVelAdd).Length() > k_flMaxVelocity)
		{
			Vec3 vLaunch;
			if (!SolveLaunch(vDelta, k_flMaxVelocity, {}, tInfo.m_flGravity, bLob, vLaunch, flTime, tInfo.m_flDrag))
				return false;

			const float flAlong = vLaunch.Dot(tInfo.m_vVelAdd), flRoot = flAlong * flAlong - tInfo.m_vVelAdd.LengthSqr() + powf(tInfo.m_flVelocity, 2);
			if (flRoot < 0.f)
				return false;
			vAim = (vLaunch * (flAlong + sqrtf(flRoot)) - tInfo.m_vVelAdd) / tInfo.m_flVelocity;
		}
		vForward = vAim.Normalized();
	}

	flYaw = Math::Rad2Deg(atan2f(vForward.y, vForward.x));
	flPitch = asinf(std::clamp(vForward.z, -1.f, 1.f));
	return true;
}

static inline bool GetAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iFlags, Info_t& tInfo, float& flPitch, float& flYaw, float& flTime)
{
	if (tInfo.m_pSpecial)
		return GetSpecialAngle(vLocalPos, vTargetPos, iFlags, tInfo, flPitch, flYaw, flTime);

	float flVelocity = tInfo.m_flVelocity;
	float flGrav = tInfo.m_flGravity;
	Vec3 vDelta = vTargetPos - vLocalPos;
	float flDist = vDelta.Length2D();
	float flDrag = GetDrag(flVelocity, F::ProjSim.m_pCurrent, iFlags);

	Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);

	flYaw = vAngleTo.y;

	if (!flGrav)
		flPitch = -Math::Deg2Rad(vAngleTo.x);
	else
	{
		float flRoot = powf(flVelocity, 4) - flGrav * (flGrav * powf(flDist, 2) + 2.f * vDelta.z * powf(flVelocity, 2));
		if (flRoot < 0.f)
			return false;

		if (!(iFlags & CalculateFlagsEnum::LobAngle))
			flPitch = atan((powf(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
		else
			flPitch = atan((powf(flVelocity, 2) + sqrt(flRoot)) / (flGrav * flDist));

		if (flDrag)
		{	// drag is handled in a stupid manner, would like something better than this
			float flTime = flDist / (flVelocity * cos(flPitch));
			flVelocity *= 1 - flDrag * flTime;

			flRoot = powf(flVelocity, 4) - flGrav * (flGrav * powf(flDist, 2) + 2.f * vDelta.z * powf(flVelocity, 2));
			if (flRoot < 0.f)
				return false;

			if (!(iFlags & CalculateFlagsEnum::LobAngle))
				flPitch = atan((powf(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
			else
				flPitch = atan((powf(flVelocity, 2) + sqrt(flRoot)) / (flGrav * flDist));

			//flVelocity = flVelocity / (flDrag * flTime) * (1.f - powf(Math::E, -flDrag * flTime));
			flVelocity = flVelocity / (1 + 0.5f * (1 - powf(Math::E, -flDrag * flTime)));
			flVelocity *= 1 + vDelta.z / flVelocity * TICK_INTERVAL * flTime;
		}
	}

	flTime = flDist / (flVelocity * cos(flPitch));

	if (Vars::Aimbot::Projectile::TimeOverride.Value)
		flTime *= Vars::Aimbot::Projectile::TimeOverride.Value;

	return true;
}

void CAimbotProjectile::CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, int iSimTime, Solution_t& tOut, uint8_t iFlags, int iTolerance)
{
	if (tOut.m_iCalculated != CalculateResultEnum::Pending)
		return;

	PROJ_ZONE(CalcAngle);
	PROJ_COUNT(CalcAngles);

	float flPitch, flYaw;
	{	// basic trajectory pass
		//Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vTargetPos), &vForward, &vRight, &vUp);
		//Vec3 vShootPos = vLocalPos + vForward * m_tInfo.m_vOffset.x + vRight * m_tInfo.m_vOffset.y + vUp * m_tInfo.m_vOffset.z;
		if (!GetAngle(vLocalPos, vTargetPos, iFlags, m_tInfo, flPitch, flYaw, tOut.m_flTime))
		{
			tOut.m_iCalculated = CalculateResultEnum::Bad;
			return;
		}

		tOut.m_flTime -= m_tInfo.m_flOffsetTime;
		tOut.m_flPitch = flPitch = -Math::Rad2Deg(flPitch) - m_tInfo.m_vAngFix.x;
		tOut.m_flYaw = flYaw -= m_tInfo.m_vAngFix.y;
	}

	int iTimeTo = ceilf(tOut.m_flTime / TICK_INTERVAL);
	bool bGood = iTolerance != -1 ? abs(iTimeTo - iSimTime) <= iTolerance : iTimeTo <= iSimTime;
	if (!(iFlags & CalculateFlagsEnum::TwoPass) || m_tInfo.m_vOffset.IsZero())
	{
		tOut.m_iCalculated = bGood ? CalculateResultEnum::Good : CalculateResultEnum::Time;
		return;
	}
	else if (!bGood)
	{
		tOut.m_iCalculated = CalculateResultEnum::Time;
		return;
	}

	int iSimFlags = (iFlags & CalculateFlagsEnum::SetupClip ? ProjSimEnum::Redirect : ProjSimEnum::None) | ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum;
#ifdef SPLASH_DEBUG5
	if (iSimFlags & ProjSimEnum::Redirect)
	{
		if (Vars::Visuals::Trajectory::Override.Value)
		{
			if (Vars::Visuals::Trajectory::ForwardRedirect.Value)
				s_mTraceCount[__FUNCTION__": setup trace"]++;
		}
		else
		{
			switch (m_tInfo.m_pWeapon->GetWeaponID())
			{
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_CROSSBOW:
			case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
			case TF_WEAPON_SYRINGEGUN_MEDIC:
				s_mTraceCount[__FUNCTION__": setup trace"]++;
			}
		}
	}
#endif
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(m_tInfo.m_pLocal, m_tInfo.m_pWeapon, { flPitch, flYaw, 0 }, m_tProjInfo, iSimFlags))
	{
		tOut.m_iCalculated = CalculateResultEnum::Bad;
		return;
	}
	if (m_iWeaponID == TF_WEAPON_LASER_POINTER)
		m_tProjInfo.m_vAng = { flPitch, flYaw, 0 };

	{	// calculate trajectory from projectile origin
		if (!GetAngle(m_tProjInfo.m_vPos, vTargetPos, iFlags, m_tInfo, tOut.m_flPitch, tOut.m_flYaw, tOut.m_flTime))
		{
			tOut.m_iCalculated = CalculateResultEnum::Bad;
			return;
		}

		{	// correct yaw
			Vec3 vShootPos = (m_tProjInfo.m_vPos - vLocalPos).To2D();
			Vec3 vTarget = vTargetPos - vLocalPos;
			Vec3 vForward; Math::AngleVectors(m_tProjInfo.m_vAng, &vForward); vForward.Normalize2D();
			float flB = 2 * (vShootPos.x * vForward.x + vShootPos.y * vForward.y);
			float flC = vShootPos.Length2DSqr() - vTarget.Length2DSqr();
			const float root=AimPolicy::LargerQuadraticRoot(1.f,flB,flC);
			if (std::isfinite(root))
			{
				vShootPos += vForward * root;
				tOut.m_flYaw = flYaw - (Math::Rad2Deg(atan2(vShootPos.y, vShootPos.x)) - flYaw);
				flYaw = Math::Rad2Deg(atan2(vShootPos.y, vShootPos.x));
			}
		}

		{	// correct pitch
			if (m_tInfo.m_flGravity)
			{
				flPitch -= m_tProjInfo.m_vAng.x;
				tOut.m_flPitch = -Math::Rad2Deg(tOut.m_flPitch) + flPitch - m_tInfo.m_vAngFix.x;
			}
			else
			{
				Vec3 vShootPos = Math::RotatePoint(m_tProjInfo.m_vPos - vLocalPos, {}, { 0, -flYaw, 0 }); vShootPos.y = 0;
				Vec3 vTarget = Math::RotatePoint(vTargetPos - vLocalPos, {}, { 0, -flYaw, 0 });
				Vec3 vForward; Math::AngleVectors(m_tProjInfo.m_vAng - Vec3(0, flYaw, 0), &vForward); vForward.y = 0; vForward.Normalize();
				float flB = 2 * (vShootPos.x * vForward.x + vShootPos.z * vForward.z);
				float flC = (powf(vShootPos.x, 2) + powf(vShootPos.z, 2)) - (powf(vTarget.x, 2) + powf(vTarget.z, 2));
				const float root=AimPolicy::LargerQuadraticRoot(1.f,flB,flC);
				if (std::isfinite(root))
				{
					vShootPos += vForward * root;
					tOut.m_flPitch = flPitch - (Math::Rad2Deg(atan2(-vShootPos.z, vShootPos.x)) - flPitch);
				}
			}
		}
	}

	iTimeTo = ceilf(tOut.m_flTime / TICK_INTERVAL);
	bGood = iTolerance != -1 ? abs(iTimeTo - iSimTime) <= iTolerance : iTimeTo <= iSimTime;
	PROJ_COUNT(CalcAnglesTwoPass);
	tOut.m_iCalculated = bGood ? CalculateResultEnum::Good : CalculateResultEnum::Time;
}



bool CAimbotProjectile::TestAngle(const Vec3& vPoint, const Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags, bool bSecondTest)
{
	BeginSearch();
	if(++m_iAngleTests>512) return false;

	PROJ_ZONE(TestAngle);
	PROJ_COUNT(TestAngles);
	if(s_boltDiagnostic.active) {++s_boltDiagnostic.tests;s_boltDiagnostic.stage="native-setup";}

	auto pLocal = m_tInfo.m_pLocal;
	auto pWeapon = m_tInfo.m_pWeapon;
	auto& tTarget = *m_tInfo.m_pTarget;

	int iSimFlags = ProjSimEnum::Redirect | ProjSimEnum::InitCheck | ProjSimEnum::PredictCmdNum | (Vars::Aimbot::General::NoSpread.Value ? ProjSimEnum::CorrectRandomAngles : ProjSimEnum::NoRandomAngles);
#ifdef SPLASH_DEBUG5
	if (Vars::Visuals::Trajectory::Override.Value)
	{
		if (Vars::Visuals::Trajectory::ForwardRedirect.Value)
			s_mTraceCount[__FUNCTION__": setup trace"]++;
	}
	else
	{
		switch (m_iWeaponID)
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_RAYGUN:
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_FLAREGUN_REVENGE:
		case TF_WEAPON_COMPOUND_BOW:
		case TF_WEAPON_CROSSBOW:
		case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
			s_mTraceCount[__FUNCTION__": setup trace"]++;
		}
	}
	s_mTraceCount[std::format(__FUNCTION__": setup clip ({}, {})", iType, iFlags)]++;
#endif
	if (m_bStickyValidation) ReleaseStickyPath();
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, m_tProjInfo, iSimFlags)) return false;
	if (m_iWeaponID == TF_WEAPON_LASER_POINTER) m_tProjInfo.m_vAng = Math::CalcAngle(m_tInfo.m_vLocalEye, vPoint);
	if (!F::ProjSim.Initialize(m_tProjInfo)) return false;
	if (m_bStickyValidation)
	{
		m_tProjInfo.m_vPath.swap(m_stickyValidationPath);m_tProjInfo.m_vPath.clear();m_bStickyPathBorrowed=true;
	}

	CGameTrace trace = {};
	bool bWrangler = m_iWeaponID == TF_WEAPON_LASER_POINTER;
	CBaseEntity* pSkip = (iType == PointTypeEnum::Direct ? (bWrangler ? m_pSentryGun->As<CBaseEntity>() : pLocal) : tTarget.m_pEntity);
	class FlightFilter final:public CTraceFilterCollideable
	{
	public:
		CTFPlayer* owner=nullptr;bool seam=false;float age=0;
		bool ShouldHitEntity(IHandleEntity* handle,int mask) override
		{
			if (seam&&handle&&!I::StaticPropMgr->IsStaticProp(handle))
			{
				auto* entity=reinterpret_cast<CBaseEntity*>(handle);
				if (entity->IsPlayer()) return SplashWindow::BlocksPlayer(entity==owner,entity==m_pSkip,
					entity->m_iTeamNum()==owner->m_iTeamNum(),age);
			}
			return CTraceFilterCollideable::ShouldHitEntity(handle,mask);
		}
	} filter;
	filter.m_pSkip=pSkip;filter.owner=pLocal;filter.seam=m_bSeamValidation&&m_tProjInfo.m_flGravity==0;
	filter.m_iPlayer = iType == PointTypeEnum::Direct ? PLAYER_DEFAULT : PLAYER_NONE;
	filter.m_iObject = iType != PointTypeEnum::Direct && bWrangler ? OBJECT_NONE : OBJECT_ALL;
	filter.m_bMisc = iType == PointTypeEnum::Direct;
	int nMask = MASK_SOLID;
	if (iType == PointTypeEnum::Direct && SDK::FriendlyFire())
	{
		switch (m_iWeaponID)
		{	// only weapons that actually hit teammates properly
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
		case TF_WEAPON_PARTICLE_CANNON:
		case TF_WEAPON_DRG_POMSON:
		case TF_WEAPON_FLAREGUN:
		case TF_WEAPON_SYRINGEGUN_MEDIC:
			filter.m_iPlayer = PLAYER_ALL;
		}
	}
	F::ProjSim.SetupTrace(filter, nMask, pWeapon);

#ifdef SPLASH_DEBUG4
	Vec3 vHull = m_tProjInfo.m_vHull.Max(1);
	G::BoxStorage.emplace_back(vPoint, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(0, 0, 0), Color_t(0, 0, 0, 0));
#endif

	if (!m_tProjInfo.m_flGravity&&!m_bSeamValidation)
	{
		SDK::TraceHull(m_tProjInfo.m_vPos, vPoint, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
#ifdef SPLASH_DEBUG5
		s_mTraceCount[__FUNCTION__": nograv trace"]++;
#endif
#ifdef SPLASH_DEBUG4
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(m_tProjInfo.m_vPos, trace.endpos), I::GlobalVars->curtime + 5.f, Color_t(0, 0, 0));
#endif
		if (Math::FullFraction(m_tProjInfo.m_vPos, vPoint, trace) < 0.999f && trace.m_pEnt != tTarget.m_pEntity)
			return false;
	}

#ifdef SPLASH_DEBUG4
	G::BoxStorage.pop_back();
#endif

	bool bDidHit = false;
	Vec3 vNew = F::ProjSim.GetOrigin();
	int iTimingTolerance = TIME_TO_TICKS(m_tInfo.m_flBoundsTime);
	if(s_boltDiagnostic.active) {s_boltDiagnostic.stage="native-flight";s_boltDiagnostic.limit=iSimTime;s_boltDiagnostic.tolerance=m_tInfo.m_flBoundsTime;}
	float flRadiusSqr = iType != PointTypeEnum::Direct ? powf(m_tProjInfo.m_flVelocity * TICK_INTERVAL + m_tProjInfo.m_vHull.z, 2) : std::numeric_limits<float>::max();
	uint8_t iTraceInterval = iFlags == PointFlagsEnum::Lob ? Vars::Aimbot::Projectile::LobTraceInterval.Value
		: iType != PointTypeEnum::Direct ? Vars::Aimbot::Projectile::SplashTraceInterval.Value
		: Vars::Aimbot::Projectile::DirectTraceInterval.Value;

	if (m_bSeamValidation||(m_iWeaponID==TF_WEAPON_COMPOUND_BOW&&tTarget.m_nAimedHitbox==HITBOX_HEAD)) iTraceInterval=1;
	const RestoreInfo_t tOriginal = { tTarget.m_pEntity->GetAbsOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	tTarget.m_pEntity->m_vecMins() = { std::clamp(tTarget.m_pEntity->m_vecMins().x, -24.f, 0.f), std::clamp(tTarget.m_pEntity->m_vecMins().y, -24.f, 0.f), tTarget.m_pEntity->m_vecMins().z };
	tTarget.m_pEntity->m_vecMaxs() = { std::clamp(tTarget.m_pEntity->m_vecMaxs().x, 0.f, 24.f), std::clamp(tTarget.m_pEntity->m_vecMaxs().y, 0.f, 24.f), tTarget.m_pEntity->m_vecMaxs().z };
	for (int n = 1; n <= iSimTime; n++)
	{
		if ((m_bStickyValidation||m_bSeamValidation)&&n>StickySearch::MaxTicks) break;
		F::ProjSim.RunTick(m_tProjInfo);

		if (bDidHit)
		{
			trace.endpos = F::ProjSim.GetOrigin();
			continue;
		}
		if (iTraceInterval != 1 && n % iTraceInterval && n != iSimTime)
			continue;

		Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
		filter.age=TICKS_TO_TIME(n-1);
		SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
		if (iType==PointTypeEnum::Direct&&trace.m_pEnt!=tTarget.m_pEntity
			&&filter.ShouldHitEntity(reinterpret_cast<IHandleEntity*>(tTarget.m_pEntity),nMask))
		{
			// A simulated target can leave its old spatial-partition leaf. Clip
			// the finite segment explicitly, keeping any nearer world/entity hit.
			const Vec3 mins=tTarget.m_vPos+tTarget.m_pEntity->m_vecMins()-m_tProjInfo.m_vHull;
			const Vec3 maxs=tTarget.m_vPos+tTarget.m_pEntity->m_vecMaxs()+m_tProjInfo.m_vHull;
			const float entry=AimPolicy::RayBox(AimGeometry::Point(vOld),AimGeometry::Point(vNew-vOld),
				AimGeometry::Point(mins),AimGeometry::Point(maxs),1.f);
			if (entry>=0&&entry<trace.fraction)
			{
				Ray_t ray;ray.Init(vOld,vNew,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull);
				CGameTrace targetTrace={};
				I::EngineTrace->ClipRayToCollideable(ray,nMask,tTarget.m_pEntity->m_Collision(),&targetTrace);
				if (targetTrace.DidHit()&&targetTrace.m_pEnt==tTarget.m_pEntity&&targetTrace.fraction<trace.fraction
					&&!trace.startsolid&&!trace.allsolid) trace=targetTrace;
			}
		}
#ifdef SPLASH_DEBUG5
		s_mTraceCount[std::format(__FUNCTION__": trace ({})", iTraceInterval)]++;
#endif
#ifdef SPLASH_DEBUG4
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vOld, trace.endpos), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 0));
#endif

		if(s_boltDiagnostic.active)
		{
			s_boltDiagnostic.tick=n;s_boltDiagnostic.hit=trace.m_pEnt?trace.m_pEnt->entindex():-1;s_boltDiagnostic.fraction=trace.fraction;
			s_boltDiagnostic.gap=vNew.DistTo(vPoint);
		}
		bool bHit = false;
		switch (iType)
		{
		case PointTypeEnum::Direct:
		case PointTypeEnum::Geometry:
			bHit = trace.DidHit(); break;
		case PointTypeEnum::Air:
			bHit = trace.endpos.DistToSqr(vPoint) < flRadiusSqr || trace.DidHit(); break;
		}

		if (bHit)
		{
			bool bValid = false, bTarget = true;
			switch (iType)
			{
			case PointTypeEnum::Direct:
				bTarget = trace.m_pEnt == tTarget.m_pEntity, bValid = bTarget && iSimTime - n < iTimingTolerance;
				if(s_boltDiagnostic.active) s_boltDiagnostic.stage=bValid?"hit":bTarget?"arrival-timing":"obstructed";
				break;
			case PointTypeEnum::Geometry:
				bValid = trace.endpos.DistToSqr(vPoint) < flRadiusSqr; break;
			case PointTypeEnum::Air:
				bValid = !trace.DidHit(); break;
			}

			if (bValid&&m_bSeamValidation&&iType!=PointTypeEnum::Direct)
			{
				const int index=m_iStickyPredictionBase+TIME_TO_TICKS(m_tInfo.m_flLatency)+n+m_tInfo.m_iLaunchDelay;
				if (!m_tMoveStorage.m_vPath.empty()&&index>=int(m_tMoveStorage.m_vPath.size())&&m_flTargetSpeed>1.f) bValid=false;
				else
				{
					m_vVerifiedContactTarget=StickyTargetAt(n+m_tInfo.m_iLaunchDelay);
					m_flVerifiedContactTime=TICKS_TO_TIME(n-1)+trace.fraction*TICK_INTERVAL;
					bValid=!trace.startsolid&&!trace.allsolid&&!(trace.surface.flags&(SURF_SKY|SURF_SKY2D))
						&&SplashDamageVisible(trace.endpos+trace.plane.normal*m_tInfo.m_flNormalOffset,m_vVerifiedContactTarget);
				}
			}
			if (bValid && iType != PointTypeEnum::Direct && !m_bSeamValidation)
			{
				CGameTrace trace2 = {};
				SDK::Trace(trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset, tTarget.m_vPos + m_tInfo.m_vTargetEye, MASK_SHOT, &filter, &trace2);
				bValid = trace2.fraction == 1.f;
#ifdef SPLASH_DEBUG5
				s_mTraceCount[__FUNCTION__": splash eye trace"]++;
#endif
#ifdef SPLASH_DEBUG4
				//G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace.endpos, trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 255));
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace2.startpos, trace2.endpos), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 255));
				G::BoxStorage.emplace_back(trace.endpos + m_tInfo.m_flNormalOffset * trace.plane.normal, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 255), Color_t(0, 0, 0, 0));
#endif
			}

#ifdef SPLASH_DEBUG4
			if (bValid)
				G::BoxStorage.emplace_back(vPoint, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(0, 255, 0), Color_t(0, 0, 0, 0));
			else
				G::BoxStorage.emplace_back(vPoint, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 0, 0), Color_t(0, 0, 0, 0));
#endif

			if (bValid && Vars::Aimbot::Projectile::IntervalRetest.Value && iTraceInterval != 1)
			{
				CGameTrace trace2 = {}; Vec3 vOld, vNew;
				int iTicks = int(m_tProjInfo.m_vPath.size());
				if (m_tInfo.m_flGravity)
					iTicks -= iTimingTolerance;

				for (int i = 1; i < iTicks; i++)
				{
					vOld = m_tProjInfo.m_vPath[i - 1], vNew = m_tProjInfo.m_vPath[i];
					SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace2);
					bValid = !trace2.DidHit();
#ifdef SPLASH_DEBUG5
					s_mTraceCount[__FUNCTION__": trace (retest)"]++;
#endif
#ifdef SPLASH_DEBUG4
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(trace2.startpos, trace2.endpos), I::GlobalVars->curtime + 5.f, Color_t(255, 255, 0));
					if (!bValid)
						G::BoxStorage.emplace_back(trace2.endpos, -vHull, vHull, Vec3(), I::GlobalVars->curtime + 5.f, Color_t(255, 255, 0), Color_t(0, 0, 0, 0));
#endif

					if (!bValid)
						break;
				}
			}

			if (bValid)
			{
				if (iTraceInterval != 1 && iType != PointTypeEnum::Direct)
				{
					int iInterval = n % iTraceInterval ? n % iTraceInterval : iTraceInterval;
					int iPopCount = ceilf(iInterval - trace.fraction * iInterval);
					for (int i = 0; i < iPopCount && !m_tProjInfo.m_vPath.empty(); i++)
						m_tProjInfo.m_vPath.pop_back();
				}

				if(Vars::Aimbot::Projectile::HuntsmanSafePoint.Value&&m_iWeaponID==TF_WEAPON_COMPOUND_BOW && tTarget.m_nAimedHitbox==HITBOX_HEAD
					&&!HeadFirst(trace.endpos,F::ProjSim.GetVelocity(),tTarget.m_vPos,true)) break;

				// attempted to have a headshot check though this seems more detrimental than useful outside of smooth aimbot
				if (!Vars::Aimbot::Projectile::HuntsmanSafePoint.Value&&tTarget.m_nAimedHitbox == HITBOX_HEAD && !bSecondTest &&
					(Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
					|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::SmoothVelocity
					|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Assistive))
				{	// loop and see if closest hitbox is head
					auto aBones = F::Backtrack.GetBones(tTarget.m_pEntity);
					if (!aBones)
						break;

					auto pSet = tTarget.m_pEntity->As<CTFPlayer>()->GetHitboxSet();
					if (!pSet)
						break;

					Vec3 vOffset = tOriginal.m_vOrigin - tTarget.m_vPos;
					Vec3 vPos = trace.endpos + F::ProjSim.GetVelocity().Normalized() * 16 + vOffset;

					float flLowestDistance = std::numeric_limits<float>::max(); int iClosest = -1;
					for (int nHitbox = 0; nHitbox < pSet->numhitboxes; ++nHitbox)
					{
						auto pBox = pSet->pHitbox(nHitbox);
						if (!pBox) continue;

						Vec3 vCenter; Math::VectorTransform({}, aBones[pBox->bone], vCenter);

						const float flDistance = vPos.DistToSqr(vCenter);
						if (flDistance < flLowestDistance)
							iClosest = nHitbox, flLowestDistance = flDistance;
					}
					if (iClosest != HITBOX_HEAD)
						break;
				}

				bDidHit = true;
			}
			else if (bTarget && iType == PointTypeEnum::Direct && m_tInfo.m_iArmTime)
			{	// run for more ticks to check for splash
				iSimTime = n + iTimingTolerance / 2;
				iType = PointTypeEnum::Geometry;
				filter.m_pSkip = tTarget.m_pEntity;
				filter.m_iPlayer = PLAYER_NONE;
				filter.m_bMisc = false;
				continue;
			}
			else
				break;

			if (iType == PointTypeEnum::Direct)
				trace.endpos = vNew;

			if (!bTarget || iType != PointTypeEnum::Direct)
				break;
		}
	}
	tTarget.m_pEntity->SetAbsOrigin(tOriginal.m_vOrigin);
	tTarget.m_pEntity->m_vecMins() = tOriginal.m_vMins;
	tTarget.m_pEntity->m_vecMaxs() = tOriginal.m_vMaxs;
	m_tProjInfo.m_vPath.push_back(trace.endpos);

	if (bDidHit)
	{
		m_vVerifiedImpact=trace.endpos;m_vVerifiedNormal=trace.plane.normal;
		PROJ_COUNT(TestAnglesHit);
	}

	return bDidHit;
}

bool CAimbotProjectile::HandlePoint(const Vec3& vOrigin, int iSimTime, float flPitch, float flYaw, float flTime, const Vec3& vPoint, uint8_t iType, uint8_t iFlags,float roll)
{
	PROJ_ZONE(HandlePoint);
	PROJ_COUNT(HandlePoints);

	iSimTime -= m_tInfo.m_iLaunchDelay;
	if(s_boltDiagnostic.active) {s_boltDiagnostic.flight=flTime;s_boltDiagnostic.interval=TICK_INTERVAL;s_boltDiagnostic.weapon=m_iWeaponID;}
	m_vPlainAngles = { flPitch, flYaw, roll };
	if (!m_bBlockAimAnglesDraw && m_flAimAnglesSetTime != I::GlobalVars->curtime)
	{
		m_vAimAngles = m_vPlainAngles;
		m_flAimAnglesSetTime = I::GlobalVars->curtime;
	}

	Vec3 acceptedOrigin=vOrigin;
	bool bReturn = false;
	bool bTested = false;
	if (m_iWeaponID == TF_WEAPON_LASER_POINTER) // Check if we can even laser point there
	{
		CGameTrace trace;
		CTraceFilterHitscan filter(H::Entities.GetLocal());
		SDK::Trace(m_vShootPos, vPoint, MASK_SHOT, &filter, &trace);

		if (trace.m_pEnt != m_tInfo.m_pTarget->m_pEntity && trace.endpos.DistTo(vPoint) > 70.f)
			return false;
	}

	Vec3 vAngles; Aim(G::CurrentUserCmd->viewangles, m_vPlainAngles, vAngles);
	m_tInfo.m_pTarget->m_vPos = vOrigin;

	int iOriginalSimTime = iSimTime;
	if (m_tInfo.m_iArmTime && iType == PointTypeEnum::Geometry || m_tInfo.m_bIgnoreTiming && iFlags == PointFlagsEnum::Lob)
	{
		if (flTime > m_tProjInfo.m_flLifetime)
			return false;

		iSimTime = std::ceil(flTime / I::GlobalVars->interval_per_tick); //TIME_TO_TICKS(flTime);
	}

	if (!m_tInfo.m_pProjectile
		? TestAngle(vPoint, vAngles, iSimTime, iType, iFlags)
		: TestAngle(m_tInfo.m_pProjectile, m_tInfo.m_vLocalEye, vPoint, vAngles, iSimTime, iType, iFlags))
	{
		if (m_bSeamValidation)
		{
			acceptedOrigin=m_vVerifiedContactTarget;flTime=m_flVerifiedContactTime;
			iOriginalSimTime=int(std::ceil(flTime/TICK_INTERVAL))+m_tInfo.m_iLaunchDelay;
		}
		if(!m_tInfo.m_pProjectile&&Vars::Aimbot::Projectile::SplashOptimization.Value&&iType!=PointTypeEnum::Direct && m_tInfo.m_flRadius>0)
		{
			auto* entity=m_tInfo.m_pTarget->m_pEntity;
			float radius=m_tInfo.m_flRadius;
			if(iType==PointTypeEnum::Air&&m_iWeaponID==TF_WEAPON_PIPEBOMBLAUNCHER)
			{
				static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime"),ramp=H::ConVars.FindVar("tf_sticky_radius_ramp_time"),airRadius=H::ConVars.FindVar("tf_sticky_airdet_radius");
				if(live&&ramp&&airRadius) radius*=Math::RemapVal(flTime,live->GetFloat(),live->GetFloat()+ramp->GetFloat(),airRadius->GetFloat(),1.f);
			}
			const float distance=AimPolicy::BoxDistance(AimGeometry::Point(m_vVerifiedImpact+m_vVerifiedNormal*m_tInfo.m_flNormalOffset),
				AimGeometry::Point(acceptedOrigin+entity->m_vecMins()),AimGeometry::Point(acceptedOrigin+entity->m_vecMaxs()));
			const float confidence=AimPolicy::Confidence(m_flTargetSpeed,flTime+m_tInfo.m_flLatency,(std::max)(radius-distance,1.f),m_flTargetTurnRate);
			const float score=AimPolicy::BlastScore(distance,radius,flTime+m_tInfo.m_flLatency)*(.25f+.75f*confidence/100.f);
			if(score<=m_flBestSplashScore) return false;
			m_flBestSplashScore=score;m_vBestImpact=m_vVerifiedImpact;m_vBestNormal=m_vVerifiedNormal;
		}
		bReturn = bTested = true;
		m_iResult = 1;
		m_flTimeTo = flTime + m_tInfo.m_flLatency;
	}
	else if (!bTested && !m_tInfo.m_pProjectile)
	{
		switch (Vars::Aimbot::General::AimType.Value)
		{
		case Vars::Aimbot::General::AimTypeEnum::Smooth:
			if (Vars::Aimbot::General::AssistStrength.Value == 100.f)
				break;
			[[fallthrough]];
		case Vars::Aimbot::General::AimTypeEnum::Assistive:
		{
			PROJ_COUNT(SecondTests);
			if (TestAngle(vPoint, m_vPlainAngles, iSimTime, iType, iFlags, true))
			{
				bReturn = true;
				m_iResult = 2;
			}
		}
		}
	}

	if (bReturn && m_bUpdate)
	{
		// We need to align the laser beam so we can hit airborne targets
		if (m_iWeaponID == TF_WEAPON_LASER_POINTER)
		{
			CGameTrace trace;
			CTraceFilterCollideable filter(m_pSentryGun);
			filter.m_iPlayer = PLAYER_NONE;

			Vec3 vForward; Math::AngleVectors(m_tProjInfo.m_vAng, &vForward);
			SDK::Trace(m_tInfo.m_vLocalEye, m_tInfo.m_vLocalEye + vForward * 10000.f, MASK_SHOT, &filter, &trace);

			m_vAngleTo = Math::CalcAngle(m_vShootPos, trace.endpos);
		}
		else m_vAngleTo = vAngles;

		m_vTarget = m_bSeamValidation?m_vVerifiedImpact:vPoint;
		m_vPredicted = acceptedOrigin;

		m_vPlayerPath.clear(), m_vProjectilePath = m_tProjInfo.m_vPath;
		if (m_tMoveStorage.m_vPath.empty())
			return true;

		if (m_tInfo.m_iArmTime && iType != PointTypeEnum::Air)
		{
			int iCount = std::min(iOriginalSimTime + TIME_TO_TICKS(m_tInfo.m_flLatency) + 1
				+ ((m_bStickyValidation||m_bSeamValidation)?m_iStickyPredictionBase:0), int(m_tMoveStorage.m_vPath.size()));
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
		}
		else
		{
			size_t iCount = std::min(m_tProjInfo.m_vPath.size() + TIME_TO_TICKS(m_tInfo.m_flLatency)
				+ ((m_bStickyValidation||m_bSeamValidation)?m_iStickyPredictionBase:0), m_tMoveStorage.m_vPath.size());
			m_vPlayerPath.insert(m_vPlayerPath.end(), m_tMoveStorage.m_vPath.begin(), m_tMoveStorage.m_vPath.begin() + iCount);
			m_vPredicted = m_vPlayerPath.back();
		}
	}

	return bReturn && m_iResult == 1;
}

void CAimbotProjectile::ResetSolution()
{
	m_iResult = false;
	m_vAngleTo = {};
	m_vTarget = {};
	m_vPredicted = {};
	m_vPlainAngles = {};
	m_flTimeTo = std::numeric_limits<float>::max();
	m_vPlayerPath.clear();
	m_vProjectilePath.clear();
	m_vBoxes.clear();
}

bool CAimbotProjectile::HandleDirect(DirectHistory_t& mDirectHistory)
{
	PROJ_ZONE(HandleDirect);

	bool bReturn = false;
	if (mDirectHistory.empty())
		return bReturn;

	auto it = mDirectHistory.begin();
	uint8_t iType = it->first;
	auto& vDirectHistory = it->second;

	if (F::ProjSim.m_bPhysics)
	{
		for (auto& tHistory : vDirectHistory)
		{
			if (I::EngineTrace->GetPointContents(tHistory.m_vPoint) & CONTENTS_WATER)
				tHistory.m_iPriority = COORD_EXTENT - tHistory.m_vPoint.z;
		}
	}
	std::sort(vDirectHistory.begin(), vDirectHistory.end(), [&](const Direct_t& a, const Direct_t& b) -> bool
		{
			const bool retryA=a.m_iAttempt>0,retryB=b.m_iAttempt>0;
			if(retryA!=retryB) return !retryA;
			if(a.m_iPriority!=b.m_iPriority) return a.m_iPriority<b.m_iPriority;
			return a.m_iAttempt<b.m_iAttempt;
		});
	m_flTimeTo = vDirectHistory.front().m_flTime + m_tInfo.m_flLatency;

	for (auto& tHistory : vDirectHistory)
	{
		if(tHistory.m_iAttempt>0&&!ExtraSearchAvailable()) break;
		if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tHistory.m_flPitch, tHistory.m_flYaw, tHistory.m_flTime, tHistory.m_vPoint, PointTypeEnum::Direct, iType))
		{
			bReturn = true;
			break;
		}
	}

	mDirectHistory.erase(it);
	return bReturn;
}

bool CAimbotProjectile::HandleSplashLegacy(SplashHistory_t& mSplashHistory)
{
	PROJ_ZONE(HandleSplash);

	bool bReturn = false;
	if (mSplashHistory.empty())
		return bReturn;

	auto it = mSplashHistory.begin();
	uint8_t iType = it->first;
	auto& vSplashHistory = it->second;

	std::sort(vSplashHistory.begin(), vSplashHistory.end(), [&](const Splash_t& a, const Splash_t& b) -> bool
		{
			return a.m_flTimeTo < b.m_flTimeTo;
		});
	uint8_t iFlags = CalculateFlagsEnum::None;
	if (iType == PointFlagsEnum::Lob)
		iFlags |= CalculateFlagsEnum::LobAngle;
	SetupSplashPoints(vSplashHistory.front().m_vOrigin, m_vSplashPoints, iFlags);
	if (!m_vSplashPoints.empty())
	{
		iFlags |= CalculateFlagsEnum::Accuracy;
		float flLowestDistance = std::numeric_limits<float>::max(); bool bFirst = true;
		for (auto& tHistory : vSplashHistory)
		{
			std::vector<Setup_t> vSplashCandidates = m_vSplashPoints;
			std::vector<Point_t> vSplashPoints = {};
			vSplashPoints = GetSplashPoints(tHistory.m_vOrigin, vSplashCandidates, tHistory.m_iSimtime, iFlags, bFirst); bFirst = false;
			PROJ_COUNT_N(SplashPointsGenerated, int64_t(vSplashPoints.size()));

			for (auto& tPoint : vSplashPoints)
			{
				float flDistance = tHistory.m_vOrigin.DistToSqr(tPoint.m_vPoint);
				if (flDistance > flLowestDistance)
					continue;

				if (HandlePoint(tHistory.m_vOrigin, tHistory.m_iSimtime, tPoint.m_tSolution.m_flPitch, tPoint.m_tSolution.m_flYaw, tPoint.m_tSolution.m_flTime, tPoint.m_vPoint, tPoint.m_iType, iType))
				{
					bReturn = true;
					flLowestDistance = flDistance;
				}
			}
			if (m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob)
				break;
		}
	}

	mSplashHistory.erase(it);
	return bReturn;
}

bool CAimbotProjectile::AimSplashContact(const Vec3& point,SeamAim& result)
{
	Vec3 angles=Math::CalcAngle(m_tInfo.m_vLocalEye,point);float best=std::numeric_limits<float>::max();bool found=false;
	for (int pass=0;pass<6&&m_iSeamSetups<1536&&ExtraSearchAvailable();++pass)
	{
		++m_iSeamSetups;m_tProjInfo={};
		if (!F::ProjSim.GetInfo(m_tInfo.m_pLocal,m_tInfo.m_pWeapon,angles,m_tProjInfo,
			ProjSimEnum::Redirect|ProjSimEnum::InitCheck|ProjSimEnum::NoRandomAngles|ProjSimEnum::PredictCmdNum)
			||!F::ProjSim.Initialize(m_tProjInfo,false)||F::ProjSim.m_bPhysics||m_tProjInfo.m_flGravity!=0) break;
		const Vec3 start=F::ProjSim.GetOrigin(),velocity=F::ProjSim.GetVelocity();const float speed=velocity.Length();
		if (!std::isfinite(speed)||speed<=0) break;
		const Vec3 direction=velocity/speed,delta=point-start;
		const float along=delta.Dot(direction);if (along<=0) break;
		const float miss=(delta-direction*along).LengthSqr();
		if (!std::isfinite(miss)||miss>=best) break;
		best=miss;result={angles,start,velocity,direction,m_tProjInfo.m_vHull,speed,m_tProjInfo.m_flLifetime};found=true;
		if (miss<1e-8f) break;
		const Vec3 wanted=Math::CalcAngle(start,point),actual=Math::VectorAngles(velocity);
		angles.x+=Math::NormalizeAngle(wanted.x-actual.x);angles.y+=Math::NormalizeAngle(wanted.y-actual.y);Math::ClampAngles(angles);
	}
	return found;
}

bool CAimbotProjectile::SplashDamageVisible(const Vec3& origin,const Vec3& targetOrigin)
{
	// Valve CTFRadiusDamageInfo::ApplyToEntity: ignore players/projectiles and
	// friendly combat items, retry the containing non-world entity once.
	class Filter final:public ITraceFilter
	{
	public:
		CBaseEntity* skip=nullptr;int team=0;bool projectiles=false;
		bool ShouldHitEntity(IHandleEntity* handle,int mask) override
		{
			if (!handle) return false;
			if (I::StaticPropMgr->IsStaticProp(handle)) return true;
			auto* entity=reinterpret_cast<CBaseEntity*>(handle);
			if (entity==skip||entity->IsPlayer()||(!projectiles&&entity->IsProjectile())) return false;
			if (entity->IsCombatItem()&&entity->m_iTeamNum()==team) return false;
			return entity->ShouldCollide(COLLISION_GROUP_PROJECTILE,mask);
		}
		TraceType_t GetTraceType() const override {return TRACE_EVERYTHING;}
	} filter;
	filter.team=m_tInfo.m_pLocal->m_iTeamNum();
	auto* target=m_tInfo.m_pTarget->m_pEntity;
	CGameTrace trace={};const Vec3 spot=targetOrigin+m_tInfo.m_vTargetEye;
	constexpr unsigned mask=MASK_SHOT&~CONTENTS_HITBOX;
	SDK::Trace(origin,spot,mask,&filter,&trace);
	if (trace.startsolid&&trace.m_pEnt&&trace.m_pEnt->entindex()!=0)
	{
		if (trace.m_pEnt->IsCombatItem()&&trace.m_pEnt!=target&&trace.m_pEnt->m_iTeamNum()==target->m_iTeamNum()) return false;
		filter.skip=trace.m_pEnt;filter.projectiles=true;SDK::Trace(origin,spot,mask,&filter,&trace);
	}
	return trace.fraction==1.f||trace.m_pEnt==target;
}

bool CAimbotProjectile::SearchSplashSeams(const Splash_t& frame,uint8_t mode,uint8_t flags)
{
	if (Vars::Aimbot::Projectile::SplashMode.Value!=Vars::Aimbot::Projectile::SplashModeEnum::Face
		||m_iSeamPairs>=8192||m_iAngleTests>=448||m_iSeamShots>=64||m_tInfo.m_flRadius<=0
		||!ExtraSearchAvailable()) return false;
	auto* entity=m_tInfo.m_pTarget->m_pEntity;
	const Vec3 mins=frame.m_vOrigin+entity->m_vecMins(),maxs=frame.m_vOrigin+entity->m_vecMaxs();
	const Vec3 centre=(mins+maxs)*.5f,reach=Vec3::Get(m_tInfo.m_flRadius+16.f),hull=m_tProjInfo.m_vHull;
	const int count=F::World.GatherSeamFaces(mins-reach,maxs+reach,m_aSeamFaces.data(),int(m_aSeamFaces.size()));
	struct Candidate {SeamGeometry::Edge edge;SeamGeometry::V point;float distance;};
	std::array<Candidate,16> candidates{};int used=0;
	auto add=[&](const SeamGeometry::Edge& edge,SeamGeometry::V point) {
		const float distance=AimPolicy::BoxDistance(point,AimGeometry::Point(mins),AimGeometry::Point(maxs));
		if (distance>m_tInfo.m_flRadius+std::abs(m_tInfo.m_flNormalOffset)+hull.Length()) return;
		for (int k=0;k<used;++k)
		{
			const auto delta=SeamGeometry::Sub(point,candidates[k].point);
			if (SeamGeometry::Dot(delta,delta)<.0001f&&std::abs(SeamGeometry::Dot(edge.normal,candidates[k].edge.normal))>.999f) return;
		}
		int slot=used;
		if (used==int(candidates.size()))
		{
			slot=0;for (int k=1;k<used;++k) if (candidates[k].distance>candidates[slot].distance) slot=k;
			if (distance>=candidates[slot].distance) return;
		}
		else ++used;
		candidates[slot]={edge,point,distance};
	};
	for (int i=0;i<count&&m_iSeamPairs<8192&&ExtraSearchAvailable();++i)
		for (int j=i+1;j<count&&m_iSeamPairs<8192;++j)
		{
			++m_iSeamPairs;SeamGeometry::Edge edge;
			if (!SeamGeometry::Intersect(m_aSeamFaces[i],m_aSeamFaces[j],edge)
				&&!SplashWindow::Boundary(m_aSeamFaces[i],m_aSeamFaces[j],AimGeometry::Point(centre),
					hull.Length()+std::abs(m_tInfo.m_flNormalOffset)+SplashWindow::Skin,edge)) continue;
			add(edge,SeamGeometry::Nearest(edge,AimGeometry::Point(centre)));add(edge,edge.first);add(edge,edge.last);
			// A rounded edge can leak past a third adjacent surface only over a
			// finite part of its span. Seed those crossings, not just its centre.
			const auto delta=SeamGeometry::Sub(edge.last,edge.first);
			for (int k=0;k<count;++k)
			{
				if (k==i||k==j) continue;
				const auto& wall=m_aSeamFaces[k];const float rate=SeamGeometry::Dot(wall.normal,delta);
				if (std::abs(rate)<1e-6f) continue;
				const float fraction=SeamGeometry::Dot(wall.normal,SeamGeometry::Sub(wall.vertices[0],edge.first))/rate;
				if (fraction<0||fraction>1) continue;
				const auto point=SeamGeometry::Add(edge.first,SeamGeometry::Mul(delta,fraction));
				const auto nearest=SeamGeometry::Nearest(wall,point),error=SeamGeometry::Sub(nearest,point);
				const float gap=hull.Length()+std::abs(m_tInfo.m_flNormalOffset)+SplashWindow::Skin;
				if (SeamGeometry::Dot(error,error)<=gap*gap) add(edge,point);
			}
		}
	std::sort(candidates.begin(),candidates.begin()+used,[](const Candidate& a,const Candidate& b){return a.distance<b.distance;});
	bool found=false;
	auto validate=[&](Vec3 point,Vec3 angles,float time,int ticks) {
		if (!ExtraSearchAvailable()||m_iSeamShots>=64||m_iAngleTests>=512||ticks<=0||ticks>StickySearch::MaxTicks
			||!F::AimbotGlobal.ShouldAimAtAngle(angles)||Math::CalcFov(I::EngineClient->GetViewAngles(),angles)>Vars::Aimbot::General::AimFOV.Value) return;
		++m_iSeamShots;
		struct Scope
		{
			CAimbotProjectile& parent;bool seam,borrow;
			~Scope(){parent.ReleaseStickyPath();parent.m_bSeamValidation=seam;parent.m_bStickyValidation=borrow;}
		} scope{*this,m_bSeamValidation,m_bStickyValidation};
		m_bSeamValidation=m_bStickyValidation=true;
		if (HandlePoint(frame.m_vOrigin,ticks+m_tInfo.m_iLaunchDelay,angles.x,angles.y,time,point,PointTypeEnum::Geometry,mode)) found=true;
	};
	for (int i=0;i<used&&m_iSeamShots<64&&m_iAngleTests<512&&ExtraSearchAvailable();++i)
	{
		const auto& candidate=candidates[i];const auto& edge=candidate.edge;
		const Vec3 base={candidate.point.x,candidate.point.y,candidate.point.z};
		const Vec3 normal={edge.normal.x,edge.normal.y,edge.normal.z},wall={edge.wall.x,edge.wall.y,edge.wall.z};
		Vec3 axis={edge.last.x-edge.first.x,edge.last.y-edge.first.y,edge.last.z-edge.first.z};const float length=axis.Length();
		if (length>1e-6f) axis/=length;
		if (m_tInfo.m_flGravity==0&&length>1e-6f&&m_iSeamSetups<1536)
		{
			struct Seed {Vec3 point{};double width=0;};std::array<Seed,2> seeds{};
			Vec3 outward=m_tInfo.m_vLocalEye-base;outward-=axis*outward.Dot(axis);outward.Normalize();
			const float limit=m_tInfo.m_flVelocity*F::MoveSim.GetMaxSimulationTime(Vars::Aimbot::Projectile::MaxSimulationTime.Value);
			for (int side=0;side<4;++side)
			{
				const auto arc=SplashWindow::Arc(edge.normal,edge.wall,float(side)*.5f);
				const Vec3 radial=side==3?outward:Vec3(arc.x,arc.y,arc.z);
				for (int depth=1;depth<8;++depth)
				{
					float offset=std::numeric_limits<float>::max();
					for (int component=0;component<3;++component) if (std::abs(radial[component])>1e-6f)
						offset=(std::min)(offset,(hull[component]+SplashWindow::Skin*float(depth)/8.f)/std::abs(radial[component]));
					if (!std::isfinite(offset)) continue;
					const Vec3 point=base+radial*offset,direction=(point-m_tInfo.m_vLocalEye).Normalized();
					const auto window=hull.LengthSqr()>0?SplashWindow::Hull(edge,AimGeometry::Point(m_tInfo.m_vLocalEye),AimGeometry::Point(direction),AimGeometry::Point(hull),limit)
						:SplashWindow::Capsule(edge,AimGeometry::Point(m_tInfo.m_vLocalEye),AimGeometry::Point(direction),SplashWindow::Skin,limit);
					if (!window.valid) continue;const double width=window.high-window.low;
					if (width>seeds[0].width) {seeds[1]=seeds[0];seeds[0]={point,width};}
					else if (width>seeds[1].width) seeds[1]={point,width};
				}
			}
			for (const auto& seed:seeds)
			{
				if (seed.width<=0) continue;
				for (int round=0;round<2&&m_iSeamShots<64&&m_iSeamSetups<1536;++round)
				{
					Vec3 point=seed.point;int tick=0;
					for (int pass=0;pass<4&&m_iSeamSetups<1536;++pass)
					{
						SeamAim launch;if (!AimSplashContact(point,launch)) break;
						const float maximum=launch.speed*(std::min)(launch.lifetime,F::MoveSim.GetMaxSimulationTime(Vars::Aimbot::Projectile::MaxSimulationTime.Value));
						const auto window=launch.hull.LengthSqr()>0?SplashWindow::Hull(edge,AimGeometry::Point(launch.start),AimGeometry::Point(launch.direction),AimGeometry::Point(launch.hull),maximum)
							:SplashWindow::Capsule(edge,AimGeometry::Point(launch.start),AimGeometry::Point(launch.direction),SplashWindow::Skin,maximum);
						if (!window.valid) break;
						const float step=launch.speed*TICK_INTERVAL;
						if (!tick) tick=round?int(std::ceil((window.low+window.high)*.5/step)):int(std::floor((window.low+window.high)*.5/step));
						if (tick<=0||tick>=StickySearch::MaxTicks||TICKS_TO_TIME(tick)>launch.lifetime) break;
						Vec3 end=launch.start;for (int at=0;at<tick;++at) end+=launch.velocity*TICK_INTERVAL;
						const auto contact=launch.hull.LengthSqr()>0?SplashWindow::Hull(edge,AimGeometry::Point(end),AimGeometry::Point(launch.direction),AimGeometry::Point(launch.hull),.001f)
							:SplashWindow::Capsule(edge,AimGeometry::Point(end),AimGeometry::Point(launch.direction),SplashWindow::Skin,.001f);
						if (contact.valid&&contact.low<1e-5)
						{validate(point,launch.angles,TICKS_TO_TIME(tick+1),tick+1);break;}
						const float rate=axis.Dot(launch.direction);if (std::abs(rate)<1e-6f) break;
						const float shift=((end-launch.start).Dot(launch.direction)-float((window.low+window.high)*.5))/rate;
						const Vec3 first={edge.first.x,edge.first.y,edge.first.z};
						const float along=(point-first).Dot(axis),moved=std::clamp(along+shift,0.f,length)-along;
						if (!std::isfinite(moved)||std::abs(moved)<1e-5f) break;
						point+=axis*moved;
						if (AimPolicy::BoxDistance(AimGeometry::Point(point),AimGeometry::Point(mins),AimGeometry::Point(maxs))>m_tInfo.m_flRadius+launch.hull.Length()+1.f) break;
					}
				}
			}
		}
		// Flat brush/displacement contact remains useful where no rounded
		// endpoint exists. Every trial uses the same actual short-sweep gate.
		auto support=[&](const Vec3& n){return std::abs(n.x)*hull.x+std::abs(n.y)*hull.y+std::abs(n.z)*hull.z;};
		for (int side=0;side<2&&m_iSeamShots<64;++side)
		{
			const Vec3 face=side?wall:normal,across=side?normal:wall;
			for (float fraction:{0.f,.5f,1.f})
			{
				if (m_iSeamShots>=64||m_iAngleTests>=512) break;
				const Vec3 point=base+face*(support(face)+SplashWindow::Skin)+across*(support(across)*fraction+SplashWindow::Skin);
				Solution_t solution;CalculateAngle(m_tInfo.m_vLocalEye,point,frame.m_iSimtime-m_tInfo.m_iLaunchDelay,solution,
					flags|CalculateFlagsEnum::TwoPass,m_tInfo.m_bIgnoreTiming&&mode==PointFlagsEnum::Lob?std::numeric_limits<int>::max():-1);
				if (solution.m_iCalculated==CalculateResultEnum::Good)
					validate(point,{solution.m_flPitch,solution.m_flYaw,0},solution.m_flTime,frame.m_iSimtime-m_tInfo.m_iLaunchDelay);
			}
		}
	}
	return found;
}

void CAimbotProjectile::ResetMoveStorage()
{
	auto path=std::move(m_tMoveStorage.m_vPath);
	m_tMoveStorage={};m_tMoveStorage.m_vPath=std::move(path);m_tMoveStorage.m_vPath.clear();
	if (m_iWeaponID==TF_WEAPON_PIPEBOMBLAUNCHER&&Vars::Aimbot::Projectile::SplashOptimization.Value)
	{
		m_tMoveStorage.m_pExternalData=m_aStickyMoveScratch.data();m_tMoveStorage.m_nExternalBytes=m_aStickyMoveScratch.size();
	}
}

void CAimbotProjectile::ReleaseStickyPath()
{
	if (!m_bStickyPathBorrowed) return;
	m_tProjInfo.m_vPath.swap(m_stickyValidationPath);m_tProjInfo.m_vPath.clear();m_bStickyPathBorrowed=false;
}

Vec3 CAimbotProjectile::StickyTargetAt(int tick) const
{
	const auto& path=m_tMoveStorage.m_vPath;
	if (!path.empty())
	{
		const int index=std::clamp(m_iStickyPredictionBase+TIME_TO_TICKS(m_tInfo.m_flLatency)+tick,0,int(path.size())-1);
		return path[index];
	}
	return m_tInfo.m_pTarget->m_pEntity->m_vecOrigin();
}

void CAimbotProjectile::DiscoverSticky(StickyState& state)
{
	state.count=0;state.bestAirCursor=0;
	auto add=[&](Vec3 point,uint8_t type,uint8_t arcs,bool relative,Vec3 normal=Vec3{},int priority=0) {
		if (!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||!arcs) return;
		for (int i=0;i<state.count;++i)
		{
			auto& old=state.candidates[i];
			if (old.type==type&&old.relative==relative&&old.point.DistToSqr(point)<1.f)
			{old.arcs|=arcs;old.priority=(std::min)(old.priority,priority);return;}
		}
		if (state.count<int(state.candidates.size())) state.candidates[state.count++]={point,normal,type,arcs,relative,priority};
	};
	for (int i=0;i<m_iStickyDirectSeedCount;++i)
	{
		const auto& seed=m_aStickyDirectSeeds[i];
		add(seed.point,seed.type,seed.arcs,seed.relative,seed.normal,seed.priority);
	}
	uint8_t arcs=0;
	if (m_tInfo.m_flRadius>0&&Vars::Aimbot::Projectile::SplashPrediction.Value!=Vars::Aimbot::Projectile::SplashPredictionEnum::Off)
		arcs=PointFlagsEnum::Regular|(ShouldLob(m_tInfo)?PointFlagsEnum::Lob:0);
	if (!arcs)
	{
		std::sort(state.candidates.begin(),state.candidates.begin()+state.count,[](const StickyCandidate& a,const StickyCandidate& b){return a.priority<b.priority;});
		return;
	}
	const Vec3 size=m_tInfo.m_pTarget->m_pEntity->m_vecMaxs()-m_tInfo.m_pTarget->m_pEntity->m_vecMins();
	if (Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AirSplash)
		add(m_tInfo.m_pTarget->m_pEntity->m_vecMins()+size*.5f,PointTypeEnum::Air,arcs,true);
	const int estimate=std::clamp((std::max)(m_tInfo.m_iArmTime,TIME_TO_TICKS(m_tInfo.m_vLocalEye.DistTo(StickyTargetAt(0))/m_tInfo.m_flVelocity)),1,state.horizon);
	const Vec3 origin=StickyTargetAt(estimate),mins=origin+m_tInfo.m_pTarget->m_pEntity->m_vecMins(),maxs=origin+m_tInfo.m_pTarget->m_pEntity->m_vecMaxs();
	const Vec3 centre=(mins+maxs)*.5f,reach=Vec3::Get(m_tInfo.m_flRadius+16.f);
	const int faces=Vars::Aimbot::Projectile::SplashMode.Value==Vars::Aimbot::Projectile::SplashModeEnum::Face&&ExtraSearchAvailable()
		?F::World.GatherSeamFaces(mins-reach,maxs+reach,m_aSeamFaces.data(),int(m_aSeamFaces.size())):0;
	for (int i=0;i<faces && state.count<64;++i)
	{
		const auto& face=m_aSeamFaces[i];const auto point=SeamGeometry::Nearest(face,AimGeometry::Point(centre));
		if (AimPolicy::BoxDistance(point,AimGeometry::Point(mins),AimGeometry::Point(maxs))>m_tInfo.m_flRadius) continue;
		add({point.x,point.y,point.z},PointTypeEnum::Geometry,arcs,false,{face.normal.x,face.normal.y,face.normal.z});
	}
	for (int i=0;i<faces && state.count<88 && m_iSeamPairs<8192;++i)
		for (int j=i+1;j<faces && state.count<88 && m_iSeamPairs<8192;++j)
		{
			++m_iSeamPairs;SeamGeometry::Edge edge;
			if (!SeamGeometry::Intersect(m_aSeamFaces[i],m_aSeamFaces[j],edge)) continue;
			const auto point=SeamGeometry::Nearest(edge,AimGeometry::Point(centre));
			if (AimPolicy::BoxDistance(point,AimGeometry::Point(mins),AimGeometry::Point(maxs))>m_tInfo.m_flRadius) continue;
			const Vec3 normal={edge.normal.x,edge.normal.y,edge.normal.z};
			add(Vec3(point.x,point.y,point.z)+normal*.03125f,PointTypeEnum::Geometry,arcs,false,normal);
		}
	CTraceFilterWorldAndPropsOnly filter(m_tInfo.m_pLocal);
	for (int i=0;i<24 && state.count<int(state.candidates.size()) && m_stickyWork.Check();++i)
	{
		const float z=1.f-2.f*(float(i)+.5f)/24.f,angle=float(i)*2.39996323f;
		const float radial=std::sqrt((std::max)(1.f-z*z,0.f));
		const Vec3 direction={radial*std::cos(angle),radial*std::sin(angle),z};
		CGameTrace trace={};SDK::Trace(centre,centre+direction*(m_tInfo.m_flRadius+size.Length()*.5f),MASK_SOLID,&filter,&trace);
		if (trace.DidHit()&&!trace.startsolid&&!trace.allsolid&&!(trace.surface.flags&(SURF_SKY|SURF_SKY2D)))
			add(trace.endpos,PointTypeEnum::Geometry,arcs,false,trace.plane.normal);
	}
	std::sort(state.candidates.begin(),state.candidates.begin()+state.count,[&](const StickyCandidate& a,const StickyCandidate& b) {
		if ((a.type==PointTypeEnum::Direct)!=(b.type==PointTypeEnum::Direct)
			&&Vars::Aimbot::Projectile::SplashPrediction.Value>=Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer)
			return a.type!=PointTypeEnum::Direct;
		if (a.type!=b.type) return a.type<b.type;
		if (a.type==PointTypeEnum::Direct&&a.priority!=b.priority) return a.priority<b.priority;
		return AimPolicy::BoxDistance(AimGeometry::Point(a.relative?origin+a.point:a.point),AimGeometry::Point(mins),AimGeometry::Point(maxs))
			<AimPolicy::BoxDistance(AimGeometry::Point(b.relative?origin+b.point:b.point),AimGeometry::Point(mins),AimGeometry::Point(maxs));
	});
}

bool CAimbotProjectile::FlySticky(StickyState& state,const StickyCandidate& candidate,const Vec3& angles,StickyFlight& flight)
{
	flight.count=flight.closest=flight.airSamples=0;flight.command=-1;flight.hit=flight.sky=false;flight.distance=std::numeric_limits<float>::max();
	flight.error=flight.contact=flight.normal={};flight.impactTime=0;
	m_tProjInfo={};
	if (!F::ProjSim.GetInfo(m_tInfo.m_pLocal,m_tInfo.m_pWeapon,angles,m_tProjInfo,
		ProjSimEnum::Redirect|ProjSimEnum::InitCheck|ProjSimEnum::NoRandomAngles|ProjSimEnum::PredictCmdNum)
		|| !F::ProjSim.Initialize(m_tProjInfo)) return false;
	flight.path[0]=F::ProjSim.GetOrigin();
	CTraceFilterCollideable filter(m_tInfo.m_pTarget->m_pEntity);
	filter.m_iPlayer=PLAYER_NONE;filter.m_iObject=OBJECT_ALL;filter.m_bMisc=false;
	int mask=MASK_SOLID;F::ProjSim.SetupTrace(filter,mask,m_tInfo.m_pWeapon);
	std::array<float,8> airScores{};
	static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime"),ramp=H::ConVars.FindVar("tf_sticky_radius_ramp_time"),airRadius=H::ConVars.FindVar("tf_sticky_airdet_radius");
	for (int tick=1;tick<=state.horizon;++tick)
	{
		const Vec3 before=flight.path[tick-1];F::ProjSim.RunTick(m_tProjInfo,false);
		const Vec3 after=F::ProjSim.GetOrigin();
		if (!std::isfinite(after.x)||!std::isfinite(after.y)||!std::isfinite(after.z)) return false;
		CGameTrace trace={};SDK::TraceHull(before,after,-m_tProjInfo.m_vHull,m_tProjInfo.m_vHull,mask,&filter,&trace);
		if (trace.startsolid||trace.allsolid) return false;
		const Vec3 end=trace.endpos;flight.path[tick]=end;flight.count=tick;
		const Vec3 desiredBefore=candidate.relative?StickyTargetAt(tick-1)+candidate.point:candidate.point;
		const Vec3 desired=candidate.relative?StickyTargetAt(tick)+candidate.point:candidate.point;
		const Vec3 desiredEnd=desiredBefore+(desired-desiredBefore)*trace.fraction;
		const Vec3 errorBefore=before-desiredBefore,delta=(end-before)-(desiredEnd-desiredBefore);
		const float fraction=delta.LengthSqr()>1e-8f?std::clamp(-errorBefore.Dot(delta)/delta.LengthSqr(),0.f,1.f):0.f;
		const Vec3 error=errorBefore+delta*fraction;const float distance=error.Length();
		if (distance<flight.distance)
		{
			flight.distance=distance;flight.error=error;flight.closest=tick;
		}
		if (!trace.DidHit()&&tick>=m_tInfo.m_iArmTime)
		{
			const Vec3 origin=StickyTargetAt(tick);
			float radius=m_tInfo.m_flRadius;
			if (live&&ramp&&airRadius) radius*=Math::RemapVal(TICKS_TO_TIME(tick),live->GetFloat(),live->GetFloat()+ramp->GetFloat(),airRadius->GetFloat(),1.f);
			const float distance=AimPolicy::BoxDistance(AimGeometry::Point(end),AimGeometry::Point(origin+m_tInfo.m_pTarget->m_pEntity->m_vecMins()),
				AimGeometry::Point(origin+m_tInfo.m_pTarget->m_pEntity->m_vecMaxs()));
			const float score=AimPolicy::BlastScore(distance,radius,TICKS_TO_TIME(tick));
			int slot=flight.airSamples;
			if (score>0)
			{
				for (int i=0;i<flight.airSamples;++i) if (score>airScores[i]) {slot=i;break;}
				if (slot<int(airScores.size()))
				{
					flight.airSamples=(std::min)(flight.airSamples+1,int(airScores.size()));
					for (int i=flight.airSamples-1;i>slot;--i) {airScores[i]=airScores[i-1];flight.airTicks[i]=flight.airTicks[i-1];}
					airScores[slot]=score;flight.airTicks[slot]=tick;
				}
			}
		}
		if (trace.DidHit())
		{
			flight.hit=true;flight.sky=bool(trace.surface.flags&(SURF_SKY|SURF_SKY2D));
			flight.contact=end;flight.normal=trace.plane.normal;
			flight.impactTime=TICKS_TO_TIME(tick-1)+trace.fraction*TICK_INTERVAL;break;
		}
	}
	flight.command=G::OriginalCmd.command_number;
	return flight.count>0&&std::isfinite(flight.distance);
}

bool CAimbotProjectile::SearchSticky()
{
	BeginSearch();m_flBestSplashScore=0.f;
	auto* target=m_tInfo.m_pTarget->m_pEntity;auto* local=m_tInfo.m_pLocal;auto* weapon=m_tInfo.m_pWeapon;
	auto handle=[](CBaseEntity* entity){return entity->As<IHandleEntity>()->GetRefEHandle().ToInt();};
	StickySearch::Key key;key.target=handle(target);key.weapon=handle(weapon);key.owner=handle(local);
	key.spawn=target->IsPlayer()?target->As<CTFPlayer>()->m_iSpawnCounter():0;key.ownerSpawn=local->m_iSpawnCounter();
	key.model=target->m_nModelIndex();key.command=G::OriginalCmd.command_number;key.map=F::World.GeometryRevision();
	key.eye=AimGeometry::Point(m_tInfo.m_vLocalEye);
	key.origin=AimGeometry::Point(m_tMoveStorage.m_vPath.empty()?target->m_vecOrigin():m_tMoveStorage.m_vPath.front());
	key.mins=AimGeometry::Point(target->m_vecMins());key.maxs=AimGeometry::Point(target->m_vecMaxs());
	key.speed=m_tInfo.m_flVelocity;key.gravity=m_tInfo.m_flGravity;key.radius=m_tInfo.m_flRadius;
	key.arm=TICKS_TO_TIME(m_tInfo.m_iArmTime);key.latency=m_tInfo.m_flLatency;key.charge=weapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
	std::uint32_t settings=2166136261u;
	auto mix=[&](std::uint32_t value){settings=(settings^value)*16777619u;};
	auto real=[&](float value){mix(std::bit_cast<std::uint32_t>(value));};
	mix(Vars::Aimbot::Projectile::Modifiers.Value);mix(Vars::Aimbot::Projectile::SplashPrediction.Value);
	mix(Vars::Aimbot::Projectile::Hitboxes.Value);real(Vars::Aimbot::Projectile::VerticalShift.Value);
	for (int i=0;i<m_iStickyDirectSeedCount;++i)
	{
		const auto& seed=m_aStickyDirectSeeds[i];mix(seed.priority);mix(seed.arcs);
		real(seed.point.x);real(seed.point.y);real(seed.point.z);
	}
	mix(Vars::Aimbot::General::NoSpread.Value);mix(m_iMethod);mix(F::AntiCheatCompatibility.Active());
	mix(Vars::Aimbot::Projectile::SplashMode.Value);
	real(Vars::Aimbot::Projectile::MaxSimulationTime.Value);real(Vars::Aimbot::Projectile::HitChance.Value);
	real(Vars::Aimbot::General::AimFOV.Value);real(m_tInfo.m_flNormalOffset);
	mix(weapon->m_iItemDefinitionIndex());mix(m_tProjInfo.m_uType);
	static auto live=H::ConVars.FindVar("tf_grenadelauncher_livetime"),ramp=H::ConVars.FindVar("tf_sticky_radius_ramp_time"),airRadius=H::ConVars.FindVar("tf_sticky_airdet_radius");
	if (live) real(live->GetFloat());if (ramp) real(ramp->GetFloat());if (airRadius) real(airRadius->GetFloat());
	real(m_tInfo.m_vOffset.x);real(m_tInfo.m_vOffset.y);real(m_tInfo.m_vOffset.z);
	key.settings=settings;
	StickyState* chosen=nullptr;
	for (auto& entry:m_aStickySearch)
	{
		if (entry.cursor.valid&&entry.cursor.seed.target==key.target&&entry.cursor.seed.weapon==key.weapon) {chosen=&entry;break;}
		if (!chosen || (!entry.cursor.valid&&chosen->cursor.valid)
			|| (chosen->cursor.valid&&entry.cursor.last.command<chosen->cursor.last.command)) chosen=&entry;
	}
	auto& state=*chosen;
	const bool fresh=state.cursor.Prepare(key);
	if (!state.cursor.valid) return false;
	state.horizon=std::clamp(TIME_TO_TICKS((std::min)(Vars::Aimbot::Projectile::MaxSimulationTime.Value,m_tProjInfo.m_flLifetime)),1,StickySearch::MaxTicks);
	if (fresh||state.cursor.stage==StickySearch::Stage::Exhausted)
	{
		state.cursor.candidate=state.cursor.arc=state.cursor.roll=state.cursor.refinement=state.cursor.airTick=0;
		state.cursor.stage=StickySearch::Stage::Seed;DiscoverSticky(state);
	}
	const int rolls=m_iMethod==Vars::Aimbot::General::AimTypeEnum::Silent&&!F::AntiCheatCompatibility.Active()?4:1;
	constexpr float rollAngles[]={0.f,180.f,90.f,-90.f};
	int flights=0,verifications=0,airChecks=0;
	auto fly=[&](const StickyCandidate& candidate,const Vec3& angles,StickyFlight& result) {
		if (!ExtraSearchAvailable()||flights>=4||!m_stickyWork.Flight(state.horizon)) return -1;
		++flights;return FlySticky(state,candidate,angles,result)?1:0;
	};
	auto next=[&](){state.cursor.Next(state.count,rolls);state.bestAirCursor=0;};
	auto verify=[&](int tick,Vec3 point,uint8_t type,uint8_t mode) {
		if (!F::AimbotGlobal.ShouldAimAtAngle(state.angles)
			||Math::CalcFov(I::EngineClient->GetViewAngles(),state.angles)>Vars::Aimbot::General::AimFOV.Value) return 0;
		if (!ExtraSearchAvailable()||verifications>=2||m_iAngleTests>=510||!m_stickyWork.Check()) return -1;
		if (state.flight.command!=key.command)
		{
			const auto& candidate=state.candidates[state.cursor.candidate];
			const int refreshed=fly(candidate,state.angles,state.flight);
			if (refreshed<0) return -1;if (!refreshed) return 0;
		}
		if (type==PointTypeEnum::Geometry)
		{
			if (!state.flight.hit||state.flight.sky) return 0;
			tick=state.flight.count;point=state.flight.contact;
		}
		else if (type==PointTypeEnum::Air)
		{
			if (tick<=0||tick>state.flight.count-(state.flight.hit?1:0)) return 0;
			point=state.flight.path[tick];
		}
		else {tick=state.flight.closest;point=StickyTargetAt(tick)+state.candidates[state.cursor.candidate].point;}
		// Smooth/assist can perform a second native flight retest. Reserve its
		// full bounded horizon too, rather than hiding that work in the budget.
		if (!m_stickyWork.Flight(StickySearch::MaxTicks,2)) return -1;
		++verifications;
		const int arrival=type==PointTypeEnum::Geometry?(std::max)(tick,m_tInfo.m_iArmTime):tick;
		if (arrival<=0||arrival>state.horizon) return 0;
		struct ValidationScope
		{
			CAimbotProjectile& parent;bool saved;
			~ValidationScope(){parent.ReleaseStickyPath();parent.m_bStickyValidation=saved;}
		} validation{*this,m_bStickyValidation};
		m_bStickyValidation=true;
		return HandlePoint(StickyTargetAt(arrival),arrival+m_tInfo.m_iLaunchDelay,state.angles.x,state.angles.y,
			TICKS_TO_TIME(arrival),point,type,mode,state.angles.z)?1:0;
	};
	auto search=[&]() -> bool
	{
	for (int work=0;work<256&&state.cursor.stage!=StickySearch::Stage::Exhausted;++work)
	{
		if (state.cursor.candidate>=state.count) {state.cursor.stage=StickySearch::Stage::Exhausted;break;}
		const auto& candidate=state.candidates[state.cursor.candidate];
		const uint8_t mode=state.cursor.arc?PointFlagsEnum::Lob:PointFlagsEnum::Regular;
		if (!(candidate.arcs&mode)) {next();continue;}
		using Stage=StickySearch::Stage;
		switch (state.cursor.stage)
		{
		case Stage::Seed:
		{
			const int tick=std::clamp((std::max)(m_tInfo.m_iArmTime,TIME_TO_TICKS(m_tInfo.m_vLocalEye.DistTo(StickyTargetAt(0))/m_tInfo.m_flVelocity)),1,state.horizon);
			const Vec3 point=candidate.relative?StickyTargetAt(tick)+candidate.point:candidate.point;
			Solution_t solution;CalculateAngle(m_tInfo.m_vLocalEye,point,state.horizon,solution,
				(state.cursor.roll?CalculateFlagsEnum::TwoPass|CalculateFlagsEnum::AccountDrag:CalculateFlagsEnum::Accuracy)
				|(state.cursor.arc?CalculateFlagsEnum::LobAngle:0),std::numeric_limits<int>::max());
			if (solution.m_iCalculated!=CalculateResultEnum::Good) {next();continue;}
			state.angles={solution.m_flPitch,solution.m_flYaw,rollAngles[state.cursor.roll]};state.bestAirCursor=0;
			state.cursor.stage=Stage::Flight;break;
		}
		case Stage::Flight:
		{
			const int result=fly(candidate,state.angles,state.flight);if (result<0) return false;
			if (!result) {next();break;}
			state.cursor.stage=state.flight.distance<=2.f||state.cursor.refinement>=StickySearch::MaxRefinements?Stage::Verify:Stage::Pitch;break;
		}
		case Stage::Pitch:
		case Stage::Yaw:
		{
			const bool pitch=state.cursor.stage==Stage::Pitch;
			Vec3 probe=state.angles;if (pitch) probe.x+=.25f;else probe.y+=.25f;
			const int result=fly(candidate,probe,m_stickyTrial);if (result<0) return false;
			if (!result) {state.cursor.stage=Stage::Verify;break;}
			if (pitch) state.pitchError=m_stickyTrial.error;else state.yawError=m_stickyTrial.error;
			state.cursor.stage=pitch?Stage::Yaw:Stage::Apply;break;
		}
		case Stage::Apply:
		{
			state.proposed=state.angles;
			if (!StickySearch::Refine(AimGeometry::Point(state.flight.error),AimGeometry::Point((state.pitchError-state.flight.error)/.25f),
				AimGeometry::Point((state.yawError-state.flight.error)/.25f),state.proposed.x,state.proposed.y))
			{state.cursor.stage=Stage::Verify;break;}
			const int result=fly(candidate,state.proposed,m_stickyTrial);if (result<0) return false;
			++state.cursor.refinement;
			if (result&&m_stickyTrial.distance+.01f<state.flight.distance)
			{
				state.angles=state.proposed;state.flight=m_stickyTrial;state.bestAirCursor=0;state.cursor.airTick=0;
				state.cursor.stage=state.flight.distance<=2.f||state.cursor.refinement>=StickySearch::MaxRefinements?Stage::Verify:Stage::Pitch;
			}
			else state.cursor.stage=Stage::Verify;
			break;
		}
		case Stage::Verify:
		{
			int result=0;
			if (candidate.type==PointTypeEnum::Direct)
				result=verify(state.flight.closest,StickyTargetAt(state.flight.closest)+candidate.point,PointTypeEnum::Direct,mode);
			else if (state.flight.hit&&!state.flight.sky)
				result=verify(state.flight.count,state.flight.contact,PointTypeEnum::Geometry,mode);
			if (result<0) return false;if (result>0) return true;
			state.cursor.stage=Stage::Air;
			state.cursor.airTick=(std::max)(state.cursor.airTick,(std::max)(m_tInfo.m_iArmTime,1));break;
		}
		case Stage::Air:
		{
			if (!(Vars::Aimbot::Projectile::Modifiers.Value&Vars::Aimbot::Projectile::ModifiersEnum::AirSplash)||m_tInfo.m_flRadius<=0)
			{next();break;}
			const int end=state.flight.count-(state.flight.hit?1:0);
			const bool preferred=state.bestAirCursor<state.flight.airSamples;
			if (!preferred&&state.cursor.airTick>end) {next();break;}
			if (airChecks>=32||!m_stickyWork.Check()) return false;
			++airChecks;
			const int tick=preferred?state.flight.airTicks[state.bestAirCursor]:state.cursor.airTick;
			const Vec3 point=state.flight.path[tick],origin=StickyTargetAt(tick);
			const float distance=AimPolicy::BoxDistance(AimGeometry::Point(point),AimGeometry::Point(origin+target->m_vecMins()),AimGeometry::Point(origin+target->m_vecMaxs()));
			if (distance<=m_tInfo.m_flRadius)
			{
				const int result=verify(tick,point,PointTypeEnum::Air,mode);
				if (result<0) return false;if (result>0) return true;
			}
			if (preferred) ++state.bestAirCursor;else ++state.cursor.airTick;
			break;
		}
		default: return false;
		}
	}
	return false;
	};
	if (search()) return true;
	// Prefer splash still permits a current-command direct fallback while the
	// expensive surface search is deferred. Preserve the user's offset priority.
	if (Vars::Aimbot::Projectile::SplashPrediction.Value!=Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer
		||m_iStickyDirectSeedCount==0||m_iAngleTests>=510) return false;
	const StickyCandidate* direct=&m_aStickyDirectSeeds[0];
	for (int i=1;i<m_iStickyDirectSeedCount;++i) if (m_aStickyDirectSeeds[i].priority<direct->priority) direct=&m_aStickyDirectSeeds[i];
	if (!(direct->arcs&PointFlagsEnum::Regular)) return false;
	int tick=std::clamp(TIME_TO_TICKS(m_tInfo.m_vLocalEye.DistTo(StickyTargetAt(0))/m_tInfo.m_flVelocity),1,state.horizon);
	Solution_t solution;
	for (int pass=0;pass<3;++pass)
	{
		solution={};CalculateAngle(m_tInfo.m_vLocalEye,StickyTargetAt(tick)+direct->point,state.horizon,solution,
			CalculateFlagsEnum::Accuracy,std::numeric_limits<int>::max());
		if (solution.m_iCalculated!=CalculateResultEnum::Good) return false;
		tick=std::clamp(int(std::ceil(solution.m_flTime/TICK_INTERVAL)),1,state.horizon);
	}
	const Vec3 angles={solution.m_flPitch,solution.m_flYaw,0};
	if (!F::AimbotGlobal.ShouldAimAtAngle(angles)||Math::CalcFov(I::EngineClient->GetViewAngles(),angles)>Vars::Aimbot::General::AimFOV.Value
		||!m_stickyWork.Check()||!m_stickyWork.Flight(StickySearch::MaxTicks,2)) return false;
	struct QuickValidationScope
	{
		CAimbotProjectile& parent;bool saved;
		~QuickValidationScope(){parent.ReleaseStickyPath();parent.m_bStickyValidation=saved;}
	} validation{*this,m_bStickyValidation};
	m_bStickyValidation=true;
	return HandlePoint(StickyTargetAt(tick),tick+m_tInfo.m_iLaunchDelay,angles.x,angles.y,
		TICKS_TO_TIME(tick),StickyTargetAt(tick)+direct->point,PointTypeEnum::Direct,PointFlagsEnum::Regular);
}

bool CAimbotProjectile::HandleSplash(SplashHistory_t& history)
{
	BeginSearch();
	if(!Vars::Aimbot::Projectile::SplashOptimization.Value||m_tInfo.m_pProjectile) return HandleSplashLegacy(history);
	PROJ_ZONE(HandleSplash);
	if(history.empty()) return false;
	auto it=history.begin();const uint8_t mode=it->first;
	auto& frames=it->second;
	std::sort(frames.begin(),frames.end(),[](const Splash_t& a,const Splash_t& b){return a.m_flTimeTo<b.m_flTimeTo;});
	uint8_t flags=CalculateFlagsEnum::Accuracy;
	if(mode==PointFlagsEnum::Lob) flags|=CalculateFlagsEnum::LobAngle;
	m_flBestSplashScore=0;
	SetupSplashPoints(frames.front().m_vOrigin,m_vSplashPoints,flags);
	const int command=G::CurrentUserCmd->command_number;
	auto& cached=m_aSplashSeeds[unsigned(m_tInfo.m_pTarget->m_pEntity->entindex())%m_aSplashSeeds.size()];
	const bool reuse=cached.target.Get()==m_tInfo.m_pTarget->m_pEntity&&cached.weapon==m_iWeaponID
		&&command>cached.command&&command-cached.command<=2&&cached.origin.DistToSqr(frames.front().m_vOrigin)<128.f*128.f;
	const bool fast=Vars::Aimbot::Projectile::SplashMode.Value==Vars::Aimbot::Projectile::SplashModeEnum::Trace;
	bool found=false,first=true;
	for(auto& frame:frames)
	{
		auto evaluate=[&](const Vec3& point,uint8_t type) {
			Solution_t solution;
			CalculateAngle(m_tInfo.m_vLocalEye,point,frame.m_iSimtime-m_tInfo.m_iLaunchDelay,solution,flags,
				m_tInfo.m_bIgnoreTiming&&mode==PointFlagsEnum::Lob?std::numeric_limits<int>::max():-1);
			return solution.m_iCalculated==CalculateResultEnum::Good&&HandlePoint(frame.m_vOrigin,frame.m_iSimtime,
				solution.m_flPitch,solution.m_flYaw,solution.m_flTime,point,type,mode);
		};
		bool improved=SearchSplashSeams(frame,mode,flags);
		if(reuse) improved=evaluate(cached.point,PointTypeEnum::Geometry)||improved;
		if(fast&&improved) {found=true;break;}
		auto candidates=m_vSplashPoints;
		auto points=GetSplashPoints(frame.m_vOrigin,candidates,frame.m_iSimtime,flags,first);first=false;
		auto estimate=[&](const Point_t& point) {
			auto* entity=m_tInfo.m_pTarget->m_pEntity;
			const float distance=AimPolicy::BoxDistance(AimGeometry::Point(point.m_vPoint),
				AimGeometry::Point(frame.m_vOrigin+entity->m_vecMins()),AimGeometry::Point(frame.m_vOrigin+entity->m_vecMaxs()));
			return AimPolicy::BlastScore(distance,m_tInfo.m_flRadius,point.m_tSolution.m_flTime);
		};
		std::sort(points.begin(),points.end(),[&](const Point_t& a,const Point_t& b){return estimate(a)>estimate(b);});
		for(const auto& point:points)
		{
			if(m_iAngleTests>=512) break;
			if(HandlePoint(frame.m_vOrigin,frame.m_iSimtime,point.m_tSolution.m_flPitch,point.m_tSolution.m_flYaw,
				point.m_tSolution.m_flTime,point.m_vPoint,point.m_iType,mode))
			{
				improved=true;
				if(fast) break;
			}
		}
		if(improved)
		{
			found=true;
			if(fast)
			{
				cached.target.Set(m_tInfo.m_pTarget->m_pEntity);cached.point=m_vBestImpact;cached.origin=frame.m_vOrigin;
				cached.command=command;cached.weapon=m_iWeaponID;
				break;
			}
			// Refine a verified surface in its tangent plane. Each new point is
			// projected back onto geometry and runs the complete flight test again.
			if(m_vBestNormal.LengthSqr()>.9f&&ExtraSearchAvailable())
			{
				for(float step:{8.f,4.f})
				{
					const Vec3 normal=m_vBestNormal,centre=m_vBestImpact;
					Vec3 right=normal.Cross(std::abs(normal.z)>.99f?Vec3(0,1,0):Vec3(0,0,1));right.Normalize();
					const Vec3 up=normal.Cross(right);
					for(int side=0;side<8&&m_iRefineTraces<64&&m_iAngleTests<512&&ExtraSearchAvailable();++side)
					{
						const float angle=side*Math::PI_V<float>*.25f;
						const Vec3 candidate=centre+(right*std::cos(angle)+up*std::sin(angle))*step;
						CTraceFilterWorldAndPropsOnly filter(m_tInfo.m_pLocal);CGameTrace trace={};++m_iRefineTraces;
						SDK::Trace(candidate+normal*16,candidate-normal*24,MASK_SOLID,&filter,&trace);
						if(trace.DidHit()&&!trace.startsolid&&!trace.allsolid&&!(trace.surface.flags&(SURF_SKY|SURF_SKY2D)))
							evaluate(trace.endpos,PointTypeEnum::Geometry);
					}
				}
				cached.target.Set(m_tInfo.m_pTarget->m_pEntity);cached.point=m_vBestImpact;cached.origin=frame.m_vOrigin;
				cached.command=command;cached.weapon=m_iWeaponID;
			}
		}
		if(m_iAngleTests>=512||(m_tInfo.m_bIgnoreTiming&&mode==PointFlagsEnum::Lob)) break;
	}
	history.erase(it);return found;
}

int CAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, bool bUpdate)
{
	BeginSearch();
	ProjTargetScope_t tProf(tTarget.m_pEntity->entindex(), tTarget.m_iTargetType, tTarget.m_nAimedHitbox, tTarget.m_flDistTo);
	PROJ_ZONE(CanHit);
	PROJ_COUNT(CanHits);

	//if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
	//	return false;

	m_pHeadBones=nullptr;m_pHeadSet=nullptr;
	// Head priority is assigned later by BuildDirects. Capture the pose before
	// MoveSim mutates the target, independently of that not-yet-selected hitbox.
	if(m_iWeaponID==TF_WEAPON_COMPOUND_BOW&&Vars::Aimbot::Projectile::HuntsmanSafePoint.Value&&tTarget.m_pEntity->IsPlayer())
	{
		if(auto* record=F::Backtrack.GetLatestRecord(tTarget.m_pEntity))
		{
			memcpy(m_aHeadSnapshot.data(),record->m_aBones,sizeof(record->m_aBones));m_pHeadBones=m_aHeadSnapshot.data();m_vHeadOrigin=record->m_vOrigin;
			m_pHeadSet=tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxSet();
		}
		else if(tTarget.m_pEntity->As<CTFPlayer>()->SetupBones(m_aHeadSnapshot.data(),MAXSTUDIOBONES,BONE_USED_BY_HITBOX,tTarget.m_pEntity->m_flSimulationTime()))
		{
			m_pHeadBones=m_aHeadSnapshot.data();m_vHeadOrigin=tTarget.m_pEntity->GetAbsOrigin();
			m_pHeadSet=tTarget.m_pEntity->As<CBaseAnimating>()->GetHitboxSet();
		}
	}

	const bool accountPing=Vars::Aimbot::Projectile::AccountPing.Value;
	const bool playerTarget=tTarget.m_pEntity->IsPlayer();
	const Vec3 targetOrigin=ProjectileTargetOrigin(tTarget.m_pEntity,accountPing);
	ResetMoveStorage();
	if(s_boltDiagnostic.active) s_boltDiagnostic.stage="movement-init";
	if (!F::MoveSim.Initialize(tTarget.m_pEntity,m_tMoveStorage,true,true,!playerTarget||accountPing,
		accountPing&&playerTarget?&targetOrigin:nullptr) && tTarget.m_iTargetType == TargetEnum::Player)
	{
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	if(s_boltDiagnostic.active) s_boltDiagnostic.stage="projectile-info";
	m_flTargetSpeed=tTarget.m_pEntity->GetAbsVelocity().Length2D();
	m_flTargetTurnRate=m_tMoveStorage.m_flAverageYaw*Math::PI_V<float>/180.f/TICK_INTERVAL;
	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, m_tProjInfo, ProjSimEnum::NoRandomAngles | ProjSimEnum::PredictCmdNum)
		|| !F::ProjSim.Initialize(m_tProjInfo, false))
	{
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	m_tInfo = { pLocal, pWeapon, &tTarget };
	m_vShootPos = pLocal->GetShootPos();
	// The sentrygun's rocket firing position is fixed based on its current angles, so its not quite correct when testing with different angles
	m_tInfo.m_vLocalEye = m_iWeaponID == TF_WEAPON_LASER_POINTER ? m_tProjInfo.m_vPos : m_vShootPos;
	m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
	m_tInfo.m_flLatency = Vars::Aimbot::Projectile::AccountPing.Value ? F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke()) : 0.f;
	tTarget.m_vPos = targetOrigin;

	Vec3 vVelocity = F::ProjSim.GetVelocity();
	m_tInfo.m_flVelocity = vVelocity.Length();
	m_tInfo.m_vAngFix = Math::VectorAngles(vVelocity);

	m_tInfo.m_vHull = m_tProjInfo.m_vHull.Min(3);
	m_tInfo.m_vOffset = m_tProjInfo.m_vPos - m_tInfo.m_vLocalEye; m_tInfo.m_vOffset.y *= -1;
	m_tInfo.m_flOffsetTime = m_tInfo.m_vOffset.Length() / m_tInfo.m_flVelocity;
	if (auto pSpecial = F::ProjSim.m_pSpecial)
	{
		m_tInfo.m_pSpecial = pSpecial;
		m_tInfo.m_flVelocity = pSpecial->m_flSpeed;
		m_tInfo.m_vAngFix = {};
		m_tInfo.m_vOffset = {};
		m_tInfo.m_flOffsetTime = 0.f;
		m_tInfo.m_vVelAdd = m_tProjInfo.m_vVelocityAdd;
		m_tInfo.m_flSpawnDist = pSpecial->m_flSpawnDist;
		m_tInfo.m_flSpawnHeight = pSpecial->m_flSpawnHeight;
		m_tInfo.m_flDrag = m_flSpecialDrag;
		m_tInfo.m_iLaunchDelay = m_iLaunchDelay;
	}

	m_tInfo.m_flGravity = m_tProjInfo.m_flGravity;
	m_tInfo.m_iSplashRestrict = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashRestrictDirect.Value : Vars::Aimbot::Projectile::SplashRestrictArc.Value;
	m_tInfo.m_flRadius = m_tInfo.m_pSpecial
		? (tTarget.m_iTargetType == TargetEnum::Player ? m_tInfo.m_pSpecial->m_flSplashRadius : m_tInfo.m_pSpecial->m_flBuildingRadius) * Vars::Aimbot::Projectile::SplashRadius.Value / 100
		: GetSplashRadius(pWeapon, pLocal, Vars::Aimbot::Projectile::SplashRadius.Value / 100);
	m_tInfo.m_flBoundsTime = tTarget.m_pEntity->GetSize().Length() / m_tInfo.m_flVelocity;
	m_tInfo.m_flRadiusTime = m_tInfo.m_flBoundsTime + m_tInfo.m_flRadius / m_tInfo.m_flVelocity;
	m_tInfo.m_iArmTime = TIME_TO_TICKS(ArmTime(pWeapon));
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_LASER_POINTER:
		m_tInfo.m_flNormalOffset = 1.f;
	}
	m_tInfo.m_bIgnoreTiming = Vars::Aimbot::Projectile::LobAnglesUnderpredict.Value && m_tInfo.m_flRadius;



	if (bUpdate) ResetSolution();
	m_bUpdate=bUpdate;
	if (m_iWeaponID==TF_WEAPON_PIPEBOMBLAUNCHER&&Vars::Aimbot::Projectile::SplashOptimization.Value)
	{
		m_iStickyDirectSeedCount=0;BuildDirects(nullptr);
		m_iStickyPredictionBase=(std::max)(int(m_tMoveStorage.m_vPath.size())-1,0);
		const int horizon=std::clamp(TIME_TO_TICKS((std::min)(m_tProjInfo.m_flLifetime,
			F::MoveSim.GetMaxSimulationTime(Vars::Aimbot::Projectile::MaxSimulationTime.Value))),1,StickySearch::MaxTicks);
		for (int tick=1-TIME_TO_TICKS(m_tInfo.m_flLatency);tick<=horizon+m_tInfo.m_iLaunchDelay;++tick)
			if (!m_tMoveStorage.m_bFailed) F::MoveSim.RunTick(m_tMoveStorage);
		if (F::ProjProfiler.Active())
		{
			tProf.SetSim(TICKS_TO_TIME(horizon),horizon,m_tMoveStorage.m_bFailed,false);
			tProf.SetLoop(horizon,m_iStickyDirectSeedCount,ShouldLob(m_tInfo)?2:1);
			tProf.SetHistory(0,0);
		}
		SearchSticky();
	}
	else
	{
	Directs_t mDirects = GetDirects();
	Splashes_t vSplashes = GetSplashes();

	DirectHistory_t mDirectHistory = {};
	SplashHistory_t mSplashHistory = {};

	ProjScope_t tTickLoop(ProjZoneEnum::TickLoop);
	const float flMaxSimTime = F::MoveSim.GetMaxSimulationTime(Vars::Aimbot::Projectile::MaxSimulationTime.Value);
	int iMaxTime = TIME_TO_TICKS(std::min(m_tProjInfo.m_flLifetime, flMaxSimTime)) + m_tInfo.m_iLaunchDelay;
	const int iDirectOffsets = int(mDirects.size()), iSplashModes = int(vSplashes.size());
	if(s_boltDiagnostic.active) {s_boltDiagnostic.stage="angle-search";s_boltDiagnostic.directs=iDirectOffsets;}
	m_iStickyPredictionBase=(std::max)(int(m_tMoveStorage.m_vPath.size())-1,0);
	int iLastTick = 0;
	for (int i = 1 - TIME_TO_TICKS(m_tInfo.m_flLatency); i <= iMaxTime; i++)
	{
		iLastTick = i;
		if (!m_tMoveStorage.m_bFailed)
		{
			F::MoveSim.RunTick(m_tMoveStorage);
			tTarget.m_vPos = m_tMoveStorage.m_vPredictedOrigin;
		}
		if (i < 0 || i < m_tInfo.m_iLaunchDelay)
			continue;

		for (auto offset=mDirects.begin();offset!=mDirects.end();)
		{
			auto current=offset++;
			auto& [iIndex,tOffset]=*current;
			if (m_tInfo.m_iArmTime && m_tInfo.m_iArmTime > i && !m_tMoveStorage.m_MoveData.m_vecVelocity.IsZero())
				break;

			Vec3& vOffset = tOffset.m_vOffset;
			uint8_t iType = tOffset.m_iFlags & -tOffset.m_iFlags;

			Vec3 vPoint = tTarget.m_vPos + vOffset;
			if (Vars::Aimbot::Projectile::HuntsmanPullPoint.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				vPoint = PullPoint(vPoint, m_tInfo.m_vLocalEye, m_tInfo, tTarget.m_vPos, tTarget.m_pEntity->m_vecMins() + m_tInfo.m_vHull, tTarget.m_pEntity->m_vecMaxs() - m_tInfo.m_vHull);
				if (Vars::Aimbot::Projectile::HuntsmanPullNoZ.Value)
					vPoint.z = tTarget.m_vPos.z + vOffset.z;
			}

			uint8_t iFlags = CalculateFlagsEnum::Accuracy;
			if (iType == PointFlagsEnum::Lob)
				iFlags |= CalculateFlagsEnum::LobAngle;
			int iTolerance = m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob ? std::numeric_limits<int>::max() : -1;

			Solution_t tSolution;
			switch (iType)
			{
			case PointFlagsEnum::Lob:
				if (ShouldLob(m_tMoveStorage, m_tInfo))
					goto end;
				tSolution.m_iCalculated = CalculateResultEnum::Bad; break;
			default: end:
				CalculateAngle(m_tInfo.m_vLocalEye, vPoint, i - m_tInfo.m_iLaunchDelay, tSolution, iFlags, iTolerance);
			}
			switch (tSolution.m_iCalculated)
			{
			case CalculateResultEnum::Good:
				if(s_boltDiagnostic.active) {++s_boltDiagnostic.solutions;s_boltDiagnostic.flight=tSolution.m_flTime;}
				if(Vars::Aimbot::Projectile::HuntsmanSafePoint.Value&&m_iWeaponID==TF_WEAPON_COMPOUND_BOW&&tTarget.m_nAimedHitbox==HITBOX_HEAD
					&&!FindHeadPoint(tTarget.m_vPos,i-m_tInfo.m_iLaunchDelay,iFlags,vPoint,tSolution)) break;
				mDirectHistory[iType].emplace_back(History_t(tTarget.m_vPos, i), tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vPoint, iIndex, tOffset.m_iSamples);
				// Preserve nearby arrival frames until native validation succeeds.
				// Each candidate must still pass the full collision/timing checks.
				if (!(m_tInfo.m_bIgnoreTiming&&iType==PointFlagsEnum::Lob)
					&&AimPolicy::DirectWindowOpen(tSolution.m_flTime,TICKS_TO_TIME(i-m_tInfo.m_iLaunchDelay),
						m_tInfo.m_flBoundsTime,TICK_INTERVAL,++tOffset.m_iSamples)) break;
				[[fallthrough]];
			case CalculateResultEnum::Bad:
				tOffset.m_iFlags &= ~iType;
				if (!(tOffset.m_iFlags /*& (PointFlagsEnum::Regular | PointFlagsEnum::Lob)*/))
					mDirects.erase(iIndex);
			}
		}

		for (auto it = vSplashes.begin(); it != vSplashes.end();)
		{
			uint8_t iFlags = CalculateFlagsEnum::AccountDrag;
			if (*it == PointFlagsEnum::Lob && !m_tInfo.m_bIgnoreTiming)
				iFlags |= CalculateFlagsEnum::LobAngle;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, tTarget.m_vPos, i - m_tInfo.m_iLaunchDelay, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculateResultEnum::Bad && mDirects.empty())
			{
				it = vSplashes.erase(it);
				continue;
			}

			const float flTimeTo = tSolution.m_flTime - TICKS_TO_TIME(i - m_tInfo.m_iLaunchDelay);
			if (flTimeTo > m_tInfo.m_flRadiusTime || m_tInfo.m_iArmTime && m_tInfo.m_iArmTime > i)
			{
				++it;
				continue;
			}
			if (flTimeTo < -m_tInfo.m_flRadiusTime && (!m_tInfo.m_iArmTime || m_tInfo.m_iArmTime < i))
			{
				it = vSplashes.erase(it);
				continue;
			}
			if (*it == PointFlagsEnum::Lob && !ShouldLob(m_tMoveStorage, m_tInfo))
			{
				++it;
				continue;
			}

			mSplashHistory[*it].emplace_back(History_t(tTarget.m_vPos, i), fabsf(flTimeTo));
			++it;
		}

		if (mDirects.empty() && vSplashes.empty())
			break;
	}
	tTickLoop.Close();

	if (F::ProjProfiler.Active())
	{
		int iDirectHistory = 0, iSplashHistory = 0;
		for (auto& [_, vHistory] : mDirectHistory)
			iDirectHistory += int(vHistory.size());
		for (auto& [_, vHistory] : mSplashHistory)
			iSplashHistory += int(vHistory.size());
		tProf.SetSim(flMaxSimTime, iMaxTime, m_tMoveStorage.m_bFailed, false);
		tProf.SetLoop(iLastTick, iDirectOffsets, iSplashModes);
		tProf.SetHistory(iDirectHistory, iSplashHistory);
		PROJ_COUNT_N(DirectPoints, iDirectHistory);
		PROJ_COUNT_N(SplashHistory, iSplashHistory);
	}

	if (!m_tInfo.m_flRadius || Vars::Aimbot::Projectile::SplashPrediction.Value < Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer)
		goto direct;
	else
		goto splash;
	while (!mDirectHistory.empty() || !mSplashHistory.empty())
	{
direct: if (HandleDirect(mDirectHistory)) break;
splash: if (HandleSplash(mSplashHistory)) break;
	}
	}
	if (!m_bBestPlayerPathSet) 
	{
		if (tTarget.m_pEntity && !tTarget.m_pEntity->GetAbsVelocity().IsZero(10.f))
			m_vBestPlayerPath = m_tMoveStorage.m_vPath;
		m_bBestPlayerPathSet = true;
	}
	F::MoveSim.Restore(m_tMoveStorage);
	if(s_boltDiagnostic.active) s_boltDiagnostic.result=m_iResult;
	if (!F::AimbotGlobal.ShouldAimAtAngle(m_vAngleTo))
	{
		if(s_boltDiagnostic.active) s_boltDiagnostic.stage="fov-restrict";
		return false;
	}
	if (!bUpdate)
		return m_iResult;

	tTarget.m_vPos = m_vTarget;
	tTarget.m_vAngleTo = m_vAngleTo;

	bool bMain = m_iResult == 1;
	bool bAny = m_iResult;
	if (bAny && (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a || Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a))
	{
		float flProjectileTime = 0.f, flTargetTime = 0.f;
		bool bBox = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot;
		bool bPoint = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint;
		bool bTimed = !Vars::Visuals::Prediction::PlayerDrawDuration.Value;
		if (bTimed)
		{
			flProjectileTime = TICKS_TO_TIME(m_vProjectilePath.size());
			flTargetTime = m_tMoveStorage.m_bFailed ? flProjectileTime : TICKS_TO_TIME(m_vPlayerPath.size());
		}
		if (bBox)
		{
			float flDuration = bTimed ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value;
			if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
				m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
			if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
				m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
		}
		if (bMain && bPoint)
		{
			float flDuration = bTimed ? flProjectileTime : Vars::Visuals::Hitbox::DrawDuration.Value;
			if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
				m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
			if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
				m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
		}
	}

	return m_iResult;
}



bool CAimbotProjectile::Aim(const Vec3& vCurAngle, const Vec3& vToAngle, Vec3& vOut)
{
	/*
	if (Vec3* pHoldAngle = F::Ticks.GetShootAngle())
	{
		vOut = *pHoldAngle;
		return true;
	}
	*/

	bool bReturn = false;
	switch (m_iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Legit:
		vOut = vCurAngle;
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
		vOut = vCurAngle.LerpAngle(vToAngle, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);

		float flMouseDelta = vMouseDelta.Length2DSqr(), flTargetDelta = vTargetDelta.Length2DSqr();
		vTargetDelta = vTargetDelta.Normalized() * sqrtf(std::min(flMouseDelta, flTargetDelta));
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	}

	if (m_iMethod != Vars::Aimbot::General::AimTypeEnum::Silent || F::AntiCheatCompatibility.Active())
		Math::ClampAngles(vOut);
	return bReturn;
}

// assume angle calculated outside with other overload
void CAimbotProjectile::Aim(CUserCmd* pCmd, Vec3& vAngles)
{
	bool bUnsure = F::Ticks.IsTimingUnsure();
	switch (m_iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAngles;
		I::EngineClient->SetViewAngles(vAngles);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure || m_iWeaponID == TF_WEAPON_FLAMETHROWER || m_iWeaponID == TF_WEAPON_LASER_POINTER)
		{
			SDK::FixMovement(pCmd, vAngles);
			pCmd->viewangles = vAngles;
			G::PSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Legit:
	{
		auto pLocal = H::Entities.GetLocal();
		if (pLocal && G::AimPoint.m_iTickCount == I::GlobalVars->tickcount)
		{
			F::BotUtils.LookLegit(pLocal, pCmd, G::AimPoint.m_vOrigin, false);
			vAngles = pCmd->viewangles;
			if (G::AimbotSteering)
				return;
			Vec3 vOldView = I::EngineClient->GetViewAngles();
			Vec3 vDelta = vAngles.DeltaAngle(vOldView);
			if (std::fabs(vDelta.x) > 0.01f || std::fabs(vDelta.y) > 0.01f || std::fabs(vDelta.z) > 0.01f)
				G::AimbotSteering = true;
		}
		break;
	}
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngles);
		pCmd->viewangles = vAngles;
		G::SilentAngles = true;
	}
}

static inline void CancelShot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, int& iLastTickCancel)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_COMPOUND_BOW:
	{
		pCmd->buttons |= IN_ATTACK2;
		pCmd->buttons &= ~IN_ATTACK;
		break;
	}
	case TF_WEAPON_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	{
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pSwap = pLocal->GetWeaponFromSlot(i);
			if (!pSwap || pSwap == pWeapon || !pSwap->CanBeSelected())
				continue;

			pCmd->weaponselect = pSwap->entindex();
			iLastTickCancel = pWeapon->entindex();
			break;
		}
	}
	}
}

static inline void DrawVisuals(int iResult, Target_t& tTarget, std::vector<Vec3>& vPlayerPath, std::vector<Vec3>& vProjectilePath, std::vector<DrawBox_t>& vBoxes)
{
	if (iResult == 1)
	{
		if (G::Attacking == 1)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
		}
		else
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount, 0 };
		}
	}

	if (G::Attacking == 1 || iResult != 1 || !Vars::Aimbot::General::AutoShoot.Value || Vars::Debug::Info.Value)
	{
		bool bPlayerPath = Vars::Visuals::Prediction::PlayerPath.Value;
		bool bProjectilePath = Vars::Visuals::Prediction::ProjectilePath.Value && (G::Attacking == 1 || Vars::Debug::Info.Value) && iResult == 1;
		bool bBoxes = Vars::Visuals::Hitbox::BoundsEnabled.Value & (Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot | Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint);
		bool bRealPath = Vars::Visuals::Prediction::RealPath.Value && iResult == 1;
		if (bPlayerPath || bProjectilePath || bBoxes || bRealPath)
		{
			G::PathStorage.clear();
			G::BoxStorage.clear();
			G::LineStorage.clear();

			if (bPlayerPath)
			{
				float flDuration = Vars::Visuals::Prediction::PlayerDrawDuration.Value;
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(vPlayerPath, !flDuration ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Prediction::PlayerPath.Value);
				if (Vars::Colors::PlayerPath.Value.a)
					G::PathStorage.emplace_back(vPlayerPath, !flDuration ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + flDuration, Vars::Colors::PlayerPath.Value, Vars::Visuals::Prediction::PlayerPath.Value, true);
			}
			if (bProjectilePath)
			{
				float flDuration = Vars::Visuals::Prediction::ProjectileDrawDuration.Value;
				if (Vars::Colors::ProjectilePathIgnoreZ.Value.a)
					G::PathStorage.emplace_back(vProjectilePath, !flDuration ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + flDuration, Vars::Colors::ProjectilePathIgnoreZ.Value, Vars::Visuals::Prediction::ProjectilePath.Value);
				if (Vars::Colors::ProjectilePath.Value.a)
					G::PathStorage.emplace_back(vProjectilePath, !flDuration ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + flDuration, Vars::Colors::ProjectilePath.Value, Vars::Visuals::Prediction::ProjectilePath.Value, true);
			}
			if (bBoxes)
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());
			if (bRealPath)
				F::Aimbot.Store(tTarget.m_pEntity, vPlayerPath.size());
		}
	}
}

bool CAimbotProjectile::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	switch (m_iWeaponID)
	{
	case TF_WEAPON_LASER_POINTER:
		if (!G::CanPrimaryAttack)
			return false;
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		if (!Vars::Aimbot::General::AutoShoot.Value && G::Attacking && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
		break;
	default:
		if (G::Throwing && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
	}
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

	m_iMethod = Vars::Aimbot::General::AimType.Value;

	if (F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && m_iWeaponID != TF_WEAPON_FLAMETHROWER)
		return false;

	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon && iRealAimType
		&& (m_iWeaponID == TF_WEAPON_COMPOUND_BOW || m_iWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || m_iWeaponID == TF_WEAPON_CANNON && G::LastUserCmd->buttons & IN_ATTACK))
	{
		pCmd->buttons |= IN_ATTACK;
		if (!G::CanPrimaryAttack && !G::Reloading && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
			return false;
	}

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionProjectile.Value);
	if (vTargets.empty())
		return false;

	if (!G::AimTarget.m_iEntIndex)
		G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2) || defined(SPLASH_DEBUG4)
	G::LineStorage.clear();
#endif
#if defined(SPLASH_DEBUG1) || defined(SPLASH_DEBUG2) || defined(SPLASH_DEBUG3) || defined(SPLASH_DEBUG4)
	G::BoxStorage.clear();
#endif
#if defined(SPLASH_DEBUG2) && defined(WORLD_DEBUG)
	G::TriangleStorage.clear();
#endif
#if defined(SPLASH_DEBUG2) && defined(DEBUG_TEXT)
	F::Debug.ClearText();
#endif
	bool bAllMissed = true;
	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();

		BoltDiagnosticScope diagnostic(m_iWeaponID==TF_WEAPON_SHOTGUN_BUILDING_RESCUE||m_iWeaponID==TF_WEAPON_GRENADELAUNCHER,m_iWeaponID,tTarget.m_pEntity->entindex(),pCmd);
		const int iResult = CanHit(tTarget, pLocal, pWeapon);
		if(s_boltDiagnostic.active) s_boltDiagnostic.result=iResult;
		if (iResult != 1 && m_iWeaponID == TF_WEAPON_CANNON && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon
			&& !(G::OriginalCmd.buttons & (IN_ATTACK | IN_USE)))
		{
			float flTime = m_flTimeTo - GRENADE_CHECK_INTERVAL;
			float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f
				? pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime
				: 1.f;
			flCharge = floorf(flCharge / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + F::ProjSim.GetDesync();
			if (flCharge < flTime)
			{
				if (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
					CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
			}
			else
			{
				pCmd->buttons |= IN_ATTACK;
				if (m_iLastTickCancel)
					pCmd->weaponselect = m_iLastTickCancel = 0;
				G::OriginalCmd.buttons |= IN_USE;
			}
		}
		if (!iResult) 
		{ 
			if (m_flAimAnglesSetTime != I::GlobalVars->curtime && m_bBestPlayerPathSet)
				m_bBlockAimAnglesDraw = true;
			continue;
		}
		bAllMissed = false;
		m_vAimAngles = m_vPlainAngles;
		if (iResult == 2)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			DrawVisuals(iResult, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		if (Vars::Aimbot::General::AutoShoot.Value)
		{
			switch (m_iWeaponID)
			{
			case TF_WEAPON_COMPOUND_BOW:
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
					pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_CANNON:
				pCmd->buttons |= IN_ATTACK;
				if (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
				{
					if (m_iLastTickCancel)
						pCmd->weaponselect = m_iLastTickCancel = 0;
					if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon)
					{	// rerun, if we won't hit in the future, fire
						int iResult2 = 0;

						float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime;
						flCharge = floorf(flCharge / GRENADE_CHECK_INTERVAL) * GRENADE_CHECK_INTERVAL + F::ProjSim.GetDesync();
						if (flCharge > GRENADE_CHECK_INTERVAL)
						{
							auto tTarget2 = tTarget;
							float flOldDetonateTime = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime();

							pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() -= GRENADE_CHECK_INTERVAL;
							iResult2 = CanHit(tTarget2, pLocal, pWeapon, false);

							pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() = flOldDetonateTime;
						}

						if (iResult2 != 1)
							pCmd->buttons &= ~IN_ATTACK;
					}
					else
						pCmd->buttons &= ~IN_ATTACK;
				}
				break;
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
			case TF_WEAPON_LUNCHBOX:
				pCmd->buttons |= IN_ATTACK2, pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_LASER_POINTER:
				pCmd->buttons &= ~IN_ATTACK2; // We are not ready to fire yet
				break;
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
			case TF_WEAPON_RAYGUN:
			case TF_WEAPON_DRG_POMSON:
			case TF_WEAPON_CROSSBOW:
				pCmd->buttons |= IN_ATTACK, pCmd->buttons &= ~IN_ATTACK2;
				if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
				{
					if (pWeapon->m_iClip1() > 0)
						pCmd->buttons &= ~IN_ATTACK;
				}
				break;
			case TF_WEAPON_GRAPPLINGHOOK:
				break;
			default:
				pCmd->buttons |= IN_ATTACK;
			}
		}

		if (m_iWeaponID != TF_WEAPON_GRAPPLINGHOOK)
		{
			if (G::Attacking = m_iWeaponID == TF_WEAPON_LASER_POINTER ? 1 : SDK::IsAttacking(pLocal, pWeapon, pCmd, true))
			{
				F::Aimbot.m_eRanType = EWeaponType::PROJECTILE;
				if (F::AutoHeal.m_iAutoSwitch == 1)
					F::AutoHeal.m_iAutoSwitch = 2;
			}
		}
		else
		{
			Vec3 vOriginalAngles = pCmd->viewangles; int iOriginalButtons = pCmd->buttons;
			pCmd->viewangles = tTarget.m_vAngleTo, pCmd->buttons |= IN_ATTACK;
			if (G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true))
				F::Aimbot.m_eRanType = EWeaponType::PROJECTILE;
			pCmd->viewangles = vOriginalAngles, pCmd->buttons = iOriginalButtons;
			if (!G::Attacking)
				continue;

			if (Vars::Aimbot::General::AutoShoot.Value)
				pCmd->buttons |= IN_ATTACK;
		}
		DrawVisuals(iResult, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);

		Aim(pCmd, tTarget.m_vAngleTo);
		if (G::PSilentAngles)
		{
			switch (m_iWeaponID)
			{
			case TF_WEAPON_FLAMETHROWER: // angles show up anyways
			case TF_WEAPON_LASER_POINTER:
			case TF_WEAPON_CLEAVER: // can't psilent with these weapons, they use SetContextThink
			case TF_WEAPON_JAR:
			case TF_WEAPON_JAR_MILK:
			case TF_WEAPON_JAR_GAS:
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
				G::PSilentAngles = false, G::SilentAngles = true;
			}
		}
		return true;
	}

	if (bAllMissed && m_iWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
		m_bMainSearchComplete = true;

	return false;
}

// TODO:
// Add projectile type options
// Make it target multiple projectiles (destroy groups)
std::vector<Target_t> CAimbotProjectile::GetProjectiles(CTFPlayer* pLocal)
{
	std::vector<Target_t> vTargets;

	int iLocalTeam = pLocal->m_iTeamNum();
	Vector vViewAngles = I::EngineClient->GetViewAngles();
	for (auto pProjectile : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pProjectile->m_iTeamNum() == iLocalTeam)
			continue;

		Vector vOrigin = pProjectile->GetAbsOrigin();
		float flDist = vOrigin.DistTo(m_vShootPos);
		if (flDist >= 1000.f)
			continue;

		auto iClassID = pProjectile->GetClassID();
		if (iClassID != ETFClassID::CTFProjectile_Rocket &&
			iClassID != ETFClassID::CTFProjectile_EnergyBall &&
			iClassID != ETFClassID::CTFProjectile_SentryRocket &&
			iClassID != ETFClassID::CTFGrenadePipebombProjectile &&
			iClassID != ETFClassID::CTFProjectile_Flare)
			continue;

		float flFov = Math::CalcFov(vViewAngles, Math::CalcAngle(m_vShootPos, vOrigin));
		if (flFov > Vars::Aimbot::General::AimFOV.Value)
			continue;

		Target_t tProj;
		tProj.m_pEntity = pProjectile;
		tProj.m_vPos = vOrigin;
		tProj.m_flDistTo = flDist;
		vTargets.push_back(tProj);
	}

	if (vTargets.empty())
		return {};

	std::sort(vTargets.begin(), vTargets.end(), [](const Target_t& a, const Target_t& b) -> bool
		{
			return a.m_flDistTo < b.m_flDistTo;
		});

	return vTargets;
}

bool CAimbotProjectile::CanHitProjectile(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, Target_t& tTarget, int iSimTicks)
{
	m_tProjInfo = {};
	F::ProjSim.GetInfo(tTarget.m_pEntity, m_tProjInfo);
	if (!F::ProjSim.Initialize(m_tProjInfo, false, true))
		return false;

	Vector vPos;
	bool bSuccess = false;	
	int iLen = static_cast<int>(m_vProjectilePath.size()) - 1;
	CGameTrace trace;
	CTraceFilterWorldAndPropsOnly filter(H::Entities.GetLocal());
	for (int i = 0; i <= iSimTicks; i++)
	{
		F::ProjSim.RunTick(m_tProjInfo);
		vPos = F::ProjSim.GetOrigin();

		if (i >= iLen)
			return false;

		float flDistToTarget = vPos.DistToSqr(m_vShootPos);
		float flDist = m_vShootPos.DistToSqr(m_vProjectilePath[i]);
		if (fabs(flDistToTarget - flDist) < 10000.f)
		{
			SDK::Trace(m_vShootPos, vPos, MASK_SHOT, &filter, &trace);
			if (!trace.DidHit())
			{
				bSuccess = true;
				break;
			}
		}
	}

	if (!bSuccess)
		return false;

	tTarget.m_vPos = vPos;
	Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vShootPos, vPos), tTarget.m_vAngleTo);

	return true;
}

bool CAimbotProjectile::RunMechArm(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	m_iMethod = Vars::Aimbot::General::AimType.Value;

	if (!m_iMethod || pLocal->m_iMetalCount() - 65 < Vars::Aimbot::Projectile::AutoSCMetalAmount.Value)
		return false;

	m_vShootPos = pLocal->GetShootPos();

	auto vTargets = GetProjectiles(pLocal);
	if (vTargets.empty())
		return false;

	m_tProjInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, m_tProjInfo, ProjSimEnum::PredictCmdNum | ProjSimEnum::NoRandomAngles)
		|| !F::ProjSim.Initialize(m_tProjInfo, false))
		return false;

	m_vProjectilePath.clear();
	Vector vStart = F::ProjSim.GetOrigin(), vEnd;
	m_vProjectilePath.push_back(vStart);

	int iSimTicks = TIME_TO_TICKS(1.2f);
	for (int i = 0; i <= iSimTicks; i++)
	{
		F::ProjSim.RunTick(m_tProjInfo);
		vEnd = F::ProjSim.GetOrigin();
		m_vProjectilePath.push_back(vEnd);
	}

	for (auto& tTarget : vTargets)
	{
		if (!CanHitProjectile(pLocal, pWeapon, tTarget, iSimTicks))
			continue;

		if (Vars::Aimbot::General::AutoShoot.Value)
			pCmd->buttons |= IN_ATTACK2, pCmd->buttons &= ~IN_ATTACK;
		if (G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true))
			F::Aimbot.m_eRanType = EWeaponType::PROJECTILE;

		Aim(pCmd, tTarget.m_vAngleTo);
		return true;
	}
	return false;
}

void CAimbotProjectile::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (m_iAimLock != 0)
		m_iAimLock--;

	m_iWeaponID = pWeapon->GetWeaponID();
	if (auto pSentryGun = pLocal->GetObjectOfType(OBJ_SENTRYGUN))
		m_pSentryGun = pSentryGun->As<CObjectSentrygun>();
	else
		m_pSentryGun = nullptr;
	if (m_iWeaponID == TF_WEAPON_PASSTIME_GUN)
		return;

	PROJ_FRAME("Run", m_iWeaponID);
	m_vBestPlayerPath.clear();
	m_bBestPlayerPathSet = false;
	m_bBlockAimAnglesDraw = false;

	int iOldAimType = Vars::Aimbot::General::AimType.Value;
	bool bOldAutoShoot = Vars::Aimbot::General::AutoShoot.Value;
	if (F::AutoHeal.m_iAutoSwitch != 0)
	{
		Vars::Aimbot::General::AimType.Value = Vars::Aimbot::General::AimTypeEnum::Silent;
		Vars::Aimbot::General::AutoShoot.Value = true;
	}

	const bool bSuccess = m_iWeaponID == TF_WEAPON_MECHANICAL_ARM ? RunMechArm(pLocal, pWeapon, pCmd) : RunMain(pLocal, pWeapon, pCmd);
	if (!bSuccess)
	{
		if (Vars::Visuals::Prediction::BestPath.Value && !m_vBestPlayerPath.empty())
		{
			if (Vars::Colors::BestPathIgnoreZ.Value.a)
				G::PathStorage.emplace_back(m_vBestPlayerPath, I::GlobalVars->curtime + TICK_INTERVAL, Vars::Colors::BestPathIgnoreZ.Value, Vars::Visuals::Prediction::BestPath.Value);
			if (Vars::Colors::BestPath.Value.a)
				G::PathStorage.emplace_back(m_vBestPlayerPath, I::GlobalVars->curtime + TICK_INTERVAL, Vars::Colors::BestPath.Value, Vars::Visuals::Prediction::BestPath.Value, true);
		}
	}
	else
	{
		if (m_iWeaponID == TF_WEAPON_LASER_POINTER)
		{
			m_iAimLock = 2;
			if (m_pSentryGun)
			{
				if (m_pSentryGun->m_hAutoAimTarget().Get())
				{
					if (m_vTarget.DistTo(m_tInfo.m_pTarget->m_pEntity->GetCenter()) < 100.f)
						pCmd->buttons |= IN_ATTACK2;
				}
				else if (auto pLaserDot = H::Entities.GetLaserDot(); pLaserDot->GetAbsOrigin().DistTo(m_vTarget) < 50.f)
					pCmd->buttons |= IN_ATTACK2;
			}
		}
		else if (m_iWeaponID == TF_WEAPON_MECHANICAL_ARM)
			m_iAimLock = 1;
	}

	if (F::AutoHeal.m_iAutoSwitch != 0)
	{
		// Force it to switch back if we cant shoot for too long
		if (!bSuccess && F::AutoHeal.m_flAutoSwitchExpireTime < I::GlobalVars->curtime)
			F::AutoHeal.m_iAutoSwitch = 2;

		Vars::Aimbot::General::AimType.Value = iOldAimType;
		Vars::Aimbot::General::AutoShoot.Value = bOldAutoShoot;
	}
#ifdef SPLASH_DEBUG5
	if (Vars::Aimbot::General::AimType.Value && !s_mTraceCount.empty())
	{
		int iTraceCount = 0;
		for (auto& iTraces : s_mTraceCount | std::views::values)
			iTraceCount += iTraces;
		SDK::Output("Traces", std::format("{}", iTraceCount).c_str());
		for (auto& [sType, iTraces] : s_mTraceCount)
			SDK::Output("Traces", std::format("{}: {}", sType, iTraces).c_str());
	}
	s_mTraceCount.clear();
#endif

	float flAmount = 0.f;
	if (m_iWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		const float flCharge = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
		flAmount = Math::RemapVal(flCharge, 0.f, SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
	}
	else if (m_iWeaponID == TF_WEAPON_CANNON)
	{
		const float flMortar = SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
		const float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() : -flMortar;
		flAmount = flMortar ? Math::RemapVal(flCharge, -flMortar, 0.f, 0.f, 1.f) : 0.f;
	}

	if (m_iWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER && G::OriginalCmd.buttons & IN_ATTACK && Vars::Aimbot::Projectile::AutoRelease.Value && flAmount > Vars::Aimbot::Projectile::AutoRelease.Value / 100)
		pCmd->buttons &= ~IN_ATTACK;
	else if (G::CanPrimaryAttack && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge)
	{
		if (m_bLastTickHeld && (G::LastUserCmd->buttons & IN_ATTACK && !(pCmd->buttons & IN_ATTACK) && !bSuccess || flAmount > 0.95f))
			CancelShot(pLocal, pWeapon, pCmd, m_iLastTickCancel);
	}

	m_bLastTickHeld = Vars::Aimbot::General::AimType.Value;
}

void CAimbotProjectile::RunPreview(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	const bool bAimPos = Vars::Visuals::Prediction::BestAimPos.Value && Vars::Colors::AimPosColor.Value.a;
	const bool bPath = Vars::Visuals::Prediction::BestPathPreview.Value && Vars::Visuals::Prediction::BestPath.Value;
	if (!bAimPos && !bPath)
		return;
	if (m_iWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER && m_bMainSearchComplete)
		return;
	if (m_flAimAnglesSetTime == I::GlobalVars->curtime)
		return;
	if (!pLocal || !pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	m_iWeaponID = pWeapon->GetWeaponID();
	switch (m_iWeaponID)
	{
	case TF_WEAPON_MECHANICAL_ARM:
	case TF_WEAPON_GRAPPLINGHOOK:
	case TF_WEAPON_PASSTIME_GUN:
		return;
	}
	PROJ_FRAME("Preview", m_iWeaponID);
	if (auto pSentryGun = pLocal->GetObjectOfType(OBJ_SENTRYGUN))
		m_pSentryGun = pSentryGun->As<CObjectSentrygun>();
	else
		m_pSentryGun = nullptr;

	m_vBestPlayerPath.clear();
	m_bBestPlayerPathSet = false;
	m_bBlockAimAnglesDraw = false;

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionProjectile.Value);
	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();

		const int iResult = CanHit(tTarget, pLocal, pWeapon);
		if (!iResult)
		{
			if (m_flAimAnglesSetTime != I::GlobalVars->curtime && m_bBestPlayerPathSet)
				m_bBlockAimAnglesDraw = true;
			continue;
		}

		m_vAimAngles = m_vPlainAngles;
		m_flAimAnglesSetTime = I::GlobalVars->curtime;
		break;
	}

	if (bPath && !m_vBestPlayerPath.empty())
	{
		if (Vars::Colors::BestPathIgnoreZ.Value.a)
			G::PathStorage.emplace_back(m_vBestPlayerPath, I::GlobalVars->curtime + TICK_INTERVAL, Vars::Colors::BestPathIgnoreZ.Value, Vars::Visuals::Prediction::BestPath.Value);
		if (Vars::Colors::BestPath.Value.a)
			G::PathStorage.emplace_back(m_vBestPlayerPath, I::GlobalVars->curtime + TICK_INTERVAL, Vars::Colors::BestPath.Value, Vars::Visuals::Prediction::BestPath.Value, true);
	}

	m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();
	m_vBestPlayerPath.clear();
}

void CAimbotProjectile::RunGrapplingHook(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	PROJ_FRAME("GrapplingHook", pWeapon->GetWeaponID());

	static auto tf_grapplinghook_enable = H::ConVars.FindVar("tf_grapplinghook_enable");
	if (!tf_grapplinghook_enable->GetBool())
	{
		if (m_tGrappleInfo.m_flRanTime)
			I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
		m_tGrappleInfo = {};
		return;
	}

	m_iWeaponID = pWeapon->GetWeaponID();
	CBaseEntity* pActionItem = m_iWeaponID != TF_WEAPON_GRAPPLINGHOOK ? pLocal->GetEntityForLoadoutSlot(LOADOUT_POSITION_ACTION) : pWeapon;
	m_pGrapplingHook = pActionItem && pActionItem->GetClassID() == ETFClassID::CTFGrapplingHook ? pActionItem->As<CTFGrapplingHook>() : nullptr;

	if (!Vars::Aimbot::Projectile::GrapplingHookAim.Value)
	{
		if (m_tGrappleInfo.m_flRanTime)
		{
			if (!m_tGrappleInfo.m_bGrapplingHookShot)
			{
				I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
				m_tGrappleInfo = {};
			}
			else if (pLocal->m_hGrapplingHookTarget() || m_pGrapplingHook && !m_pGrapplingHook->m_hProjectile())
			{
				I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
				m_tGrappleInfo = {};
			}
		}
		return;
	}

	if (!m_pGrapplingHook)
		return;

	if (m_tGrappleInfo.m_flRanTime)
	{
		const float flMaxDelay = F::Backtrack.GetReal() + 0.25f;
		const float flDelta = I::GlobalVars->curtime - m_tGrappleInfo.m_flRanTime;
		if (flDelta > flMaxDelay && !m_tGrappleInfo.m_bGrapplingHookShot)
		{
			I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
			m_tGrappleInfo.Fail();
		}
		else
		{
			if (auto pTarget = pLocal->m_hGrapplingHookTarget().Get())
			{
				I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");

				if (m_tGrappleInfo.m_flRanTime && !pTarget->IsPlayer())
					m_tGrappleInfo.Fail();
				return;
			}

			if (auto pProjectile = m_pGrapplingHook->m_hProjectile().Get())
			{
				m_tGrappleInfo.m_bGrapplingHookShot = true;
				if (!m_tGrappleInfo.m_bWallOnMiss)
				{
					auto vDelta = m_tGrappleInfo.m_vLastGrapplePoint - pProjectile->GetCenter();
					if (pProjectile->GetAbsVelocity().Dot(vDelta) < 0.f)
					{
						I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
						m_tGrappleInfo.Fail();
						return;
					}
				}
			}
			else if (!m_pGrapplingHook->m_hProjectile() && m_tGrappleInfo.m_bGrapplingHookShot)
			{
				I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
				m_tGrappleInfo = {};
				return;
			}

			if (pLocal->m_bUsingActionSlot() && !m_tGrappleInfo.m_bGrapplingHookShot)
			{
				G::Attacking = true;
				m_iMethod = Vars::Aimbot::General::AimTypeEnum::Silent;
				Aim(pCmd, m_tGrappleInfo.m_vLastAngleTo);
				G::PSilentAngles = false;
			}
		}
	}
	else if (pLocal->m_bUsingActionSlot() && m_pGrapplingHook->m_hProjectile())
	{
		if (!m_tGrappleInfo.m_bFail)
			I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
		return;
	}

	auto vTargets = F::AimbotGlobal.ManageTargets(GetPlayers, pLocal, m_pGrapplingHook, Vars::Aimbot::General::TargetSelectionProjectile.Value);
	if (vTargets.empty()) return;

	m_iWeaponID = TF_WEAPON_GRAPPLINGHOOK;
	m_iMethod = Vars::Aimbot::General::AimTypeEnum::Plain;

	auto vShootPos = pLocal->GetShootPos();
	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();
		if (tTarget.m_flDistTo > 1900.f)
			continue;

		const bool bResult = CanHit(tTarget, pLocal, m_pGrapplingHook, true);
		if (!bResult || tTarget.m_vPos.DistTo(vShootPos) > 1900.f) continue;

		bool bFirstRun = !m_tGrappleInfo.m_flRanTime;
		if (bFirstRun)
		{
			I::EngineClient->ClientCmd_Unrestricted("+use_action_slot_item");
			m_tGrappleInfo.m_flRanTime = I::GlobalVars->curtime;
			m_tGrappleInfo.m_bFail = false;
		}
		else if (m_tGrappleInfo.m_flLastTimeTo - 1.5f > m_flTimeTo)
		{
			I::EngineClient->ClientCmd_Unrestricted("-use_action_slot_item");
			m_tGrappleInfo.Fail();
			break;
		}
		m_tGrappleInfo.m_flLastTimeTo = m_flTimeTo;

		if (bFirstRun)
		{
			Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo, &vForward);

			CGameTrace wallTrace = {};
			CTraceFilterWorldAndPropsOnly filter = {};
			SDK::TraceHull(tTarget.m_vPos, tTarget.m_vPos + vForward * 500.f, -m_tInfo.m_vHull, m_tInfo.m_vHull, MASK_SHOT_HULL, &filter, &wallTrace);

			m_tGrappleInfo.m_bWallOnMiss = wallTrace.DidHit();
		}

		DrawVisuals(1, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);
		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
		m_tGrappleInfo.m_vLastGrapplePoint = tTarget.m_vPos;
		m_tGrappleInfo.m_vLastAngleTo = tTarget.m_vAngleTo;
		break;
	}
}

// TestAngle and CanHit shares a bunch of code, possibly merge somehow

bool CAimbotProjectile::TestAngle(CBaseEntity* pProjectile, const Vec3& vOrigin, const Vec3& vPoint, Vec3& vAngles, int iSimTime, uint8_t iType, uint8_t iFlags)
{
	PROJ_ZONE(TestAngle);
	PROJ_COUNT(TestAngles);

	auto pLocal = m_tInfo.m_pLocal;
	auto pWeapon = m_tInfo.m_pWeapon;
	auto& tTarget = *m_tInfo.m_pTarget;

	m_tProjInfo = {};
	F::ProjSim.GetInfo(pProjectile, m_tProjInfo);
	CGameTrace trace = {};
	{
		CTraceFilterWorldAndPropsOnly filter = {};

		Vec3 vEyePos = pLocal->GetShootPos(); // m_tInfo.m_vLocalEye is not actually our shootpos here
		m_tProjInfo.m_vPos = vOrigin;

		Vec3 vPos = vPoint;
		if (m_tInfo.m_flGravity)
			vPos += Vec3(0, 0, m_tInfo.m_flGravity * pow(TICKS_TO_TIME(iSimTime), 2) / 2);
		Vec3 vForward = (vPos - m_tProjInfo.m_vPos).Normalized();
		m_tProjInfo.m_vAng = Math::VectorAngles(vForward);

		SDK::Trace(m_tProjInfo.m_vPos, m_tProjInfo.m_vPos + vForward * MAX_TRACE_LENGTH, MASK_SOLID, &filter, &trace);
		vAngles = Math::CalcAngle(vEyePos, trace.endpos);
		vForward = (vEyePos - trace.endpos).Normalized();
		if (vForward.Dot(trace.plane.normal) <= 0)
			return false;

		SDK::Trace(vEyePos, trace.endpos, MASK_SOLID, &filter, &trace);
		if (Math::FullFraction(vEyePos, trace.endpos, trace) < 0.999f)
			return false;

		if (!F::AutoAirblast.CanAirblastEntity(pLocal, pWeapon, pProjectile, vAngles))
			return false;
	}
	if (!F::ProjSim.Initialize(m_tProjInfo, false, true))
		return false;

	CTraceFilterCollideable filter(iType == PointTypeEnum::Direct ? pLocal : tTarget.m_pEntity);
	filter.m_iPlayer = iType == PointTypeEnum::Direct ? PLAYER_DEFAULT : PLAYER_NONE;
	int nMask = MASK_SOLID;
	F::ProjSim.SetupTrace(filter, nMask, pProjectile);

	if (!m_tProjInfo.m_flGravity)
	{
		SDK::TraceHull(m_tProjInfo.m_vPos, vPoint, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);
		if (Math::FullFraction(m_tProjInfo.m_vPos, vPoint, trace) < 0.999f && trace.m_pEnt != tTarget.m_pEntity)
			return false;
	}

	bool bDidHit = false;
	Vec3 vNew = F::ProjSim.GetOrigin();
	int iTimingTolerance = TIME_TO_TICKS(m_tInfo.m_flBoundsTime);
	float flRadiusSqr = powf(m_tProjInfo.m_flVelocity * TICK_INTERVAL + m_tProjInfo.m_vHull.z, 2);
	uint8_t iTraceInterval = iFlags == PointFlagsEnum::Lob ? Vars::Aimbot::Projectile::LobTraceInterval.Value
		: iType != PointTypeEnum::Direct ? Vars::Aimbot::Projectile::SplashTraceInterval.Value
		: Vars::Aimbot::Projectile::DirectTraceInterval.Value;

	const RestoreInfo_t tOriginal = { tTarget.m_pEntity->GetAbsOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
	tTarget.m_pEntity->SetAbsOrigin(tTarget.m_vPos);
	tTarget.m_pEntity->m_vecMins() = { std::clamp(tTarget.m_pEntity->m_vecMins().x, -24.f, 0.f), std::clamp(tTarget.m_pEntity->m_vecMins().y, -24.f, 0.f), tTarget.m_pEntity->m_vecMins().z };
	tTarget.m_pEntity->m_vecMaxs() = { std::clamp(tTarget.m_pEntity->m_vecMaxs().x, 0.f, 24.f), std::clamp(tTarget.m_pEntity->m_vecMaxs().y, 0.f, 24.f), tTarget.m_pEntity->m_vecMaxs().z };
	for (int n = 1; n <= iSimTime; n++)
	{
		F::ProjSim.RunTick(m_tProjInfo);

		if (bDidHit)
		{
			trace.endpos = F::ProjSim.GetOrigin();
			continue;
		}
		if (iTraceInterval != 1 && n % iTraceInterval && n != iSimTime)
			continue;

		Vec3 vOld = vNew; vNew = F::ProjSim.GetOrigin();
		SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace);

		bool bHit = false;
		switch (iType)
		{
		case PointTypeEnum::Direct:
		case PointTypeEnum::Geometry:
			bHit = trace.DidHit(); break;
		case PointTypeEnum::Air:
			bHit = trace.endpos.DistToSqr(vPoint) < flRadiusSqr || trace.DidHit(); break;
		}

		if (bHit)
		{
			bool bValid = false, bTarget = true;
			switch (iType)
			{
			case PointTypeEnum::Direct:
				bTarget = trace.m_pEnt == tTarget.m_pEntity, bValid = bTarget && iSimTime - n < iTimingTolerance; break;
			case PointTypeEnum::Geometry:
				bValid = trace.endpos.DistToSqr(vPoint) < flRadiusSqr; break;
			case PointTypeEnum::Air:
				bValid = !trace.DidHit(); break;
			}

			if (bValid && iType != PointTypeEnum::Direct)
			{
				CGameTrace trace2 = {};
				SDK::Trace(trace.endpos + trace.plane.normal * m_tInfo.m_flNormalOffset, tTarget.m_vPos + m_tInfo.m_vTargetEye, MASK_SHOT, &filter, &trace2);
				bValid = trace2.fraction == 1.f;
			}

			if (bValid && Vars::Aimbot::Projectile::IntervalRetest.Value && iTraceInterval != 1)
			{
				CGameTrace trace2 = {}; Vec3 vOld, vNew;
				int iTicks = int(m_tProjInfo.m_vPath.size());
				if (m_tInfo.m_flGravity)
					iTicks -= iTimingTolerance;

				for (int i = 1; i < iTicks; i++)
				{
					vOld = m_tProjInfo.m_vPath[i - 1], vNew = m_tProjInfo.m_vPath[i];
					SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, nMask, &filter, &trace2);
					bValid = !trace2.DidHit();

					if (!bValid)
						break;
				}
			}

			if (bValid)
			{
				if (iTraceInterval != 1 && iType != PointTypeEnum::Direct)
				{
					int iInterval = n % iTraceInterval ? n % iTraceInterval : iTraceInterval;
					int iPopCount = ceilf(iInterval - trace.fraction * iInterval);
					for (int i = 0; i < iPopCount && !m_tProjInfo.m_vPath.empty(); i++)
						m_tProjInfo.m_vPath.pop_back();
				}

				bDidHit = true;
			}
			else
				break;

			if (iType == PointTypeEnum::Direct)
				trace.endpos = vNew;

			if (!bTarget || iType != PointTypeEnum::Direct)
				break;
		}
	}
	tTarget.m_pEntity->SetAbsOrigin(tOriginal.m_vOrigin);
	tTarget.m_pEntity->m_vecMins() = tOriginal.m_vMins;
	tTarget.m_pEntity->m_vecMaxs() = tOriginal.m_vMaxs;
	m_tProjInfo.m_vPath.push_back(trace.endpos);

	return bDidHit;
}


bool CAimbotProjectile::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pProjectile)
{
	BeginSearch();
	const bool accountPing=Vars::Aimbot::Projectile::AccountPing.Value;
	const bool playerTarget=tTarget.m_pEntity->IsPlayer();
	const Vec3 targetOrigin=ProjectileTargetOrigin(tTarget.m_pEntity,accountPing);
	ResetMoveStorage();
	if (!F::MoveSim.Initialize(tTarget.m_pEntity,m_tMoveStorage,true,true,!playerTarget||accountPing,
		accountPing&&playerTarget?&targetOrigin:nullptr) && tTarget.m_iTargetType == TargetEnum::Player)
	{
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	m_tProjInfo = {};
	F::ProjSim.GetInfo(pProjectile, m_tProjInfo);
	if (!F::ProjSim.Initialize(m_tProjInfo, false, true))
	{
		F::MoveSim.Restore(m_tMoveStorage);
		return false;
	}

	m_tInfo = { pLocal, m_tProjInfo.m_pWeapon, &tTarget, pProjectile };
	m_tInfo.m_flLatency = Vars::Aimbot::Projectile::AccountPing.Value ? F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke()) : 0.f;
	m_tInfo.m_vHull = pProjectile->m_vecMaxs().Min(3);
	{
		CGameTrace trace = {};
		CTraceFilterWorldAndPropsOnly filter = {};

		for (int i = TIME_TO_TICKS(m_tInfo.m_flLatency); i > 0; i--)
		{
			Vec3 vOld = F::ProjSim.GetOrigin();
			F::ProjSim.RunTick(m_tProjInfo);
			Vec3 vNew = F::ProjSim.GetOrigin();

			SDK::TraceHull(vOld, vNew, -m_tProjInfo.m_vHull, m_tProjInfo.m_vHull, MASK_SOLID, &filter, &trace);
			m_tProjInfo.m_vPos = trace.endpos;
		}
		m_tInfo.m_vLocalEye = m_tProjInfo.m_vPos; // just assume from the projectile without any offset, check validity later
	}
	m_tInfo.m_vTargetEye = tTarget.m_pEntity->As<CTFPlayer>()->GetViewOffset();
	tTarget.m_vPos = targetOrigin;

	m_tInfo.m_flVelocity = m_tProjInfo.m_flVelocity;

	m_tInfo.m_flGravity = m_tProjInfo.m_flGravity;
	m_tInfo.m_iSplashRestrict = !m_tInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashRestrictDirect.Value : Vars::Aimbot::Projectile::SplashRestrictArc.Value;

	float flSize = tTarget.m_pEntity->GetSize().Length();
	m_tInfo.m_flRadius = GetSplashRadius(pProjectile, pWeapon, pLocal, Vars::Aimbot::Projectile::SplashRadius.Value / 100);
	m_tInfo.m_flBoundsTime = tTarget.m_pEntity->GetSize().Length() / m_tInfo.m_flVelocity;
	m_tInfo.m_flRadiusTime = m_tInfo.m_flBoundsTime + m_tInfo.m_flRadius / m_tInfo.m_flVelocity;
	m_tInfo.m_bIgnoreTiming = Vars::Aimbot::Projectile::LobAnglesUnderpredict.Value && m_tInfo.m_flRadius;



	Directs_t mDirects = GetDirects();
	Splashes_t vSplashes = GetSplashes();

	DirectHistory_t mDirectHistory = {};
	SplashHistory_t mSplashHistory = {};

	int iMaxTime = TIME_TO_TICKS(Vars::Aimbot::Projectile::MaxSimulationTime.Value);
	for (int i = 1 - TIME_TO_TICKS(m_tInfo.m_flLatency); i <= iMaxTime; i++)
	{
		if (!m_tMoveStorage.m_bFailed)
		{
			F::MoveSim.RunTick(m_tMoveStorage);
			tTarget.m_vPos = m_tMoveStorage.m_vPredictedOrigin;
		}
		if (i < 0 || i < m_tInfo.m_iLaunchDelay)
			continue;

		for (auto offset=mDirects.begin();offset!=mDirects.end();)
		{
			auto current=offset++;
			auto& [iIndex,tOffset]=*current;
			Vec3& vOffset = tOffset.m_vOffset;
			uint8_t iType = tOffset.m_iFlags & -tOffset.m_iFlags;

			Vec3 vPoint = tTarget.m_vPos + vOffset;
			if (Vars::Aimbot::Projectile::HuntsmanPullPoint.Value && tTarget.m_nAimedHitbox == HITBOX_HEAD)
			{
				vPoint = PullPoint(vPoint, m_tInfo.m_vLocalEye, m_tInfo, tTarget.m_pEntity->m_vecMins() + m_tInfo.m_vHull, tTarget.m_pEntity->m_vecMaxs() - m_tInfo.m_vHull, tTarget.m_vPos);
				if (Vars::Aimbot::Projectile::HuntsmanPullNoZ.Value)
					vPoint.z = tTarget.m_vPos.z + vOffset.z;
			}

			uint8_t iFlags = CalculateFlagsEnum::Accuracy;
			if (iType == PointFlagsEnum::Lob)
				iFlags |= CalculateFlagsEnum::LobAngle;
			int iTolerance = m_tInfo.m_bIgnoreTiming && iType == PointFlagsEnum::Lob ? std::numeric_limits<int>::max() : -1;

			Solution_t tSolution;
			switch (iType)
			{
			case PointFlagsEnum::Lob:
				if (ShouldLob(m_tMoveStorage, m_tInfo))
					goto end;
				tSolution.m_iCalculated = CalculateResultEnum::Bad; break;
			default: end:
				CalculateAngle(m_tInfo.m_vLocalEye, vPoint, i - m_tInfo.m_iLaunchDelay, tSolution, iFlags, iTolerance);
			}
			switch (tSolution.m_iCalculated)
			{
			case CalculateResultEnum::Good:
				mDirectHistory[iType].emplace_back(History_t(tTarget.m_vPos, i), tSolution.m_flPitch, tSolution.m_flYaw, tSolution.m_flTime, vPoint, iIndex);
				[[fallthrough]];
			case CalculateResultEnum::Bad:
				tOffset.m_iFlags &= ~iType;
				if (!(tOffset.m_iFlags /*& (PointFlagsEnum::Regular | PointFlagsEnum::Lob)*/))
					mDirects.erase(iIndex);
			}
		}

		for (auto it = vSplashes.begin(); it != vSplashes.end();)
		{
			uint8_t iFlags = CalculateFlagsEnum::AccountDrag;
			if (*it == PointFlagsEnum::Lob && !m_tInfo.m_bIgnoreTiming)
				iFlags |= CalculateFlagsEnum::LobAngle;

			Solution_t tSolution; CalculateAngle(m_tInfo.m_vLocalEye, tTarget.m_vPos, i - m_tInfo.m_iLaunchDelay, tSolution, iFlags);
			if (tSolution.m_iCalculated == CalculateResultEnum::Bad && mDirects.empty())
			{
				it = vSplashes.erase(it);
				continue;
			}

			const float flTimeTo = tSolution.m_flTime - TICKS_TO_TIME(i - m_tInfo.m_iLaunchDelay);
			if (flTimeTo > m_tInfo.m_flRadiusTime)
			{
				++it;
				continue;
			}
			if (flTimeTo < -m_tInfo.m_flRadiusTime)
			{
				it = vSplashes.erase(it);
				continue;
			}
			if (*it == PointFlagsEnum::Lob && !ShouldLob(m_tMoveStorage, m_tInfo))
			{
				++it;
				continue;
			}

			mSplashHistory[*it].emplace_back(History_t(tTarget.m_vPos, i), fabsf(flTimeTo));
			++it;
		}

		if (mDirects.empty() && vSplashes.empty())
			break;
	}

	ResetSolution();
	m_bUpdate = true;
	if (!m_tInfo.m_flRadius || Vars::Aimbot::Projectile::SplashPrediction.Value < Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer)
		goto direct;
	else
		goto splash;
	while (!mDirectHistory.empty() || !mSplashHistory.empty())
	{
direct: if (HandleDirect(mDirectHistory)) break;
splash: if (HandleSplash(mSplashHistory)) break;
	}
	if (!m_bBestPlayerPathSet)
	{
		if (tTarget.m_pEntity && !tTarget.m_pEntity->GetAbsVelocity().IsZero(10.f))
			m_vBestPlayerPath = m_tMoveStorage.m_vPath;
		m_bBestPlayerPathSet = true;
	}
	F::MoveSim.Restore(m_tMoveStorage);

	tTarget.m_vPos = m_vTarget;
	tTarget.m_vAngleTo = m_vAngleTo;

	bool bMain = m_iResult == 1;
	if (bMain)
	{
		if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a || Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
		{
			float flProjectileTime = 0.f, flTargetTime = 0.f;
			bool bBox = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::OnShot;
			bool bPoint = Vars::Visuals::Hitbox::BoundsEnabled.Value & Vars::Visuals::Hitbox::BoundsEnabledEnum::AimPoint;
			bool bTimed = !Vars::Visuals::Prediction::PlayerDrawDuration.Value;
			if (bTimed)
			{
				flProjectileTime = TICKS_TO_TIME(m_vProjectilePath.size());
				flTargetTime = m_tMoveStorage.m_bFailed ? flProjectileTime : TICKS_TO_TIME(m_vPlayerPath.size());
			}
			if (bBox)
			{
				float flDuration = bTimed ? flTargetTime : Vars::Visuals::Hitbox::DrawDuration.Value;
				if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
					m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
				if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
					m_vBoxes.emplace_back(m_vPredicted, tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
			}
			if (bPoint)
			{
				float flDuration = bTimed ? flProjectileTime : Vars::Visuals::Hitbox::DrawDuration.Value;
				if (Vars::Colors::BoundHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoundHitboxFaceIgnoreZ.Value.a)
					m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdgeIgnoreZ.Value, Vars::Colors::BoundHitboxFaceIgnoreZ.Value);
				if (Vars::Colors::BoundHitboxEdge.Value.a || Vars::Colors::BoundHitboxFace.Value.a)
					m_vBoxes.emplace_back(m_vTarget, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + flDuration, Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true);
			}
		}
	}

	return m_iResult;
}

bool CAimbotProjectile::AutoAirblast(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pProjectile)
{
	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionProjectile.Value);
	if (vTargets.empty())
		return false;

	m_iWeaponID = pWeapon->GetWeaponID();
	m_iMethod = Vars::Aimbot::General::AimTypeEnum::Silent;
	m_vBestPlayerPath.clear();
	m_bBestPlayerPathSet = false;
	m_bBlockAimAnglesDraw = false;

	//if (!G::AimTarget.m_iEntIndex)
	//	G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	for (auto& tTarget : vTargets)
	{
		m_flTimeTo = std::numeric_limits<float>::max();
		m_vPlayerPath.clear(); m_vProjectilePath.clear(); m_vBoxes.clear();

		const bool bResult = CanHit(tTarget, pLocal, pWeapon, pProjectile);
		if (!bResult) 
		{ 
			if (m_flAimAnglesSetTime != I::GlobalVars->curtime && m_bBestPlayerPathSet)
				m_bBlockAimAnglesDraw = true;
			continue; 
		}

		m_vAimAngles = m_vPlainAngles;
		G::Attacking = true;
		DrawVisuals(1, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);

		Aim(pCmd, tTarget.m_vAngleTo);
		return true;
	}

	if (Vars::Visuals::Prediction::BestPath.Value && !m_vBestPlayerPath.empty())
	{
		if (Vars::Colors::BestPathIgnoreZ.Value.a)
			G::PathStorage.emplace_back(m_vBestPlayerPath, I::GlobalVars->curtime + TICK_INTERVAL, Vars::Colors::BestPathIgnoreZ.Value, Vars::Visuals::Prediction::BestPath.Value);
		if (Vars::Colors::BestPath.Value.a)
			G::PathStorage.emplace_back(m_vBestPlayerPath, I::GlobalVars->curtime + TICK_INTERVAL, Vars::Colors::BestPath.Value, Vars::Visuals::Prediction::BestPath.Value, true);
	}

	return false;
}

bool CAimbotProjectile::AimPasstimePass(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	return RunPasstime(pLocal, pWeapon, pCmd);
}

std::vector<Target_t> CAimbotProjectile::GetAbilityTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	return F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionProjectile.Value);
}

void CAimbotProjectile::ShowTarget(int iResult, Target_t& tTarget)
{
	DrawVisuals(iResult, tTarget, m_vPlayerPath, m_vProjectilePath, m_vBoxes);
}
