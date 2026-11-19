#include "Createmove.h"
#include "../Aimbot/SpyKnife/SpyKnife.h"
#ifndef TEXTMODE
#include "../Visuals/JumpCoach/JumpCoach.h"
#ifndef TEXTMODE
#include "../Visuals/Sandevistan/Sandevistan.h"
#endif
#endif
#include "../Aimbot/AutoTrolldier/AutoTrolldier.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../Aimbot/AutoStickyJump/AutoStickyJump.h"

#include "../Aimbot/Aimbot.h"
#include "../Backtrack/Backtrack.h"
#include "../CritHack/CritHack.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../Misc/Misc.h"
#include "../Misc/AutoDodge/AutoDodge.h"
#include "../NoSpread/NoSpread.h"
#include "../NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../PacketManip/PacketManip.h"
#include "../Resolver/Resolver.h"
#include "../Ticks/Ticks.h"
#include "../Visuals/Visuals.h"
#include "../Visuals/FakeAngle/FakeAngle.h"
#include "../Spectate/Spectate.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../NavBot/Objectives.h"
#include "../NavBot/NavBotCore.h"
#include "../NavBot/NavEngine.h"
#include "../FollowBot/FollowBot.h"
#include "../AutoJoin/AutoJoin.h"
#include "../Misc/AutoItem/AutoItem.h"
#include "../Misc/AutoVote/AutoVote.h"

MAKE_SIGNATURE(IHasGenericMeter_GetMeterMultiplier, "client.dll", "F3 0F 10 81 ? ? ? ? C3 CC CC CC CC CC CC CC 48 85 D2", 0x0);
MAKE_SIGNATURE(C_BaseAnimating_AutoAllowBoneAccess, "client.dll", "40 53 48 83 EC ? 41 0F B6 C0 44 0F B6 CA", 0x0);
MAKE_SIGNATURE(C_BaseAnimating_AutoAllowBoneAccessOnDelete, "client.dll", "B9 ? ? ? ? E9 ? ? ? ? CC CC CC CC CC CC 48 89 5C 24", 0x0);

void CCreateMove::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	G::PSilentAngles = G::SilentAngles = G::Attacking = G::Throwing = false;
	G::LastUserCmd = G::CurrentUserCmd ? G::CurrentUserCmd : pCmd;
	G::CurrentUserCmd = pCmd;
	G::OriginalCmd = *pCmd;

	if (!pWeapon)
		return;

	SDK::CanAttack(pLocal, pWeapon, pCmd, G::CanPrimaryAttack, G::CanSecondaryAttack, G::Reloading);
	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd);
	G::PrimaryWeaponType = SDK::GetWeaponType(pWeapon, &G::SecondaryWeaponType);
	G::CanHeadshot = pWeapon->CanHeadshot() || pWeapon->AmbassadorCanHeadshot(TICKS_TO_TIME(pLocal->m_nTickBase()));
}

void CCreateMove::Run(int nSequenceNum, float flInputSampleFrametime)
{
	{
		char autoallow[16];
		S::C_BaseAnimating_AutoAllowBoneAccess.Call<void>(autoallow, true, false);
		I::MDLCache->BeginLock();
		I::Input->CreateMove(nSequenceNum, flInputSampleFrametime, !I::ClientState->IsPaused());
		I::MDLCache->EndLock();
		S::C_BaseAnimating_AutoAllowBoneAccessOnDelete.Call<void>(autoallow);
	}

#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::CHLClient_CreateMove[DEFAULT_BIND])
		return;
#endif

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
	{
		F::AutoDodge.Reset();
		F::AutoStickyJump.Reset();
		F::SpyKnife.Reset(true);
		F::AutoRocketJump.Reset();
		F::AutoTrolldier.Reset();
#ifndef TEXTMODE
        F::JumpCoach.Reset();
#endif
		return;
	}

	auto pWeapon = H::Entities.GetWeapon();
	CUserCmd* pCmd = &I::Input->m_pCommands[nSequenceNum % MULTIPLAYER_BACKUP];
	I::Prediction->Update(I::ClientState->m_nDeltaTick, I::ClientState->m_nDeltaTick > 0, I::ClientState->last_command_ack, I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands);

	UpdateInfo(pLocal, pWeapon, pCmd);
	F::Resolver.BeginCommand();
#ifndef TEXTMODE
	F::Spectate.CreateMove(pCmd);
#endif
	F::Misc.RunPre(pLocal, pCmd);
	F::AutoJoin.Run(pLocal);
	F::AutoItem.Run(pLocal);
	SDK::RefreshTriggerStorage();
	F::GameObjectiveController.Update();
	F::BotUtils.Run(pLocal, pWeapon, pCmd);
	F::AutoVote.Run(pLocal);
	F::Backtrack.CreateMove(pLocal, pWeapon, pCmd);

	F::AutoDodge.Prepare(pLocal, pCmd);
	F::AutoStickyJump.Prepare(pLocal, pWeapon, pCmd);
	if (F::AutoStickyJump.Controls(pCmd))
	{
		F::AutoRocketJump.Reset();
		F::AutoTrolldier.Reset();
		F::SpyKnife.Reset(true);
	}
	else
	{
		F::AutoRocketJump.Prepare(pLocal, pWeapon, pCmd);
		F::AutoTrolldier.UpdateAutomation(pLocal, pWeapon, pCmd);
		F::AutoTrolldier.Prepare(pLocal, pWeapon, pCmd);
		F::SpyKnife.Prepare(pLocal,pWeapon,pCmd);
	}
	F::AutoStickyJump.Apply(pLocal,pCmd);
	F::Ticks.BeginBurstCommand(pLocal, pWeapon, pCmd);
	F::Ticks.Start(pLocal, pCmd);
	{
		if (!F::AutoStickyJump.Controls(pCmd) && !F::AutoTrolldier.SuppressCombat())
			F::Aimbot.Run(pLocal, pWeapon, pCmd);
		F::AutoTrolldier.AfterAim(pLocal, pWeapon, pCmd);
		F::Ticks.EnforceBurstCommand(pLocal, pWeapon, pCmd);
	}
	F::Ticks.End(pLocal, pCmd);
	F::SpyKnife.AfterAim(pLocal,pWeapon,pCmd);
	{
		if (!F::AutoStickyJump.Controls(pCmd)&&!F::AutoDodge.Controls(pCmd)&&!F::AutoTrolldier.IsActive(pCmd)&&!F::SpyKnife.Controls(pCmd))
		{
			F::FollowBot.Run(pLocal, pCmd);
			F::NavBotCore.Run(pLocal, pWeapon, pCmd);
			F::NavEngine.Run(pLocal, pWeapon, pCmd);
			F::VSHController.RunBoss(pLocal, pCmd);
			F::ZIController.CreateMove(pLocal, pCmd);
			F::BotUtils.HandleSmartJump(pLocal, pCmd);
		}
		if (!F::AutoStickyJump.Controls(pCmd)) F::CritHack.Run(pLocal, pWeapon, pCmd);
		F::CritHack.CacheDrawInfo(pLocal);
		F::NoSpread.Run(pLocal, pWeapon, pCmd);
		F::Misc.RunPost(pLocal, pCmd);
		if (!F::AutoDodge.Controls(pCmd)) F::Misc.AutoFaNJump(pLocal, pWeapon, pCmd);
		F::AutoTrolldier.RotateCamera(pLocal, pCmd);
		F::AutoTrolldier.ApplyMovement(pCmd);
		F::SpyKnife.ApplyMovement(pLocal,pWeapon,pCmd);
		const bool dodging = F::AutoDodge.Controls(pCmd);
		F::AutoDodge.Prepare(pLocal, pCmd, true);
        if (F::AutoDodge.Controls(pCmd)) F::AutoStickyJump.Reset();
		F::AutoDodge.ApplyMovement(pCmd);
		if (!dodging && F::AutoDodge.Controls(pCmd))
		{
			F::EnginePrediction.End(pLocal, nullptr);
			F::EnginePrediction.Start(pLocal, pCmd);
		}
		F::Ticks.EnforceBurstCommand(pLocal, pWeapon, pCmd);
		F::PacketManip.Run(pLocal, pWeapon, pCmd);
		F::Ticks.CreateMove(pLocal, pWeapon, pCmd);
		F::AntiAim.Run(pLocal, pWeapon, pCmd);
		F::AntiCheatCompatibility.CreateMove(pCmd);
		F::SpyKnife.Finalize(pLocal,pWeapon,pCmd);
		F::AutoDodge.ApplyMovement(pCmd);
		F::AutoStickyJump.Apply(pLocal,pCmd);
		F::Resolver.CommitShot(pLocal,pWeapon,pCmd);
		
#ifndef TEXTMODE
		F::Visuals.CreateMove(pLocal, pWeapon, pCmd);
		F::Visuals.LocalAnimations(pLocal, pWeapon, pCmd);
#endif
	}
#ifndef TEXTMODE
    const int trailKind=F::Ticks.m_bWarp?Sandevistan::Warp:F::Ticks.m_bDoubletap?Sandevistan::Doubletap:Sandevistan::None;
    F::Sandevistan.MarkCommand(pLocal,pCmd,trailKind,std::max(0,F::Ticks.m_iShiftedTicks-F::Ticks.m_iShiftedGoal));
    // Capture while this command's predicted player state is still active.
    // Engine RunSimulation may skip already acknowledged commands on localhost.
    if(F::EnginePrediction.m_bInPrediction)
        F::Sandevistan.CapturePrediction(pLocal,pCmd,I::GlobalVars->curtime);
#endif
	F::EnginePrediction.End(pLocal, pCmd);
#ifndef TEXTMODE
    F::JumpCoach.Update(pLocal,pWeapon,pCmd);
#endif
		F::Resolver.CreateMove();
		F::NoSpreadHitscan.AskForPlayerPerf();
	G::Choking = !G::SendPacket, G::LastUserCmd = pCmd;
}
