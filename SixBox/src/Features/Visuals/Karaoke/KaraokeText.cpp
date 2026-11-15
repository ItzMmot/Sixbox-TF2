#include "KaraokeText.h"
#ifndef TEXTMODE
#include <algorithm>
#include <limits>
#include <map>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <ImGui/imstb_truetype.h>

namespace Karaoke
{
    namespace
    {
        ImU32 Tint(ImU32 color,float alpha,float shade=1)
        {
            const auto c=ImGui::ColorConvertU32ToFloat4(color);
            return ImGui::ColorConvertFloat4ToU32({c.x*shade,c.y*shade,c.z*shade,c.w*std::clamp(alpha,0.f,1.f)});
        }
        ImU32 Pack(ImVec4 c,float shade,float spec)
        {
            return IM_COL32(int(std::clamp(c.x*shade+spec,0.f,1.f)*255+.5f),int(std::clamp(c.y*shade+spec,0.f,1.f)*255+.5f),
                int(std::clamp(c.z*shade+spec,0.f,1.f)*255+.5f),int(std::clamp(c.w,0.f,1.f)*255+.5f));
        }
        ImU32 Blend(ImU32 a,ImU32 b,float t)
        {
            const auto x=ImGui::ColorConvertU32ToFloat4(a),y=ImGui::ColorConvertU32ToFloat4(b);
            return ImGui::ColorConvertFloat4ToU32({std::lerp(x.x,y.x,t),std::lerp(x.y,y.y,t),std::lerp(x.z,y.z,t),std::lerp(x.w,y.w,t)});
        }
    }
    void TextRenderer::Invalidate()
    {
        m_fontId=0;m_revision.clear();m_identity.clear();m_lineCount=0;m_layouts.clear();m_glyphIndex.clear();m_glyphs.clear();m_meshesPrepared=false;
    }
    unsigned TextRenderer::CacheGlyph(ImFont* font,ImWchar code,int detail,bool build)
    {
        unsigned id=0;
        if(auto found=m_glyphIndex.find(code);found!=m_glyphIndex.end())id=found->second;
        else
        {
            if(m_glyphs.size()>=1024)return 0;
            id=unsigned(m_glyphs.size());m_glyphs.emplace_back();m_glyphIndex[code]=id;
        }
        if(!build||m_glyphs[id].prepared)return id;m_glyphs[id].prepared=true;
        auto* glyph=font->GetFontBaked(64.f)->FindGlyph(code);
        if(!glyph||!glyph->Visible||glyph->SourceIdx>=unsigned(font->Sources.Size))return id;
        auto* source=font->Sources[glyph->SourceIdx];if(!source->FontData||source->FontDataSize<=0)return id;
        stbtt_fontinfo info{};
        auto* data=static_cast<const unsigned char*>(source->FontData);
        const int offset=stbtt_GetFontOffsetForIndex(data,int(source->FontNo));
        if(offset<0||!stbtt_InitFont(&info,data,offset))return id;
        const int index=stbtt_FindGlyphIndex(&info,int(glyph->Codepoint));if(!index)return id;
        stbtt_vertex* vertices=nullptr;const int count=stbtt_GetGlyphShape(&info,index,&vertices);
        if(count<=0||!vertices)return id;
        std::vector<Outline> rings;Outline ring;OutlinePoint previous{};
        const auto append=[&](OutlinePoint point){if(ring.empty()||std::abs(point[0]-ring.back()[0])+std::abs(point[1]-ring.back()[1])>1e-4f)ring.push_back(point);};
        for(int i=0;i<count;++i)
        {
            const auto v=vertices[i];const OutlinePoint end{float(v.x),float(-v.y)};
            if(v.type==STBTT_vmove)
            {
                if(ring.size()>2){if(ring.front()==ring.back())ring.pop_back();rings.push_back(std::move(ring));ring={};}
                append(end);
            }
            else if(v.type==STBTT_vline)append(end);
            else
            {
                const OutlinePoint control{float(v.cx),float(-v.cy)},control2{float(v.cx1),float(-v.cy1)};
                for(int step=1;step<=detail;++step)
                {
                    const float t=float(step)/detail,u=1-t;
                    append(v.type==STBTT_vcubic?OutlinePoint{u*u*u*previous[0]+3*u*u*t*control[0]+3*u*t*t*control2[0]+t*t*t*end[0],u*u*u*previous[1]+3*u*u*t*control[1]+3*u*t*t*control2[1]+t*t*t*end[1]}:
                        OutlinePoint{u*u*previous[0]+2*u*t*control[0]+t*t*end[0],u*u*previous[1]+2*u*t*control[1]+t*t*end[1]});
                }
            }
            previous=end;
        }
        if(ring.size()>2){if(ring.front()==ring.back())ring.pop_back();rings.push_back(std::move(ring));}
        stbtt_FreeShape(&info,vertices);
        float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
        for(const auto& contour:rings)for(const auto& p:contour){x0=(std::min)(x0,p[0]);y0=(std::min)(y0,p[1]);x1=(std::max)(x1,p[0]);y1=(std::max)(y1,p[1]);}
        if(x1<=x0||y1<=y0)return id;
        // Match ImGui's merged-font offsets, glyph expansion and current font metrics.
        for(auto& contour:rings)for(auto& p:contour){p[0]=glyph->X0+(p[0]-x0)/(x1-x0)*(glyph->X1-glyph->X0);p[1]=glyph->Y0+(p[1]-y0)/(y1-y0)*(glyph->Y1-glyph->Y0);}
        const auto mesh=Extrude(std::move(rings));auto& cached=m_glyphs[id];
        std::map<std::array<float,3>,uint16_t> indices;
        for(const auto& triangle:mesh.faces)
        {
            IndexedFace face;face.normal=triangle.normal;
            for(int v=0;v<3;++v)
            {
                const auto point=triangle.points[v];const std::array<float,3> key{point.x,point.y,point.z};
                auto found=indices.find(key);
                if(found==indices.end())
                {
                    if(cached.vertices.size()>=m_projected.size()){cached.vertices.clear();cached.faces.clear();return id;}
                    const auto index=uint16_t(cached.vertices.size());cached.vertices.push_back(point);indices[key]=index;face.indices[v]=index;
                }
                else face.indices[v]=found->second;
            }
            cached.faces.push_back(face);
        }
        return id;
    }
    void TextRenderer::Prepare(ImFont* font,const State& state,int detail,bool meshes)
    {
        detail=std::clamp(detail,3,16);
        const bool fontChanged=m_fontId!=font->FontId||m_detail!=detail;
        if(fontChanged){Invalidate();m_fontId=font->FontId;m_detail=detail;m_glyphs.reserve(1024);m_glyphs.emplace_back();m_glyphIndex.reserve(1024);}
        if(!fontChanged&&m_revision==state.lyricsRevision&&m_identity==state.identity&&m_lineCount==state.lines.size())
        {
            if(meshes&&!m_meshesPrepared)
            {
                for(const auto& layout:m_layouts)for(const auto& character:layout.characters)CacheGlyph(font,character.code,detail);
                m_meshesPrepared=true;
            }
            return;
        }
        m_meshesPrepared=false;
        m_revision=state.lyricsRevision;m_identity=state.identity;m_lineCount=state.lines.size();m_layouts.clear();m_layouts.reserve(state.lines.size());
        for(const auto& line:state.lines)
        {
            Layout layout;layout.characters.reserve(line.text.size());
            const char* cursor=line.text.data();const char* end=cursor+line.text.size();bool join=false;float clusterPivot=0;
            while(cursor<end)
            {
                const size_t byte=size_t(cursor-line.text.data());const auto unicode=Decode(cursor,end);
                const ImWchar code=unicode>IM_UNICODE_CODEPOINT_MAX?font->FallbackChar:ImWchar(unicode);
                const bool newCluster=!Combining(unicode)&&!join;
                if(newCluster)++layout.clusters;
                join=unicode==0x200d;
                const auto mesh=CacheGlyph(font,code,detail,false);
                auto* baked=font->GetFontBaked(64.f);const float advance=baked->GetCharAdvance(code);
                if(newCluster||layout.characters.empty())clusterPivot=layout.width+advance*.5f;
                Character character{code,mesh,layout.clusters?layout.clusters-1:0,layout.width,advance,clusterPivot};
                size_t begin=0;float segmentX=0;
                for(const auto& segment:line.segments)
                {
                    const size_t next=begin+segment.text.size();
                    const float width=font->CalcTextSizeA(64.f,FLT_MAX,0,segment.text.c_str()).x;
                    if(byte>=begin&&byte<next&&width>0)
                    {
                        const double duration=(std::max)(.001,segment.end-segment.start);
                        character.start=segment.start+duration*std::clamp((layout.width-segmentX)/width,0.f,1.f);
                        character.end=segment.start+duration*std::clamp((layout.width+advance-segmentX)/width,0.f,1.f);character.timed=true;break;
                    }
                    segmentX+=width;begin=next;
                }
                layout.characters.push_back(character);layout.width+=advance;
            }
            m_layouts.push_back(std::move(layout));
        }
        if(meshes)
        {
            for(const auto& layout:m_layouts)for(const auto& character:layout.characters)CacheGlyph(font,character.code,detail);
            m_meshesPrepared=true;
        }
    }
    void TextRenderer::Draw(ImDrawList* draw,ImFont* font,const State& state,double position,int active,float scroll,ImVec2 center,
        const TextStyle& style,const TextFrame* world,Glass* material,const GlassStyle& glass)
    {
        if(state.lines.empty())return;
        Prepare(font,state,style.detail,world&&style.volume);
        bool glassReady=material&&material->Ready()&&glass.mode!=0;
        const bool volume=world&&style.volume&&style.depth>.001f;
        const int focus=(std::max)(0,active),first=(std::max)(0,focus-std::clamp(style.context,0,5));
        const int last=(std::min)(int(state.lines.size())-1,focus+std::clamp(style.context,0,5));
        if(volume&&m_faces.capacity()<131072)m_faces.reserve(131072);
        m_faces.clear();
        draw->PushTexture(font->OwnerAtlas->TexRef);
        if(glassReady)glassReady=material->BeginText(draw,glass,volume);
        const ImVec2 white=font->OwnerAtlas->TexUvWhitePixel;
        const Rotation globalRotation(style.angles);
        const Point3 cameraRight=world?Unit({world->matrix[0],world->matrix[1],world->matrix[2]}):Point3{};
        const Point3 cameraUp=world?Unit({world->matrix[4],world->matrix[5],world->matrix[6]}):Point3{};
        for(int index=first;index<=last;++index)
        {
            if(size_t(index)>=m_layouts.size())break;
            TextFrame anchored;
            const TextFrame* frame=world;
            if(world&&world->stay)
            {
                const auto found=std::find_if(world->lines.begin(),world->lines.end(),[&](const WorldLine& line){return line.index==index;});
                if(found==world->lines.end()||!found->visible)continue;
                anchored=*world;anchored.origin=found->origin;anchored.right=found->right;anchored.normal=found->normal;anchored.ground=found->ground;anchored.radius=found->radius;anchored.arcLimit=found->arcLimit;frame=&anchored;
            }
            const auto& layout=m_layouts[index];const auto& line=state.lines[index];const bool current=index==active;
            const double age=index<active?position-RetirementTime(state,size_t(index+1)):0;
            const auto old=index<active?Retire(style.oldEffect,age,style.oldStrength,style.fallDistance,style.fallTime):OldLine{};
            if(old.alpha<.005f)continue;
            float size=style.size*(current?style.emphasis:1.f);
            float ratio=(std::min)(size/64.f,style.maxWidth/(std::max)(1.f,layout.width));
            if(world&&style.wrap)
            {
                const float span=(std::max)(1.f,frame->radius)*(std::min)(std::clamp(style.arc,30.f,160.f),frame->arcLimit)*.01745329252f;
                ratio=(std::min)(size/64.f,span/((std::max)(.001f,style.scale)*(std::max)(1.f,layout.width)));
            }
            size=ratio*64;
            const float left=center.x-layout.width*ratio*.5f,top=center.y+((world&&world->stay)?0:(index-scroll)*style.size*style.spacing);
            const uint32_t seed=Mix(uint32_t(index)*23527u+uint32_t(line.text.size()));
            for(const auto& character:layout.characters)
            {
                auto motion=index<active&&style.oldEffect==3?Crumble(age,character.cluster,layout.clusters,seed,style.crumble):LetterMotion{};
                const float opacity=style.opacity*old.alpha*motion.alpha*(index>active?.65f:1.f);
                if(opacity<.004f)continue;
                const float fill=current?(character.timed?Progress(position,character.start,character.end):1.f):0;
                const float x=left+character.x*ratio,y=top;
                const auto base=Tint(style.base,opacity,old.shade),highlight=Tint(style.highlight,opacity);
                const auto baseRGB=ImGui::ColorConvertU32ToFloat4(base),highlightRGB=ImGui::ColorConvertU32ToFloat4(highlight);
                const auto sideRGB=ImGui::ColorConvertU32ToFloat4(Tint(style.side,opacity));
                const auto turn=Point3{motion.rotation.x,motion.rotation.y,motion.rotation.z+old.angle};
                const Rotation letterRotation(turn);
                const float cx=left+character.pivot*ratio,cy=y+size*.5f;
                const float radius=world?(std::max)(1.f,frame->radius):1;
                const Point3 pivot{(cx-center.x+motion.displacement.x)*style.scale,(cy-center.y)*style.scale+motion.displacement.y+old.drop,motion.displacement.z};
                const Rotation curveRotation({0,world&&style.wrap?pivot.x/radius:0,0});
                const Point3 curvePivot=world&&style.wrap?Cylinder(pivot,radius):pivot;
                const auto transform=[&](Point3 p)
                {
                    if(world)
                    {
                        Point3 local=letterRotation.Apply({(p.x-cx)*style.scale,(p.y-cy)*style.scale,p.z});
                        if(style.wrap)local=curveRotation.Apply(globalRotation.Apply(local))+curvePivot;
                        else local=globalRotation.Apply(local+curvePivot);
                        return frame->origin+frame->right*local.x-frame->up*local.y+frame->normal*local.z;
                    }
                    p=letterRotation.Apply({p.x-cx,p.y-cy,p.z})+Point3{cx,cy,0};
                    return p+Point3{motion.displacement.x,motion.displacement.y+old.drop,0};
                };
                const auto& mesh=m_glyphs[character.mesh];
                if(volume&&!mesh.faces.empty()&&m_faces.size()+mesh.faces.size()<=m_faces.capacity())
                {
                    const Point3 offset=transform({x,y,0});
                    const auto direction=[&](Point3 p)
                    {
                        p=style.wrap?curveRotation.Apply(globalRotation.Apply(letterRotation.Apply(p))):globalRotation.Apply(letterRotation.Apply(p));
                        return frame->right*p.x-frame->up*p.y+frame->normal*p.z;
                    };
                    const float scale=ratio*style.scale;
                    const Point3 axisX=direction({scale,0,0}),axisY=direction({0,scale,0}),axisZ=direction({0,0,style.depth});
                    const Point3 normalX=direction({1,0,0}),normalY=direction({0,1,0}),normalZ=direction({0,0,1});
                    const Point3 halfway=Unit(style.light+Unit(world->camera-transform({cx,cy,style.depth*.5f})));
                    for(size_t i=0;i<mesh.vertices.size();++i)
                    {
                        const auto p=mesh.vertices[i];auto& output=m_projected[i];
                        output.point=offset+axisX*p.x+axisY*p.y+axisZ*p.z;
                        output.screen=Project(world->matrix.data(),output.point.x,output.point.y,output.point.z,world->screen.x,world->screen.y);
                        if(frame->clipGround&&output.point.z<frame->ground-2)output.screen.visible=false;
                        output.depth=world->matrix[12]*output.point.x+world->matrix[13]*output.point.y+world->matrix[14]*output.point.z+world->matrix[15];
                    }
                    for(const auto& triangle:mesh.faces)
                    {
                        const auto& a=m_projected[triangle.indices[0]];
                        const auto& b=m_projected[triangle.indices[1]];
                        const auto& c=m_projected[triangle.indices[2]];
                        if(!a.screen.visible||!b.screen.visible||!c.screen.visible)continue;
                        // Extrusion normals lie either in xy or on z; uniform xy scale
                        // preserves their unit length through both cached rotations.
                        const auto n=triangle.normal;const Point3 normal=normalX*n.x+normalY*n.y+normalZ*n.z;
                        if(Dot(normal,world->camera-a.point)<=0)continue;
                        const bool side=std::abs(n.z)<.5f;
                        const float light=.3f+.7f*(std::max)(0.f,Dot(normal,style.light));
                        float spec=(std::max)(0.f,Dot(halfway,normal));spec*=spec;const float power8=(spec*spec)*(spec*spec);spec=power8*power8*power8*style.specular;
                        const float shade=std::lerp(1.f,light,style.shade);
                        const ImVec2 uv=glassReady?ImVec2{Dot(normal,cameraRight),-Dot(normal,cameraUp)}:white;
                        auto shadeColor=[&](ImVec4 rgb)
                        {
                            if(side){rgb.x+=(sideRGB.x-rgb.x)*.72f;rgb.y+=(sideRGB.y-rgb.y)*.72f;rgb.z+=(sideRGB.z-rgb.z)*.72f;rgb.w+=(sideRGB.w-rgb.w)*.72f;}
                            return Pack(rgb,shade,spec);
                        };
                        const bool partial=current&&fill>0&&fill<.999f;
                        const ImU32 faceColor=shadeColor(current&&fill>=.999f?highlightRGB:baseRGB);
                        const ImU32 litColor=partial?shadeColor(highlightRGB):faceColor;
                        Face face;face.depth=(a.depth+b.depth+c.depth)/3;
                        for(int v=0;v<3;++v)
                        {
                            const auto& p=m_projected[triangle.indices[v]];
                            ImU32 color=faceColor;
                            if(partial)
                            {
                                const float glyphX=mesh.vertices[triangle.indices[v]].x;
                                color=Blend(faceColor,litColor,std::clamp((fill*character.advance-glyphX)/(std::max)(1.f,character.advance*.06f),0.f,1.f));
                            }
                            face.vertices[v]={{p.screen.x,p.screen.y},uv,color};
                        }
                        m_faces.push_back(face);
                    }
                }
                else
                {
                    auto* glyph=font->GetFontBaked(64.f)->FindGlyph(character.code);
                    if(!glyph||!glyph->Visible)continue;
                    if(glassReady&&volume){material->EndText(draw);glassReady=material->BeginText(draw,glass,false);}
                    const int begin=draw->VtxBuffer.Size;
                    auto paint=[&](ImU32 color,float dx,float dy,float amount=1.f)
                    {
                        float x0=x+glyph->X0*ratio+dx,x1=x+glyph->X1*ratio+dx;
                        const float y0=y+glyph->Y0*ratio+dy,y1=y+glyph->Y1*ratio+dy;
                        float u0=glyph->U0,u1=glyph->U1;
                        if(amount<1)
                        {
                            const float edge=x+character.advance*ratio*amount;
                            if(edge<=x0)return;
                            if(edge<x1){u1=u0+(u1-u0)*(edge-x0)/(x1-x0);x1=edge;}
                        }
                        if(glyph->Colored)color|=~IM_COL32_A_MASK;
                        draw->PrimReserve(6,4);draw->PrimRectUV({x0,y0},{x1,y1},{u0,glyph->V0},{u1,glyph->V1},color);
                    };
                    if(!glassReady&&style.shadow>0)paint(IM_COL32(8,10,16,int(170*opacity)),style.shadow,style.shadow);
                    paint(base,0,0);
                    if(current&&fill>0)
                    {
                        if(!glassReady&&style.glow>0)
                            for(int side=0;side<4;++side)paint(Tint(style.highlight,opacity*style.glow*.17f),side==0?-2.f:side==1?2.f:0,side==2?-2.f:side==3?2.f:0);
                        paint(highlight,0,0,fill);
                    }
                    bool visible=true;
                    for(int vertex=begin;vertex<draw->VtxBuffer.Size;++vertex)
                    {
                        auto& v=draw->VtxBuffer[vertex];const auto p=transform({v.pos.x,v.pos.y,0});
                        if(world)
                        {
                            const auto projected=Project(world->matrix.data(),p.x,p.y,p.z,world->screen.x,world->screen.y);
                            if(!projected.visible||(frame->clipGround&&p.z<frame->ground-2)){visible=false;break;}
                            v.pos={projected.x,projected.y};
                        }
                        else v.pos={p.x,p.y};
                    }
                    if(!visible)for(int vertex=begin;vertex<draw->VtxBuffer.Size;++vertex)draw->VtxBuffer[vertex].col=0;
                    if(glassReady&&volume){material->EndText(draw);glassReady=material->BeginText(draw,glass,true);}
                }
            }
        }
        if(!m_faces.empty())
        {
            std::sort(m_faces.begin(),m_faces.end(),[](const Face& a,const Face& b){return a.depth>b.depth;});
            draw->PushTexture(font->OwnerAtlas->TexRef);
            for(size_t begin=0;begin<m_faces.size();)
            {
                const size_t count=(std::min)(size_t(16384),m_faces.size()-begin);
                draw->PrimReserve(int(count*3),int(count*3));
                for(size_t i=begin;i<begin+count;++i)
                {
                    const auto& face=m_faces[i];const auto index=draw->_VtxCurrentIdx;
                    draw->PrimWriteIdx(ImDrawIdx(index));draw->PrimWriteIdx(ImDrawIdx(index+1));draw->PrimWriteIdx(ImDrawIdx(index+2));
                    for(const auto& v:face.vertices)draw->PrimWriteVtx(v.pos,glassReady?v.uv:white,v.col);
                }
                begin+=count;
            }
            draw->PopTexture();
        }
        if(glassReady)material->EndText(draw);
        draw->PopTexture();
    }
}
#endif
