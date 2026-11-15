#pragma once
#ifndef TEXTMODE
#include "KaraokeGeometry.h"
#include "KaraokeModel.h"
#include "KaraokeGlass.h"
#include <string>
#include <unordered_map>

namespace Karaoke
{
    struct TextStyle
    {
        float size=29,maxWidth=1000,spacing=1.45f,opacity=1,emphasis=1.08f,oldStrength=.72f,fallDistance=120,fallTime=1.5f,shadow=2,glow=.3f;
        int context=2,oldEffect=0;
        ImU32 base=IM_COL32(180,184,196,255),highlight=IM_COL32(160,174,245,255),side=IM_COL32(98,110,160,255);
        CrumbleStyle crumble;
        bool volume=true,wrap=true;
        float arc=85;
        float scale=.16f,depth=2.5f,shade=.65f,specular=.3f;
        Point3 angles{0,.22f,0},light=Unit({-.5f,-.7f,-1.f});
        int detail=6;
    };
    struct TextFrame
    {
        std::array<float,16> matrix{};
        Point3 origin,right,up,normal,camera;
        float ground=-100000,radius=160,arcLimit=85;
        bool clipGround=true,stay=false;
        std::array<WorldLine,11> lines{};
        ImVec2 screen;
    };
    class TextRenderer
    {
        struct Character {ImWchar code=0;unsigned mesh=0,cluster=0;float x=0,advance=0,pivot=0;double start=0,end=0;bool timed=false;};
        struct Layout {std::vector<Character> characters;float width=0;unsigned clusters=0;};
        struct IndexedFace {std::array<uint16_t,3> indices;Point3 normal;};
        struct Glyph {std::vector<Point3> vertices;std::vector<IndexedFace> faces;bool prepared=false;};
        struct ProjectedVertex {Point3 point;ScreenPoint screen;float depth=0;};
        std::array<ProjectedVertex,4096> m_projected{};
        struct Face {std::array<ImDrawVert,3> vertices;float depth=0;};
        ImGuiID m_fontId=0;
        int m_detail=0;
        bool m_meshesPrepared=false;
        std::string m_revision,m_identity;
        size_t m_lineCount=0;
        std::unordered_map<unsigned,unsigned> m_glyphIndex;
        std::vector<Glyph> m_glyphs;
        std::vector<Layout> m_layouts;
        std::vector<Face> m_faces;
        unsigned CacheGlyph(ImFont* font,ImWchar code,int detail,bool build=true);
        void Prepare(ImFont* font,const State& state,int detail,bool meshes);
    public:
        void Invalidate();
        void Draw(ImDrawList* draw,ImFont* font,const State& state,double position,int active,float scroll,
            ImVec2 center,const TextStyle& style,const TextFrame* world,Glass* material,const GlassStyle& glass);
    };
}
#endif
