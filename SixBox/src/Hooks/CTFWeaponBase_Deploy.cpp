#ifndef TEXTMODE
#include "../SDK/SDK.h"
#include "../Features/Visuals/SkinChanger/SkinChanger.h"

// Verified in the installed x64 client against the native deploy attribute reads.
MAKE_SIGNATURE(CTFWeaponBase_Deploy, "client.dll", "40 53 56 41 57 48 81 EC ? ? ? ? 33 F6 44 0F 29 44 24 ? 48 8B D9 44 0F 29 54 24 ? 39 B1", 0x0);

MAKE_HOOK(CTFWeaponBase_Deploy, S::CTFWeaponBase_Deploy(), bool, CTFWeaponBase* rcx)
{
    DEBUG_RETURN(CTFWeaponBase_Deploy, rcx);
    F::SkinChanger.PrepareWeaponDeploy(rcx);
    return CALL_ORIGINAL(rcx);
}
#endif
