// tests/text_effects_test.cpp — TextEffects mask and bundled ImGui integration tests.
#include "../Unibox/src/Features/ImGui/TextEffects/TextEffects.h"
#include <ImGui/imgui_internal.h>
#include <cassert>
#include <cstdio>
#include <limits>
#ifdef IMGUI_ENABLE_FREETYPE
#include <imgui_freetype.h>
#endif

using namespace TextEffects;
static Workspace scratch;
struct Metrics { float advance, width, height, textWidth; double mass; };
static Metrics Bake(const char* path, Settings effects, ImTextureFormat format, bool render = false)
{
    ImFontAtlas atlas;
    atlas.TexMinWidth = atlas.TexMinHeight = 64; // Force growth/repack during font loading.
    atlas.TexDesiredFormat = format;
    ImFontConfig config;
    config.OversampleH = 2;
#ifdef IMGUI_ENABLE_FREETYPE
    config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
#endif
    ConfigureFont(&atlas, config, effects);
#ifdef IMGUI_ENABLE_FREETYPE
    assert((config.FontLoaderFlags & 1023u) == ImGuiFreeTypeLoaderFlags_LightHinting);
#endif
    ImFont* font = atlas.AddFontFromFileTTF(path, 18.f, &config);
    assert(font && atlas.Build());
#ifdef IMGUI_ENABLE_FREETYPE
    assert(atlas.FontLoader == ImGuiFreeType::GetFontLoader());
#endif
    auto* baked = font->GetFontBaked(18.f);
    auto* glyph = baked->FindGlyph('M');
    assert(glyph && glyph->Visible);
    Metrics result {glyph->AdvanceX, glyph->X1 - glyph->X0, glyph->Y1 - glyph->Y0,
        font->CalcTextSizeA(18.f, 1000.f, 0.f, "Michroma 0123").x, 0.};
    const auto rect = *ImFontAtlasPackGetRect(&atlas, glyph->PackId);
    for (int y = 0; y < rect.h; ++y)
        for (int x = 0; x < rect.w; ++x)
        {
            const auto* pixel = static_cast<unsigned char*>(atlas.TexData->GetPixelsAt(rect.x+x, rect.y+y));
            result.mass += atlas.TexData->Format == ImTextureFormat_Alpha8 ? *pixel
                : ((*reinterpret_cast<const ImU32*>(pixel) >> IM_COL32_A_SHIFT) & 255);
        }
    if (render)
    {
        ++atlas.RefCount; // Retain the stack-owned atlas across context shutdown.
        ImGui::CreateContext(&atlas);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {800,600}; io.DeltaTime = 1.f/60.f; io.IniFilename = nullptr;
        io.FontDefault = font;
        atlas.SetTexID(1);
        ImFontAtlasUpdateNewFrame(&atlas, 1, false);
        ImGui::NewFrame();
        const int count = baked->Glyphs.Size;
        auto* draw = ImGui::GetBackgroundDrawList();
        const int vertices = draw->VtxBuffer.Size;
        for (int i = 0; i < 32; ++i) draw->AddText(font, 18.f, {10.f, 20.f}, IM_COL32(200,200,200,255), "M");
        assert(draw->VtxBuffer.Size - vertices == 32 * 4); // One quad/glyph, including glow.
        assert(baked->Glyphs.Size == count); // Cached draws never rebake glyphs.
        ImGui::Render(); ImGui::DestroyContext();
        --atlas.RefCount;
    }
    return result;
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto settings = Settings::Sanitize(.75f, 80.f, 4.5f);
    assert(Settings::Unpack(settings.Pack()) == settings);
    const auto invalid = Settings::Sanitize(std::numeric_limits<float>::quiet_NaN(), -1.f, 100.f);
    assert(invalid.expansion == 0.f && invalid.strength == 0.f && invalid.radius == 12.f);
    constexpr int size = 41, center = 20;
    std::fill(scratch.source.begin(), scratch.source.end(), 0.f);
    scratch.source[center * size + center] = 1.f;
    assert(scratch.Apply(size,size,{0,0,4},1,1));
    assert(scratch.output[center*size+center] == 255 && scratch.output[center*size+center+1] == 0);
    assert(scratch.Apply(size,size,{1,0,4},1,1));
    assert(scratch.output[center*size+center+1] == 255 && scratch.output[center*size+center+2] == 0);
    assert(scratch.Apply(size,size,{0,100,6},1,1));
    assert(scratch.output[center*size+center] == 255);
    assert(scratch.output[center*size+center+1] > scratch.output[center*size+center+4]);
    assert(scratch.output[center*size+center+1] == scratch.output[center*size+center-1]);
    assert(!scratch.Apply(Workspace::Side+1,1,{},1,1));
    assert(!scratch.Apply(10,10,{},0,1));
    for (auto format : {ImTextureFormat_RGBA32, ImTextureFormat_Alpha8})
    {
        const auto base = Bake(argv[1], {0,0,4}, format);
        const auto stroke = Bake(argv[1], {.75f,0,4}, format);
        const auto weak = Bake(argv[1], {.75f,50,5}, format);
        const auto strong = Bake(argv[1], {.75f,100,5}, format, true);
        assert(base.advance == stroke.advance && base.advance == strong.advance);
        assert(base.textWidth == stroke.textWidth && base.textWidth == strong.textWidth);
        assert(stroke.mass > base.mass && weak.mass > stroke.mass && strong.mass > weak.mass);
        assert(stroke.width > base.width && strong.width > stroke.width);
    }
    std::puts("Text effects: masks, Gaussian falloff, atlas repack, metrics and cached geometry passed");
}
