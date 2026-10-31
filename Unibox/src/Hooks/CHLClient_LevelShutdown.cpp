#include "../SDK/SDK.h"
#include "../Features/Aimbot/AutoTrolldier/AutoTrolldier.h"
#include "../Features/Aimbot/AutoRocketJump/AutoRocketJump.h"

#include "../Features/Backtrack/Backtrack.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/NavBot/NavEngine.h"
#include "../Features/NavBot/Hazards.h"
#include "../Features/NavBot/Jobs/NavBotJobs.h"
#include "../Features/Misc/AutoVote/AutoVote.h"
#ifndef TEXTMODE
#include "../Features/Visuals/Aura/Aura.h"
#include "../Features/Visuals/JumpCoach/JumpCoach.h"
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#endif

MAKE_HOOK(CHLClient_LevelShutdown, U::Memory.GetVirtual(I::Client, 7), void,
	void* rcx)
{
	DEBUG_RETURN(CHLClient_LevelShutdown, rcx);

	F::AutoRocketJump.Reset();
	F::AutoTrolldier.Reset();
	F::Backtrack.Reset();
	F::Chams.Reset();
	H::Entities.Clear(true);
	F::EnginePrediction.Unload();
	F::Spectate.Reset();
#ifndef TEXTMODE
    F::Aura.ClearScene();
    F::JumpCoach.Reset();
	G::TriggerStorage.clear();
	G::PasstimeGoalStorage.clear();
#endif
	F::NavEngine.ClearRespawnRooms();
	F::Hazards.Reset();
	F::NavBotSupplies.ResetCachedOrigins();
	F::AutoVote.Reset();
#ifndef TEXTMODE
	F::SkinChanger.OnLevelShutdown();
#endif

	CALL_ORIGINAL(rcx);
}
