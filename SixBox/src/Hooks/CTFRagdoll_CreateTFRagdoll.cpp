#include "../SDK/SDK.h"
#include "../Features/Visuals/SkinChanger/SkinChanger.h"

MAKE_SIGNATURE(CTFRagdoll_CreateTFRagdoll, "client.dll", "48 89 4C 24 ? 55 53 56 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 8B 91", 0x0);
// Verified against the current client: the networkable interface receives this
// callback before the gib/ragdoll branch. No fixed entity offsets are required.
MAKE_SIGNATURE(CTFRagdoll_OnDataChanged, "client.dll", "40 53 57 48 81 EC ? ? ? ? 8B DA 48 8B F9 E8 ? ? ? ? 85 DB 0F 85 ? ? ? ? 8B 97", 0x0);

MAKE_HOOK(CTFRagdoll_OnDataChanged, S::CTFRagdoll_OnDataChanged(), void,
    IClientNetworkable* rcx, DataUpdateType_t type)
{
    DEBUG_RETURN(CTFRagdoll_OnDataChanged, rcx, type);
#ifndef TEXTMODE
    if (type == DATA_UPDATE_CREATED && !G::Unload)
    {
        auto* ragdoll = reinterpret_cast<CTFRagdoll*>(rcx->GetIClientUnknown()->GetBaseEntity());
        F::SkinChanger.CreateGoldRagdoll(ragdoll, +[](void* entity)
        {
            CALL_ORIGINAL(static_cast<CTFRagdoll*>(entity)->GetClientNetworkable(), DATA_UPDATE_CREATED);
        });
        return;
    }
#endif
    CALL_ORIGINAL(rcx, type);
}

MAKE_HOOK(CTFRagdoll_CreateTFRagdoll, S::CTFRagdoll_CreateTFRagdoll(), void,
    void* rcx)
{
    DEBUG_RETURN(CTFRagdoll_CreateTFRagdoll, rcx);
#ifndef TEXTMODE
    if (Vars::Visuals::Removals::Ragdolls.Value && !SDK::CleanScreenshot()) return;
    auto* ragdoll = reinterpret_cast<CTFRagdoll*>(rcx);
    // Gold flags were prepared before OnDataChanged chose between gibs and a
    // corpse. Keep them intact; apply the existing global effects to other corpses.
    const int effects = Vars::Visuals::Effects::RagdollEffects.Value;
    if (effects && !ragdoll->m_bGoldRagdoll() && !SDK::CleanScreenshot())
    {
        using namespace Vars::Visuals::Effects::RagdollEffectsEnum;
        ragdoll->m_bGib() = false;
        ragdoll->m_bBurning() = effects & Burning;
        ragdoll->m_bElectrocuted() = effects & Electrocuted;
        ragdoll->m_bBecomeAsh() = effects & Ash;
        ragdoll->m_bDissolving() = effects & Dissolve;
        ragdoll->m_bGoldRagdoll() = effects & Gold;
        ragdoll->m_bIceRagdoll() = effects & Ice;
    }
#endif
    CALL_ORIGINAL(rcx);
}
