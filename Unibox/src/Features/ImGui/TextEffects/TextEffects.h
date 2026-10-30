#pragma once
#include <ImGui/imgui.h>
#include "GlyphMask.h"

namespace TextEffects
{
    // Call only during font/atlas setup, before the next ImGui frame starts.
    void ConfigureFont(ImFontAtlas* atlas, ImFontConfig& config, Settings settings);
}
