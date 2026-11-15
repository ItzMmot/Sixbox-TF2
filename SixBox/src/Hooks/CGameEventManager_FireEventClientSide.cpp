#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Killstreak/Killstreak.h"
#include "../Features/Visuals/SkinChanger/SkinChanger.h"

MAKE_HOOK(CGameEventManager_FireEventClientSide, U::Memory.GetVirtual(I::GameEventManager, 8), bool,
	IGameEventManager2* rcx, IGameEvent* event)
{
	DEBUG_RETURN(CGameEventManager_FireEventClientSide, rcx, event);

    if (!event || G::Unload) return CALL_ORIGINAL(rcx, event);
    F::SkinChanger.GoldKillEvent(event);

	// We have to do it here because our own event listener runs after game event manager
	if (Vars::Visuals::Other::KillstreakWeapons.Value)
	{
		auto uHash = FNV1A::Hash32(event->GetName());
		if (uHash == FNV1A::Hash32Const("player_death"))
			F::Killstreak.PlayerDeath(event);
	}

	return CALL_ORIGINAL(rcx, event);
}
#endif