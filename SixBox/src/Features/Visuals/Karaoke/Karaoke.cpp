#include "Karaoke.h"
#ifndef TEXTMODE
#include "../../Configs/Configs.h"
#include "../../Binds/Binds.h"
#include "../../ImGui/Render.h"
#include "../../ImGui/Menu/Menu.h"
#include "../../ImGui/Fonts/MaterialDesign/IconDefinitions.h"
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <deque>
#include <charconv>
#include <shellapi.h>

namespace
{
    using Tree=boost::property_tree::ptree;
    double WallTime()
    {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    std::filesystem::path BridgeDirectory()
    {
        wchar_t path[32768]{};
        const DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",path,DWORD(std::size(path)));
        if(!count||count>=std::size(path))throw std::runtime_error("LOCALAPPDATA is unavailable");
        return std::filesystem::path(path)/L"SixBox"/L"Karaoke";
    }
    void WriteRequest(const std::filesystem::path& path,const Tree& request)
    {
        auto temp=path;temp+=L".tmp";
        {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);boost::property_tree::write_json(stream,request,false);stream.flush();if(!stream)throw std::runtime_error("Bridge write failed");}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Bridge replacement failed");
    }
    double Finite(const Tree& tree,const char* key,double fallback=0)
    {
        const double value=tree.get<double>(key,fallback);return std::isfinite(value)?value:fallback;
    }
    std::string String(const Tree& tree,const char* key,size_t limit=8192)
    {
        auto text=tree.get<std::string>(key,"");if(text.size()>limit)text.resize(limit);return text;
    }
    void ReadState(const std::filesystem::path& path,::Karaoke::State& state,const std::string& epoch)
    {
        if(std::filesystem::file_size(path)>2*1024*1024)throw std::runtime_error("Bridge status too large");
        Tree document;std::ifstream stream(path,std::ios::binary);boost::property_tree::read_json(stream,document);
        const double age=WallTime()-Finite(document,"updated");
        if(age>3||age< -5)throw std::runtime_error("Bridge heartbeat expired");
        state.received=GetTickCount64();state.error=String(document,"error");
        if(document.get<std::string>("epoch","")==epoch)state.ack=document.get<uint64_t>("ack",0);
        state.controlError=String(document,"control_error");
        if(auto track=document.get_child_optional("track"))
        {
            state.title=String(*track,"title",1024);state.artist=String(*track,"artist",1024);state.album=String(*track,"album",1024);
            state.source=String(*track,"source",128);state.bus=String(*track,"bus",512);state.track=String(*track,"id",2048);
            state.identity=String(*track,"identity",128);state.repeat=String(*track,"repeat",32);
            state.position=(std::max)(0.,Finite(*track,"position"));state.duration=(std::max)(0.,Finite(*track,"duration"));
            state.positionKnown=track->get<bool>("position_known",true);
            state.playing=track->get<bool>("playing",false);state.rate=std::clamp(Finite(*track,"rate",1),.01,4.);
            if(state.playing&&state.positionKnown)state.position+=std::clamp(WallTime()-Finite(*track,"sampled",Finite(document,"updated")),0.,3.)*state.rate;
            state.canPlay=track->get<bool>("can_play",false);state.canNext=track->get<bool>("can_next",false);
            state.canPrevious=track->get<bool>("can_previous",false);state.canSeek=track->get<bool>("can_seek",false);
            state.canRepeat=track->get<bool>("can_repeat",false);
        }
        Tree lyricDocument;
        auto lyrics=document.get_child_optional("lyrics");
        const auto revision=document.get<std::string>("lyrics_revision","");state.lyricsRevision=revision;
        if(!lyrics&&!revision.empty())
        {
            const auto lyricPath=path.parent_path()/L"lyrics.json";
            if(std::filesystem::file_size(lyricPath)>2*1024*1024)throw std::runtime_error("Lyrics too large");
            std::ifstream content(lyricPath,std::ios::binary);boost::property_tree::read_json(content,lyricDocument);
            if(lyricDocument.get<std::string>("revision","")==revision)lyrics=lyricDocument.get_child_optional("payload");
        }
        if(lyrics)
        {
            if(state.error.empty())state.error=String(*lyrics,"error");
            state.provider=String(*lyrics,"provider",128);state.copyright=String(*lyrics,"copyright",2048);
            state.synced=lyrics->get<bool>("synced",false);state.instrumental=lyrics->get<bool>("instrumental",false);
            if(auto credits=lyrics->get_child_optional("attribution"))
                for(const auto& value:*credits)
                {
                    if(state.credits.size()>=8)break;
                    state.credits.push_back({String(value.second,"name",256),String(value.second,"url",2048)});
                }
            if(auto lines=lyrics->get_child_optional("lines"))
                for(const auto& value:*lines)
                {
                    if(state.lines.size()>=2000)break;
                    const auto& item=value.second;
                    ::Karaoke::Line line;line.text=String(item,"text");line.start=(std::max)(0.,Finite(item,"start"));
                    line.end=(std::max)(line.start+.05,Finite(item,"end",line.start+5));
                    if(auto segments=item.get_child_optional("segments"))
                        for(const auto& segment:*segments)
                        {
                            if(line.segments.size()>=500)break;
                            const double start=(std::max)(0.,Finite(segment.second,"start"));
                            line.segments.push_back({String(segment.second,"text"),start,(std::max)(start+.01,Finite(segment.second,"end",start+.5))});
                        }
                    if(!line.text.empty())state.lines.push_back(std::move(line));
                }
            std::stable_sort(state.lines.begin(),state.lines.end(),[](const auto& a,const auto& b){return a.start<b.start;});
        }
    }
    ImU32 Color(Color_t color,float alpha=1,float shade=1)
    {
        return IM_COL32(int(color.r*shade),int(color.g*shade),int(color.b*shade),int(std::clamp(color.a*alpha,0.f,255.f)));
    }
    void OpenCredit(const std::string& url)
    {
        if(url.starts_with("https://"))ShellExecuteA(nullptr,"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
}

CKaraoke::CKaraoke()
{
    auto initial=std::make_shared<::Karaoke::State>();initial->error="Connecting to the local music bridge...";m_state=std::move(initial);
}
CKaraoke::~CKaraoke(){Shutdown();}
void CKaraoke::Shutdown()
{
    m_shutdown.store(true);m_stop.store(true);m_wake.notify_all();
    if(m_worker.joinable())m_worker.join();
    m_glass.Release();m_textGlass.Release();
}
void CKaraoke::ReleaseDevice(){m_glass.Release();m_textGlass.Release();}
void CKaraoke::CommandPlayer(const char* action,const std::string& value,const ::Karaoke::State& state)
{
    std::lock_guard lock(m_mutex);
    if(m_count>=m_commands.size())return;
    auto& command=m_commands[(m_head+m_count)%m_commands.size()];
    command={++m_sequence,action,value,state.bus,state.track,state.identity,WallTime()};++m_count;m_wake.notify_one();
}
void CKaraoke::Worker()
{
    std::deque<std::shared_ptr<const ::Karaoke::State>> retired;
    const std::string epoch=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
    std::filesystem::path directory;
    try{directory=BridgeDirectory();std::filesystem::create_directories(directory);}catch(...)
    {
        auto error=std::make_shared<::Karaoke::State>();error->error="The local music bridge directory is unavailable.";
        std::lock_guard lock(m_mutex);m_state=std::move(error);return;
    }
    bool wroteDisabled=false;uint64_t nextPoll=0;
    while(!m_stop.load())
    {
        Request desired;Command command;
        {
            std::unique_lock lock(m_mutex);m_wake.wait_for(lock,std::chrono::milliseconds(m_request.enabled&&m_request.visualizer?16:200));
            if(m_stop.load())break;
            desired=m_request;if(m_count)command=m_commands[m_head];
        }
        if(desired.enabled&&desired.visualizer)
        {
            ::Karaoke::AudioPacket audio;
            try
            {
                const auto path=directory/L"audio.bin";
                if(std::filesystem::file_size(path)==sizeof(audio))
                {
                    std::ifstream stream(path,std::ios::binary);stream.read(reinterpret_cast<char*>(&audio),sizeof(audio));
                    if(!stream)audio={};
                }
            }catch(...){}
            std::lock_guard lock(m_mutex);m_audio=audio;
        }
        else {std::lock_guard lock(m_mutex);m_audio={};}
        const auto tick=GetTickCount64();if(tick<nextPoll)continue;nextPoll=tick+200;
        if(!desired.enabled&&wroteDisabled)continue;
        auto fresh=std::make_shared<::Karaoke::State>();
        try
        {
            Tree document;document.put("version",1);document.put("updated",WallTime());document.put("epoch",epoch);
            document.put("enabled",desired.enabled);document.put("settings.provider",desired.provider);document.put("settings.source",desired.source);document.put("settings.visualizer",desired.visualizer);
            if(desired.enabled){document.put("settings.spicy_key",desired.spicy);document.put("settings.musixmatch_key",desired.musixmatch);}
            if(command.sequence)
            {
                document.put("command.sequence",command.sequence);document.put("command.action",command.action);
                document.put("command.value",command.value);document.put("command.bus",command.bus);document.put("command.track",command.track);
                document.put("command.identity",command.identity);document.put("command.created",command.created);
            }
            WriteRequest(directory/L"request.json",document);wroteDisabled=!desired.enabled;
            if(desired.enabled)ReadState(directory/L"status.json",*fresh,epoch);
            else fresh->received=GetTickCount64();
            if(command.sequence&&(fresh->ack>=command.sequence||WallTime()-command.created>4))
            {
                std::lock_guard lock(m_mutex);
                if(m_count&&m_commands[m_head].sequence==command.sequence){m_commands[m_head]={};m_head=(m_head+1)%m_commands.size();--m_count;}
            }
        }
        catch(...)
        {
            fresh->received=GetTickCount64();
            fresh->error="Music bridge offline. Run scripts/karaoke/install.py and check sixbox-karaoke.service.";
        }
        {
            std::lock_guard lock(m_mutex);
            if(m_state)retired.push_back(std::move(m_state));
            m_state=std::move(fresh);
        }
        // Destroy text vectors on this worker, not when the render thread releases its snapshot.
        std::erase_if(retired,[](const auto& value){return value.use_count()==1;});
    }
    try
    {
        Tree document;document.put("version",1);document.put("updated",WallTime());document.put("epoch",epoch);document.put("enabled",false);
        WriteRequest(directory/L"request.json",document);
    }catch(...){}
}

void CKaraoke::ResetWorld()
{
    m_place.store(true);m_anchored=false;m_stayAnchors={};m_stayIdentity.clear();m_stayRevision.clear();m_staying=false;
    std::lock_guard lock(m_worldMutex);m_world.valid=false;
}
void CKaraoke::CacheWorld()
{
    using namespace Vars::Visuals::World::Karaoke;
    // Called from IEngineVGui::Paint after the main-thread W2S matrix update.
    bool enabled,follow,occlusion,stay;int outputs,context;float distance,height,row,offset;
    {
        std::scoped_lock settings(F::Binds.m_mMutex);
        enabled=Enabled.Value;outputs=Outputs.Value;follow=WorldFollow.Value;occlusion=WorldOcclusion.Value;
        distance=std::clamp(WorldDistance.Value,40.f,1000.f);height=std::clamp(WorldHeight.Value,-100.f,160.f);
        stay=WorldStay.Value;context=std::clamp(Context.Value,0,5);offset=Offset.Value;
        row=FontSize.Value*LineSpacing.Value*WorldScale.Value;
    }
    if(!enabled||!(outputs&2)||!I::EngineClient->IsInGame()) {ResetWorld();return;}
    auto* local=H::Entities.GetLocal();if(!local){ResetWorld();return;}
    CViewSetup view;const Vec3 camera=I::Client->GetPlayerView(view)?view.origin:local->GetEyePosition();
    Vec3 angles=I::EngineClient->GetViewAngles();angles.x=0;angles.z=0;
    Vec3 forward,right;Math::AngleVectors(angles,&forward,&right);
    const bool place=m_place.exchange(false);
    if(place||stay!=m_staying){m_stayAnchors={};m_stayIdentity.clear();m_stayRevision.clear();m_staying=stay;}
    if(!m_anchored||place||follow||stay)
    {
        m_anchor=camera+forward*distance+Vec3(0,0,height);
        m_right=right;m_normal=forward;m_radius=distance;m_anchored=true;
        const auto& matrix=H::Draw.m_mWorldToProjection.m;
        const float focal=std::sqrt(matrix[0][0]*matrix[0][0]+matrix[0][1]*matrix[0][1]+matrix[0][2]*matrix[0][2]);
        m_arcLimit=std::clamp(2*std::atan(1.f/(std::max)(.01f,focal))*57.2957795f*.88f,15.f,160.f);
        CTraceFilterWorldAndPropsOnly filter(local);CGameTrace trace{};
        SDK::Trace(m_anchor+Vec3(0,0,32),m_anchor-Vec3(0,0,2048),MASK_SOLID,&filter,&trace);
        m_ground=trace.fraction<1?trace.endpos.z:-100000.f;
    }
    WorldFrame frame;frame.origin=m_anchor;frame.right=m_right;frame.up={0,0,1};frame.normal=m_normal;frame.camera=camera;frame.ground=m_ground;
    frame.radius=m_radius;frame.arcLimit=m_arcLimit;frame.matrix=H::Draw.m_mWorldToProjection;frame.valid=true;
    if(occlusion&&!stay)
    {
        CTraceFilterWorldAndPropsOnly filter(local);CGameTrace hit{};
        SDK::Trace(camera,m_anchor,MASK_SOLID,&filter,&hit);
        frame.valid=hit.fraction>.98f&&!hit.startsolid;
    }
    frame.stay=stay;
    if(stay)
    {
        std::shared_ptr<const ::Karaoke::State> state;{std::lock_guard lock(m_mutex);state=m_state;}
        if(state)
        {
            if(state->identity!=m_stayIdentity||state->lyricsRevision!=m_stayRevision)
            {m_stayAnchors={};m_stayIdentity=state->identity;m_stayRevision=state->lyricsRevision;}
            ++m_stayFrame;const auto tick=GetTickCount64();
            const int active=(std::max)(0,state->positionKnown?::Karaoke::ActiveLine(*state,::Karaoke::Position(*state,tick)+offset):-1);
            const int first=(std::max)(0,active-context),last=(std::min)(int(state->lines.size())-1,active+context);
            CTraceFilterWorldAndPropsOnly filter(local);int slot=0;
            for(int line=first;line<=last&&slot<int(frame.lines.size());++line,++slot)
            {
                const size_t index=size_t(line)%m_stayAnchors.size();auto& anchor=m_stayAnchors[index];
                const float lineOffset=anchor.line==line&&anchor.seen+1==m_stayFrame?anchor.offset:(line-active)*row;
                const Vec3 target=m_anchor-Vec3(0,0,lineOffset);
                const bool moved=anchor.Update(line,m_stayFrame,tick*.001,{target.x,target.y,target.z},angles.y,lineOffset);
                const Vec3 origin{anchor.origin.x,anchor.origin.y,anchor.origin.z};
                if(moved)
                {
                    CGameTrace ground{};SDK::Trace(origin+Vec3(0,0,32),origin-Vec3(0,0,2048),MASK_SOLID,&filter,&ground);
                    m_stayGround[index]=ground.fraction<1?ground.endpos.z:-100000.f;m_stayRadius[index]=m_radius;m_stayArc[index]=m_arcLimit;
                }
                auto& placed=frame.lines[slot];placed.index=line;placed.origin=anchor.origin;placed.ground=m_stayGround[index];placed.radius=m_stayRadius[index];placed.arcLimit=m_stayArc[index];placed.visible=true;
                const float yaw=anchor.yaw*.01745329252f;placed.normal={std::cos(yaw),std::sin(yaw),0};placed.right={std::sin(yaw),-std::cos(yaw),0};
                if(occlusion)
                {
                    CGameTrace hit{};SDK::Trace(camera,origin,MASK_SOLID,&filter,&hit);placed.visible=hit.fraction>.98f&&!hit.startsolid;
                }
            }
        }
    }
    std::lock_guard lock(m_worldMutex);m_world=frame;
}

void CKaraoke::DrawVisualizer(ImDrawList* draw,ImVec2 pos,ImVec2 size,const ::Karaoke::AudioPacket& audio,uint64_t identity)
{
    using namespace Vars::Visuals::World::Karaoke;
    const float dt=std::clamp(ImGui::GetIO().DeltaTime,0.f,.05f);
    const double now=WallTime();
    if(m_audioIdentity!=identity)
    {
        m_audioIdentity=identity;m_audioSequence=0;m_bands={};m_peaks={};m_waveLeft={};m_waveRight={};m_sparks={};
        m_beat=m_bass=m_snare=m_treble=m_previousOnset=0;m_lastBurst=0;
    }
    const bool valid=::Karaoke::ValidAudio(audio,now,identity)&&(audio.flags&1);
    const bool newFrame=valid&&audio.sequence!=m_audioSequence;
    const float response=std::clamp(VisualizerReaction.Value,0.f,1.f),gain=VisualizerGain.Value;
    const float attack=.03f-.026f*response,release=.18f-.13f*response;
    m_bass=::Karaoke::Envelope(m_bass,valid?std::clamp(audio.bass*gain,0.f,1.f):0,dt,attack,.1f);
    m_snare=::Karaoke::Envelope(m_snare,valid?std::clamp(audio.snare*gain,0.f,1.f):0,dt,attack,.055f);
    m_treble=::Karaoke::Envelope(m_treble,valid?audio.treble:0,dt,attack,.04f);
    m_beat=::Karaoke::Envelope(m_beat,valid?audio.onset:0,dt,.003f,.055f);
    const auto primary=Highlight.Value,secondary=VisualizerColor.Value;
    auto color=[&](float t,float alpha)
    {
        t=std::clamp(t,0.f,1.f);
        return IM_COL32(int(std::lerp(float(primary.r),float(secondary.r),t)),int(std::lerp(float(primary.g),float(secondary.g),t)),
            int(std::lerp(float(primary.b),float(secondary.b),t)),int(255*std::clamp(alpha,0.f,1.f)));
    };
    draw->PushClipRect(pos,{pos.x+size.x,pos.y+size.y},true);
    const float base=pos.y+size.y*.72f,step=size.x/64,glow=VisualizerGlow.Value;
    const float pulse=m_bass*VisualizerPulse.Value;
    draw->AddRectFilledMultiColor(pos,{pos.x+size.x,pos.y+size.y},color(0,.015f+pulse*.035f),color(1,.015f+pulse*.035f),color(1,0),color(0,0));
    // Drum halos and the sharp stereo ribbon sit behind the spectral columns.
    const ImVec2 center{pos.x+size.x*.5f,base};
    if(m_beat>.015f)
    {
        const float radius=size.y*(.17f+.23f*(1-m_beat));
        draw->AddCircle(center,radius,color(.5f,m_beat*glow*.16f),48,2+3*m_bass);
        draw->AddCircle(center,radius*.82f,color(.1f,m_beat*.23f),48,1);
    }
    for(size_t i=0;i<m_waveLeft.size();++i)
    {
        m_waveLeft[i]=::Karaoke::Envelope(m_waveLeft[i],valid?audio.left[i]:0,dt,.009f,.035f);
        m_waveRight[i]=::Karaoke::Envelope(m_waveRight[i],valid?audio.right[i]:0,dt,.009f,.035f);
        if(!i)continue;
        const float x0=pos.x+(i-1)*size.x/95,x1=pos.x+i*size.x/95;
        const float amplitude=size.y*(.09f+.08f*pulse)*gain;
        const ImVec2 a{x0,pos.y+size.y*.40f+m_waveLeft[i-1]*amplitude},b{x1,pos.y+size.y*.40f+m_waveLeft[i]*amplitude};
        const ImVec2 c{x0,pos.y+size.y*.43f-m_waveRight[i-1]*amplitude},d{x1,pos.y+size.y*.43f-m_waveRight[i]*amplitude};
        draw->AddLine(a,b,color(.05f,.09f*glow),5);draw->AddLine(c,d,color(.95f,.09f*glow),5);
        draw->AddLine(a,b,color(.05f,.28f+.28f*m_snare),1);draw->AddLine(c,d,color(.95f,.28f+.28f*m_treble),1);
    }
    for(size_t i=0;i<m_bands.size();++i)
    {
        const float target=valid?std::clamp(audio.bands[i]*gain,0.f,1.f):0;
        m_bands[i]=::Karaoke::Envelope(m_bands[i],target,dt,attack,release);
        m_peaks[i]=(std::max)(m_bands[i],m_peaks[i]-dt*(.55f+response));
        const float t=float(i)/63,energy=m_bands[i],height=energy*size.y*(.52f+.06f*pulse);
        const float x=pos.x+i*step+step*.18f,w=(std::max)(1.f,step*.62f);
        if(height>.2f)
        {
            for(int layer=2;layer>0;--layer)
            {
                const float spread=layer*1.6f;
                draw->AddRectFilled({x-spread,base-height-spread},{x+w+spread,base+spread},color(t,glow*energy*.035f/float(layer)),3);
            }
            draw->AddRectFilledMultiColor({x,base-height},{x+w,base},color(t,.65f+energy*.3f),color(t,.65f+energy*.3f),color(t,.13f),color(t,.13f));
            draw->AddRectFilled({x,base-height},{x+w,base-height+(std::min)(1.8f,height)},color(t,.8f));
            draw->AddRectFilledMultiColor({x,base+3},{x+w,base+3+height*.32f},color(t,energy*.16f),color(t,energy*.16f),color(t,0),color(t,0));
        }
        const float peakY=base-m_peaks[i]*size.y*.58f;
        if(m_peaks[i]>.05f)draw->AddLine({x,peakY},{x+w,peakY},color(t,.24f),1);
    }
    draw->AddLine({pos.x,base+1},{pos.x+size.x,base+1},color(.5f,.12f+(.12f*pulse)),1);
    if(newFrame)
    {
        m_audioSequence=audio.sequence;
        if(audio.onset>.25f&&(audio.onset>m_previousOnset+.12f||now-m_lastBurst>.045)&&now-m_lastBurst>.03)
        {
            m_lastBurst=now;uint32_t seed=uint32_t(audio.sequence*2654435761u);
            auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return (seed&0xffff)/65535.f;};
            const int count=int(VisualizerParticles.Value*(2+6*audio.onset));int spawned=0;
            for(auto& spark:m_sparks)
            {
                if(spark.life>0)continue;
                if(spawned>=count)break;
                spark.x=random();spark.y=.72f;spark.vx=(random()-.5f)*.8f;
                spark.vy=-.35f-random()*(.6f+audio.snare);spark.total=spark.life=.2f+random()*.4f;++spawned;
            }
        }
        m_previousOnset=audio.onset;
    }
    for(auto& spark:m_sparks)
    {
        if(spark.life<=0)continue;
        spark.life-=dt;spark.x+=spark.vx*dt;spark.y+=spark.vy*dt;spark.vy+=1.2f*dt;
        const float alpha=std::clamp(spark.life/spark.total,0.f,1.f)*VisualizerParticles.Value;
        const ImVec2 point{pos.x+spark.x*size.x,pos.y+spark.y*size.y};
        draw->AddCircleFilled(point,2.4f,color(spark.x,alpha*.07f*glow),8);
        draw->AddLine(point,{point.x-spark.vx*size.x*.012f,point.y-spark.vy*size.y*.012f},color(spark.x,alpha*.7f),1.25f);
    }
    draw->PopClipRect();
}

void CKaraoke::Draw(IDirect3DDevice9* device,bool canDraw)
{
    using namespace Vars::Visuals::World::Karaoke;
    if(m_shutdown.load())return;
    {
        std::scoped_lock settings(F::Binds.m_mMutex);std::lock_guard lock(m_mutex);
        const bool audioWanted=Enabled.Value&&Visualizer.Value&&(Outputs.Value&4)&&(VisualizerMode.Value==0||!m_state||m_state->lines.empty());
        bool changed=m_request.visualizer!=audioWanted||m_request.enabled!=Enabled.Value||m_request.provider!=Provider.Value||m_request.source!=Source.Value
            ||m_request.spicy!=SpicyKey.Value||m_request.musixmatch!=MusixmatchKey.Value;
        if(changed)
        {
            if(!Enabled.Value){m_glass.Release();m_textGlass.Release();m_errorNotice={};m_controlNotice={};m_seekDragging=false;for(auto& command:m_commands)command={};m_head=m_count=0;}
            m_request.visualizer=audioWanted;
            m_request.enabled=Enabled.Value;m_request.provider=Provider.Value;m_request.source=Source.Value;
            m_request.spicy=SpicyKey.Value;m_request.musixmatch=MusixmatchKey.Value;
            m_wake.notify_one();
        }
        if(Enabled.Value&&!m_worker.joinable())
        {
            try{m_worker=std::thread(&CKaraoke::Worker,this);}catch(...){return;}
        }
    }
    std::scoped_lock settings(F::Binds.m_mMutex);
    if(!Enabled.Value||!canDraw||!Outputs.Value)return;
    std::shared_ptr<const ::Karaoke::State> state;
    {std::lock_guard lock(m_mutex);state=m_state;}
    if(!state)return;
    const double noticeTime=GetTickCount64()*.001;
    m_errorNotice.Update(state->error,state->identity,noticeTime);
    m_controlNotice.Update(state->controlError,state->identity,noticeTime);
    const auto screen=ImGui::GetIO().DisplaySize;
    if(screen.x<1||screen.y<1)return;
    ImFont* font=Font.Value==2?F::Render.FontMono:Font.Value==1?F::Render.FontBold:F::Render.FontRegular;
    if(!font)font=ImGui::GetFont();
    const double position=::Karaoke::Position(*state,GetTickCount64())+Offset.Value;
    const int active=state->positionKnown?::Karaoke::ActiveLine(*state,position):-1;
    const float target=state->synced?float((std::max)(0,active)):float(::Karaoke::PlainLinePosition(*state,position));
    if(m_scrollIdentity!=state->identity||m_scrollRevision!=state->lyricsRevision||std::abs(target-m_scroll)>8)
    {
        m_scrollIdentity=state->identity;m_scrollRevision=state->lyricsRevision;m_scroll=target;
    }
    const float response=Smoothing.Value<=0?1.f:1-std::exp(-ImGui::GetIO().DeltaTime/(.015f+.45f*Smoothing.Value));
    m_scroll+=(target-m_scroll)*response;
    ::Karaoke::GlassStyle textGlass;
    textGlass.mode=TextGlass.Value;textGlass.blur=TextBlur.Value;textGlass.refraction=TextRefraction.Value;
    textGlass.chroma=TextChromatic.Value;textGlass.frost=TextFrost.Value;textGlass.edge=TextEdge.Value;
    textGlass.opacity=TextGlassOpacity.Value;textGlass.tint.w=TextTint.Value;
    if(textGlass.mode)m_textGlass.Capture(device,textGlass);
    else if(m_textGlass.Ready())m_textGlass.Release();
    ::Karaoke::TextStyle text;
    text.spacing=LineSpacing.Value;text.opacity=Opacity.Value;text.emphasis=ActiveScale.Value;text.context=Context.Value;
    text.oldEffect=OldText.Value;text.oldStrength=OldStrength.Value;text.fallDistance=FallDistance.Value;text.fallTime=FallTime.Value;
    text.shadow=Shadow.Value;text.glow=Glow.Value;text.base=Color(TextColor.Value);text.highlight=Color(Highlight.Value);
    text.crumble={CrumbleStagger.Value,CrumbleDuration.Value,CrumbleDistance.Value,CrumbleSpread.Value,CrumbleSpin.Value,CrumbleDepth.Value,CrumbleGravity.Value,CrumbleOrder.Value};
    text.wrap=WorldWrap.Value;text.arc=WorldArc.Value;text.volume=WorldVolume.Value;text.depth=WorldDepth.Value;text.scale=WorldScale.Value;text.detail=WorldMeshDetail.Value;
    text.shade=WorldShade.Value;text.specular=WorldSpecular.Value;text.side=Color(WorldSideColor.Value);
    constexpr float radians=.01745329252f;
    text.angles={WorldPitch.Value*radians,WorldYaw.Value*radians,WorldRoll.Value*radians};
    const float lightPitch=WorldLightPitch.Value*radians,lightYaw=WorldLightYaw.Value*radians;
    text.light={std::cos(lightPitch)*std::cos(lightYaw),std::cos(lightPitch)*std::sin(lightYaw),std::sin(lightPitch)};
    auto drawLyrics=[&](ImDrawList* draw,ImVec2 center,float size,float maxWidth,bool world,const WorldFrame* frame)
    {
        text.size=size;text.maxWidth=maxWidth;
        ::Karaoke::TextFrame geometry;
        if(world&&frame)
        {
            std::copy(&frame->matrix.m[0][0],&frame->matrix.m[0][0]+16,geometry.matrix.begin());
            auto point=[](const Vec3& p){return ::Karaoke::Point3{p.x,p.y,p.z};};
            geometry.origin=point(frame->origin);geometry.right=point(frame->right);geometry.up=point(frame->up);
            geometry.normal=point(frame->normal);geometry.camera=point(frame->camera);geometry.screen=screen;
            geometry.radius=frame->radius;geometry.arcLimit=frame->arcLimit;geometry.ground=frame->ground;geometry.clipGround=WorldGround.Value;geometry.stay=frame->stay;geometry.lines=frame->lines;
        }
        m_textRenderer.Draw(draw,font,*state,position,active,m_scroll,center,text,world?&geometry:nullptr,&m_textGlass,textGlass);
    };
    auto* background=ImGui::GetBackgroundDrawList();
    if(Outputs.Value&1)drawLyrics(background,{TextX.Value*screen.x,TextY.Value*screen.y},FontSize.Value,screen.x*.9f,false,nullptr);
    if(Outputs.Value&2)
    {
        WorldFrame frame;{std::lock_guard lock(m_worldMutex);frame=m_world;}
        if(frame.valid)
        {
            // CPU text clipping uses logical screen coordinates before projection.
            // Broaden only that stage, then restore real GPU scissor rectangles.
            background->PushClipRect({-100000,-100000},{100000,100000},false);
            const int firstCommand=background->CmdBuffer.Size-1;
            drawLyrics(background,{screen.x*.5f,screen.y*.5f},FontSize.Value,screen.x*.85f,true,&frame);
            const int endCommand=background->CmdBuffer.Size;
            background->PopClipRect();
            for(int cmd=firstCommand;cmd<endCommand&&cmd<background->CmdBuffer.Size;++cmd)
                background->CmdBuffer[cmd].ClipRect={0,0,screen.x,screen.y};
        }
    }
    const bool requiredCredits=state->provider=="Spicy Lyrics"||state->provider=="Musixmatch";
    if(!(Outputs.Value&4))
    {
        if(requiredCredits||Credits.Value)
        {
            ImGui::SetNextWindowPos({10,screen.y-12},ImGuiCond_Always,{0,1});
            ImGui::SetNextWindowSize({(std::min)(600.f,screen.x-20),0},ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(.3f);
            if(ImGui::Begin("Karaoke credits",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings
                |(F::Menu.m_bIsOpen?0:ImGuiWindowFlags_NoInputs)))
            {
                for(const auto& credit:state->credits)
                {
                    ImGui::TextWrapped("%s",credit.name.c_str());
                    if(ImGui::IsItemHovered()&&ImGui::IsMouseClicked(0))OpenCredit(credit.url);
                }
                if(!state->copyright.empty())ImGui::TextWrapped("%s",state->copyright.c_str());
            }
            ImGui::End();
        }
        return;
    }
    ::Karaoke::GlassStyle glass;
    glass.mode=Glass.Value;glass.blur=Blur.Value;glass.refraction=Refraction.Value;glass.zoom=Zoom.Value;glass.chroma=Chromatic.Value;
    glass.frost=Frost.Value;glass.edge=EdgeGlow.Value;glass.radius=Rounding.Value;glass.opacity=PanelOpacity.Value;
    glass.tint={PanelColor.Value.r/255.f,PanelColor.Value.g/255.f,PanelColor.Value.b/255.f,PanelColor.Value.a/255.f};
    if(glass.mode)m_glass.Capture(device,glass);
    else if(m_glass.Ready())m_glass.Release();
    const float width=std::clamp(HudWidth.Value,260.f,(std::max)(260.f,screen.x-24));
    ImGui::SetNextWindowPos({std::clamp(HudX.Value*screen.x,0.f,(std::max)(0.f,screen.x-width)),std::clamp(HudY.Value*screen.y,0.f,screen.y-100)},ImGuiCond_Always);
    ImGui::SetNextWindowSize({width,0},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16,14));ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,Rounding.Value);
    const int flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings
        |ImGuiWindowFlags_NoMove|(F::Menu.m_bIsOpen?0:ImGuiWindowFlags_NoInputs);
    if(ImGui::Begin("Karaoke##SixBox",nullptr,flags))
    {
        auto* draw=ImGui::GetWindowDrawList();const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
        if(glass.mode&&m_glass.Ready())m_glass.Draw(draw,pos,size,glass);
        else draw->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},Color(PanelColor.Value,PanelOpacity.Value),Rounding.Value);
        draw->AddRect(pos,{pos.x+size.x,pos.y+size.y},Color(Highlight.Value,.15f),Rounding.Value);
        ImGui::PushFont(F::Render.FontRegular?F::Render.FontRegular:font,15.f);
        const auto header=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##MoveKaraoke",{width-32,20});
        draw->AddText(F::Render.FontRegular?F::Render.FontRegular:font,13,header,Color(TextColor.Value,.8f),state->source.empty()?"Karaoke":state->source.c_str());
        if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0))
        {
            HudX.Value=std::clamp((pos.x+ImGui::GetIO().MouseDelta.x)/screen.x,0.f,1.f);HudX[DEFAULT_BIND]=HudX.Value;
            HudY.Value=std::clamp((pos.y+ImGui::GetIO().MouseDelta.y)/screen.y,0.f,1.f);HudY[DEFAULT_BIND]=HudY.Value;
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width-32);
        ImGui::TextUnformatted(state->title.empty()?"Waiting for music":state->title.c_str());
        if(!state->artist.empty())ImGui::TextDisabled("%s",state->artist.c_str());
        auto notice=[&](const ::Karaoke::ErrorNotice& value,const char* compact)
        {
            if(value.text.empty())return;
            const float alpha=value.Alpha(noticeTime);
            if(alpha>0)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*alpha);
                ImGui::TextWrapped("%s",value.text.c_str());ImGui::PopStyleVar();
            }
            else
            {
                ImGui::TextDisabled("%s",compact);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",value.text.c_str());
            }
        };
        notice(m_errorNotice,state->error=="Looking up lyrics..."?"Looking up lyrics...":"Lyrics unavailable");
        notice(m_controlNotice,"Player control unavailable");
        if(glass.mode&&!m_glass.Ready())ImGui::TextDisabled("Glass unavailable on this render target");
        if(Controls.Value&&!state->title.empty())
        {
            double mediaPosition=::Karaoke::Position(*state,GetTickCount64());
            const auto bar=ImGui::GetCursorScreenPos();const float barWidth=width-32;
            ImGui::BeginDisabled(!state->canSeek||state->duration<=0);
            ImGui::InvisibleButton("##SeekKaraoke",{barWidth,16});
            if(ImGui::IsItemActivated())
            {
                m_seekIdentity=state->identity;m_seekDragging=true;
            }
            if(!F::Menu.m_bIsOpen||!state->canSeek||state->duration<=0||m_seekIdentity!=state->identity)m_seekDragging=false;
            const bool activeSeek=ImGui::IsItemActive(),released=ImGui::IsItemDeactivated();
            if(m_seekDragging&&(activeSeek||released))
                mediaPosition=std::clamp((ImGui::GetMousePos().x-bar.x)/barWidth,0.f,1.f)*state->duration;
            const float ratio=state->duration>0?float(mediaPosition/state->duration):0;
            draw->AddRectFilled({bar.x,bar.y+6},{bar.x+barWidth,bar.y+10},IM_COL32(60,64,78,180),2);
            draw->AddRectFilled({bar.x,bar.y+6},{bar.x+barWidth*std::clamp(ratio,0.f,1.f),bar.y+10},Color(Highlight.Value),2);
            if(ImGui::IsItemHovered())ImGui::SetTooltip(state->canSeek?"Click or drag to seek":"This player does not support seeking");
            if(released&&m_seekDragging&&!ImGui::IsMouseDown(0))
            {
                char seconds[48];std::snprintf(seconds,sizeof(seconds),"%.3f",mediaPosition);
                CommandPlayer("seek",seconds,*state);
            }
            if(released||!activeSeek)m_seekDragging=false;
            ImGui::EndDisabled();
            char timing[64];const int now=int(mediaPosition),total=int(state->duration);
            std::snprintf(timing,sizeof(timing),"%d:%02d / %d:%02d",now/60,now%60,total/60,total%60);ImGui::TextDisabled("%s",timing);
            const char* repeat=state->repeat=="Track"?"Track":state->repeat=="Playlist"?"List":"Off";
            const float repeatWidth=ImGui::CalcTextSize(repeat).x+ImGui::GetStyle().FramePadding.x*2;
            const float controlWidth=std::clamp((width-32-repeatWidth-3*ImGui::GetStyle().ItemSpacing.x)/3,24.f,44.f);
            auto button=[&](const char* label,const char* tooltip,bool enabled,const char* action)
            {
                ImGui::BeginDisabled(!enabled);
                if(F::Render.IconFont)ImGui::PushFont(F::Render.IconFont,21.f);
                const bool clicked=ImGui::Button(label,{controlWidth,30});
                if(F::Render.IconFont)ImGui::PopFont();
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",tooltip);
                if(clicked&&enabled)CommandPlayer(action,"",*state);
                ImGui::EndDisabled();
            };
            button(ICON_MD_SKIP_PREVIOUS,"Previous",state->canPrevious,"previous");ImGui::SameLine();
            button(state->playing?ICON_MD_PAUSE:ICON_MD_PLAY_ARROW,state->playing?"Pause":"Play",state->canPlay,state->playing?"pause":"play");ImGui::SameLine();
            button(ICON_MD_SKIP_NEXT,"Next",state->canNext,"next");ImGui::SameLine();
            ImGui::BeginDisabled(!state->canRepeat);
            if(ImGui::Button(repeat,{repeatWidth,30}))CommandPlayer("repeat",state->repeat=="None"?"Track":state->repeat=="Track"?"Playlist":"None",*state);
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Repeat: off / track / playlist. Availability depends on the player.");
            ImGui::EndDisabled();
        }
        if(Visualizer.Value&&(VisualizerMode.Value==0||state->lines.empty()))
        {
            ImGui::Dummy({0,8});const auto visualPos=ImGui::GetCursorScreenPos();
            ::Karaoke::AudioPacket audio;{std::lock_guard lock(m_mutex);audio=m_audio;}
            uint64_t identity=0;std::from_chars(state->identity.data(),state->identity.data()+(std::min)(size_t(16),state->identity.size()),identity,16);
            DrawVisualizer(draw,visualPos,{width-32,VisualizerHeight.Value},audio,identity);
            ImGui::Dummy({width-32,VisualizerHeight.Value});
            const bool valid=::Karaoke::ValidAudio(audio,WallTime(),identity);
            if(!valid||!(audio.flags&1))
            {
                const char* status=!state->playing?"Music paused":!valid?"Waiting for isolated music audio":
                    audio.flags==4?"Music tab cannot be isolated":audio.flags==8?"Audio backend unavailable":audio.flags==16?"Music capture unavailable":"Waiting for music stream";
                ImGui::TextDisabled("%s",status);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Requires parec/pactl and python3-numpy. Only the selected player's audio is used. A browser must label its music tab's individual audio stream. Mixed system/browser capture is never used.");
            }
        }
        if(HudLyrics.Value&&!state->lines.empty())
        {
            if(state->synced)
            {
                ImGui::Dummy({0,8});const auto lyric=ImGui::GetCursorScreenPos();
                const float height=130;draw->PushClipRect(lyric,{lyric.x+width-32,lyric.y+height},true);
                drawLyrics(draw,{lyric.x+(width-32)*.5f,lyric.y+height*.5f-10},(std::min)(FontSize.Value,26.f),width-42,false,nullptr);
                draw->PopClipRect();ImGui::Dummy({width-32,height});
            }
            else
            {
                ImGui::TextDisabled(state->duration>0&&state->positionKnown?"Estimated auto-scroll — no timestamps":"Plain lyrics — scroll manually");
                if(ImGui::BeginChild("##PlainKaraoke",{0,130}))
                {
                    const bool textMaterial=textGlass.mode&&m_textGlass.BeginText(ImGui::GetWindowDrawList(),textGlass,false);
                    ImGuiListClipper clipper;clipper.Begin(int(state->lines.size()));
                    while(clipper.Step())for(int line=clipper.DisplayStart;line<clipper.DisplayEnd;++line)ImGui::TextUnformatted(state->lines[line].text.c_str());
                    if(textMaterial)m_textGlass.EndText(ImGui::GetWindowDrawList());
                    if(state->duration>0&&state->positionKnown)
                        ImGui::SetScrollY(ImGui::GetScrollMaxY()*(state->lines.size()>1?std::clamp(m_scroll/float(state->lines.size()-1),0.f,1.f):0.f));
                }
                ImGui::EndChild();
            }
        }
        if(state->instrumental)ImGui::TextDisabled("Instrumental");
        if(Credits.Value||requiredCredits)
            for(size_t i=0;i<state->credits.size();++i)
            {
                ImGui::PushID(int(i));const auto& credit=state->credits[i];
                ImGui::TextDisabled("%s",credit.name.c_str());
                if(ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s",credit.url.c_str());
                    if(ImGui::IsMouseClicked(0))OpenCredit(credit.url);
                }
                ImGui::PopID();
            }
        if(!state->copyright.empty())ImGui::TextWrapped("%s",state->copyright.c_str());
        ImGui::PopTextWrapPos();ImGui::PopFont();
    }
    ImGui::End();ImGui::PopStyleVar(2);
}
#endif
