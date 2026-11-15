CVarEnum(OverlayRender, "Overlay render (Linux only)", 0, VISUAL | NOBIND | DROPDOWN_MULTI, "Off",
    VA_LIST("Box", "Nametags", "Stencil"), Box = 1, Nametags = 2, Stencil = 4);
CVar(OverlayBoxGlow, "Box glow", false, VISUAL | NOBIND);
CVar(OverlayBoxGlowStrength, "Box glow strength", 85.f, VISUAL | NOBIND | SLIDER_CLAMP, 0.f, 200.f, 5.f, "%g%%");
CVar(OverlayBoxGlowRadius, "Box glow radius", 8.f, VISUAL | NOBIND | SLIDER_CLAMP | SLIDER_PRECISION, 1.f, 24.f, 0.5f, "%gpx");
CVar(OverlayBoxStencil, "Box outer border", false, VISUAL | NOBIND);
CVar(OverlayBoxStencilThickness, "Box border thickness", 2.f, VISUAL | NOBIND | SLIDER_CLAMP | SLIDER_PRECISION, 1.f, 8.f, 0.5f, "%gpx");
CVar(OverlayBoxStencilColor, "Box border color", Color_t(255, 255, 255, 220), VISUAL | NOBIND);
CVar(OverlayStencilWidth, "Default stencil width", 3, VISUAL | NOBIND | SLIDER_CLAMP, 1, 16, 1, "%ipx");
CVar(OverlayStencilRate, "Stencil update rate", 60, VISUAL | NOBIND | SLIDER_CLAMP, 15, 120, 5, "%iHz");
