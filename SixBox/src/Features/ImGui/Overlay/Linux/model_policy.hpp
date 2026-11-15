#pragma once
namespace LocalModel
{
template<class Entity>
bool EnemyPart(Entity* entity, Entity* local, bool enabled, int flags)
{
    if (!enabled || flags || !local || !entity || entity->IsWearableVM()) return false;
    auto* player=entity;
    if (!player->IsPlayer())
    {
        if (!(entity->IsBaseCombatWeapon() || entity->IsWearable())) return false;
        player=entity->m_hOwnerEntity().Get();
    }
    return player && player->IsPlayer() && player!=local && player->m_iTeamNum()!=local->m_iTeamNum();
}
}
