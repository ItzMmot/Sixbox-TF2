if (LocalModel::Route(pEntity) && GetDistanceThing(flDistance,pGroup->m_tGlow,tColor) && SDK::IsOnScreen(pEntity,pEntity->IsBaseCombatWeapon() || pEntity->IsWearable()))
{
    auto outline=pGroup->m_tGlow;
    outline.Stencil=outline.Stencil>0?std::clamp(outline.Stencil,1,16):std::clamp(Vars::ESP::OverlayStencilWidth.Value,1,16);
    outline.Blur=0;
    m_mEntities[outline].emplace_back(pEntity,tColor,0,true);
}
else
