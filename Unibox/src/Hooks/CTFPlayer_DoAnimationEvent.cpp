#include "../SDK/SDK.h"
#include "../Features/Resolver/Resolver.h"

MAKE_SIGNATURE(CTFPlayer_DoAnimationEvent, "client.dll", "48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 41 8B E8 8B FA", 0x0);

MAKE_HOOK(CTFPlayer_DoAnimationEvent, S::CTFPlayer_DoAnimationEvent(), void,
	void* rcx, PlayerAnimEvent_t event, int nData)
{
	DEBUG_RETURN(CTFPlayer_DoAnimationEvent, rcx, event, nData);

	auto* player=reinterpret_cast<CTFPlayer*>(rcx);
	if (F::Resolver.QueueAnimationEvent(player,event,nData)) return;
	CALL_ORIGINAL(rcx,event,nData);
}