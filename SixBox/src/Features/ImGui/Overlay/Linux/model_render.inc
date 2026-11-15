// Discard extension-only entries if their owner/team or the render route
// changed since Store. Never reinterpret them as native backbuffer outlines.
for (auto& [settings,infos]:m_mEntities)
    std::erase_if(infos,[](const auto& info){return info.m_bExternal && !LocalModel::Route(info.m_pEntity,info.m_iFlags);});
if (LocalModel::Enabled())
{
    // Selected player silhouettes never fall back onto the game surface.
    auto* context=I::MaterialSystem->GetRenderContext();
    if (!context || !m_pMatGlowColor || !m_pRenderBuffer1 || !m_pMatHaloAddToScreen) return;
    const int width=H::Draw.m_nScreenW,height=H::Draw.m_nScreenH;
    if (width<=0 || height<=0) return;
    const auto selected=[](const auto& info){return info.m_bExternal || LocalModel::Route(info.m_pEntity,info.m_iFlags);};
    const auto drawGroup=[&](const Glow_t& settings,const auto& infos,bool privatePass)
    {
        SecondBegin(context,width,height);
        for (const auto& info:infos)
        {
            if (selected(info)!=privatePass) continue;
            I::RenderView->SetColorModulation(info.m_tColor);
            I::RenderView->SetBlend(info.m_tColor.a/255.f);
            m_iFlags=info.m_iFlags;DrawModel(info.m_pEntity);m_iFlags=0;
        }
        auto effect=settings;
        if (privatePass) effect.Blur=0;
        SecondEnd(effect,context,width,height);
    };
    for (const auto& [settings,infos]:m_mEntities)
        if (std::any_of(infos.begin(),infos.end(),[&](const auto& info){return !selected(info);})) drawGroup(settings,infos,false);
    if (!LocalModel::captureFrame || !LocalModel::target) return;
    LocalModel::captureFrame=false;
    try
    {
        LocalModel::TargetScope output(context,width,height);
        // The output target owns its depth/stencil surface. Rebuild the
        // unshifted model mask on that exact surface before applying EQUAL 0;
        // a mask written into the game's depth buffer does not belong here.
        FirstBegin(context);
        {
            // Keep model draw opacity nonzero; disable color writes instead of
            // relying on alpha-zero draws to execute on every studio path.
            LocalModel::ColorWriteScope noColor(context);
            I::RenderView->SetBlend(1.f);
            try
            {
                for (const auto& [settings,infos]:m_mEntities)
                    for (const auto& info:infos)
                    {
                        if (!selected(info)) continue;
                        m_iFlags=info.m_iFlags;DrawModel(info.m_pEntity);m_iFlags=0;
                    }
            }
            catch (...) { m_iFlags=0;FirstEnd(context);throw; }
            FirstEnd(context);
        }
        bool hasModels=false;
        for (const auto& [settings,infos]:m_mEntities)
        {
            if (!std::any_of(infos.begin(),infos.end(),selected)) continue;
            hasModels=true;drawGroup(settings,infos,true);
        }
        LocalModel::Publish(context,width,height,hasModels);
    }
    catch (...) { LocalExtension::ModelCaptureError(true); }
    return;
}
