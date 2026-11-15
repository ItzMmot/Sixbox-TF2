FDropdown(Vars::ESP::OverlayRender, FDropdownEnum::Multi);
FTooltip("Linux/Proton only. Requires the local overlay service.");
if (Vars::ESP::OverlayRender.Value & Vars::ESP::OverlayRenderEnum::Box)
{
    FToggle(Vars::ESP::OverlayBoxGlow);
    if (Vars::ESP::OverlayBoxGlow.Value)
    {
        FSlider(Vars::ESP::OverlayBoxGlowStrength);
        FSlider(Vars::ESP::OverlayBoxGlowRadius);
    }
    FToggle(Vars::ESP::OverlayBoxStencil);
    if (Vars::ESP::OverlayBoxStencil.Value)
    {
        FSlider(Vars::ESP::OverlayBoxStencilThickness);
        FColorPicker(Vars::ESP::OverlayBoxStencilColor);
    }
}
if (Vars::ESP::OverlayRender.Value & Vars::ESP::OverlayRenderEnum::Stencil)
{
    FSlider(Vars::ESP::OverlayStencilWidth);
    FSlider(Vars::ESP::OverlayStencilRate);
    TextWrapped("Enemy model silhouettes. Group stencil width overrides the fallback width above.");
}
if (Vars::ESP::OverlayRender.Value)
    TextWrapped("%s", LocalExtension::Status());
