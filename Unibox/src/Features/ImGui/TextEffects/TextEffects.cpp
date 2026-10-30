#ifndef TEXTMODE
#include "TextEffects.h"
#include <ImGui/imgui_internal.h>
#include <string_view>

namespace TextEffects
{
    namespace
    {
        const ImFontLoader* baseLoader = nullptr;
        ImFontLoader effectLoader;
        Workspace workspace;

        bool LoadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void* userData,
            ImWchar codepoint, ImFontGlyph* glyph, float* advance)
        {
            if (!baseLoader->FontBakedLoadGlyph(atlas, src, baked, userData, codepoint, glyph, advance)) return false;
            if (!glyph || !glyph->Visible || glyph->Colored || glyph->PackId == ImFontAtlasRectId_Invalid) return true;
            const auto settings = Settings::Unpack(src->FontLoaderFlags);
            if (!settings.Active()) return true;
            const auto oldId = glyph->PackId;
            const auto old = *ImFontAtlasPackGetRect(atlas, oldId);
            const float width = glyph->X1 - glyph->X0, height = glyph->Y1 - glyph->Y0;
            if (width <= 0.f || height <= 0.f) return true;
            const float scaleX = old.w / width, scaleY = old.h / height;
            if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.f || scaleY <= 0.f || scaleX > 8.f || scaleY > 8.f) return true;
            const float halo = settings.strength > 0.f ? settings.radius : 0.f;
            const int padX = int(std::ceil((settings.expansion + halo) * scaleX)) + 1;
            const int padY = int(std::ceil((settings.expansion + halo) * scaleY)) + 1;
            const int w = old.w + padX * 2, h = old.h + padY * 2;
            if (w > Workspace::Side || h > Workspace::Side) return true;
            std::fill_n(workspace.source.begin(), w * h, 0.f);
            ImTextureData* tex = atlas->TexData;
            for (int y = 0; y < old.h; ++y)
                for (int x = 0; x < old.w; ++x)
                {
                    const auto* pixel = static_cast<const unsigned char*>(tex->GetPixelsAt(old.x + x, old.y + y));
                    const unsigned alpha = tex->Format == ImTextureFormat_Alpha8 ? *pixel
                        : ((*reinterpret_cast<const ImU32*>(pixel) >> IM_COL32_A_SHIFT) & 255);
                    workspace.source[(y + padY) * w + x + padX] = alpha / 255.f;
                }
            if (!workspace.Apply(w, h, settings, scaleX, scaleY)) return true;
            // Pack may grow/repack the atlas: source pixels and rect dimensions
            // were copied first, so no pointers into the old texture survive.
            const auto newId = ImFontAtlasPackAddRect(atlas, w, h);
            if (newId == ImFontAtlasRectId_Invalid) return true;
            auto* rect = ImFontAtlasPackGetRect(atlas, newId);
            glyph->PackId = newId;
            glyph->X0 -= padX / scaleX; glyph->X1 += padX / scaleX;
            glyph->Y0 -= padY / scaleY; glyph->Y1 += padY / scaleY;
            ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, glyph, rect, workspace.output.data(), ImTextureFormat_Alpha8, w);
            ImFontAtlasPackDiscardRect(atlas, oldId);
            return true;
        }
    }

    void ConfigureFont(ImFontAtlas* atlas, ImFontConfig& config, Settings settings)
    {
        settings = Settings::Sanitize(settings.expansion, settings.strength, settings.radius);
        if (!settings.Active()) return;
        if (!atlas->Builder) ImFontAtlasBuildInit(atlas);
        // This adapter uses the project's bundled stb loader. Other backends
        // keep their normal flags and rendering rather than interpreting ours.
        if (!atlas->FontLoader || std::string_view(atlas->FontLoader->Name).find("stb") == std::string_view::npos) return;
        if (!baseLoader)
        {
            baseLoader = atlas->FontLoader;
            effectLoader = *baseLoader;
            effectLoader.Name = "Unibox text effects";
            effectLoader.FontBakedLoadGlyph = LoadGlyph;
        }
        config.FontLoader = &effectLoader;
        config.FontLoaderFlags = settings.Pack();
    }
}
#endif
