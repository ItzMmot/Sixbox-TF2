#pragma once
#ifndef TEXTMODE
#include "../../../SDK/SDK.h"
#include "KaraokeModel.h"
#include "KaraokeGlass.h"
#include "KaraokeText.h"
#include <atomic>
#include <array>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

class CKaraoke
{
    struct Request {bool enabled=false,visualizer=false;int provider=0,source=0;std::string spicy,musixmatch,path;};
    struct Command {uint64_t sequence=0;std::string action,value,bus,track,identity;double created=0;};
    ::Karaoke::Glass m_glass,m_textGlass;
    ::Karaoke::TextRenderer m_textRenderer;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_worker;
    std::atomic<bool> m_stop=false,m_shutdown=false,m_place=true;
    Request m_request;
    std::array<Command,8> m_commands;
    unsigned m_head=0,m_count=0;
    uint64_t m_sequence=0;
    std::shared_ptr<const Karaoke::State> m_state;
    ::Karaoke::AudioPacket m_audio;
    ::Karaoke::ErrorNotice m_errorNotice,m_controlNotice;
    struct Spark {float x=0,y=0,vx=0,vy=0,life=0,total=1;};
    std::array<Spark,96> m_sparks{};
    std::array<float,64> m_bands{},m_peaks{};
    std::array<float,96> m_waveLeft{},m_waveRight{};
    uint64_t m_audioSequence=0,m_audioIdentity=0;
    float m_beat=0,m_bass=0,m_snare=0,m_treble=0,m_previousOnset=0;
    double m_lastBurst=0;
    void DrawVisualizer(ImDrawList* draw,ImVec2 pos,ImVec2 size,const ::Karaoke::AudioPacket& audio,uint64_t identity);
    void Worker();
    void CommandPlayer(const char* action,const std::string& value,const Karaoke::State& state);
    struct WorldFrame {Vec3 origin,right,up,normal,camera;VMatrix matrix;float ground=-100000,radius=160,arcLimit=85;bool valid=false,stay=false;std::array<::Karaoke::WorldLine,11> lines{};};
    std::mutex m_worldMutex;
    WorldFrame m_world;
    Vec3 m_anchor,m_right,m_normal;
    float m_ground=-100000,m_radius=160,m_arcLimit=85;
    bool m_anchored=false;
    std::array<::Karaoke::StayAnchor,32> m_stayAnchors{};
    std::array<float,32> m_stayGround{},m_stayRadius{},m_stayArc{};
    std::string m_stayIdentity,m_stayRevision;
    uint64_t m_stayFrame=0;
    bool m_staying=false;
    bool m_seekDragging=false;
    std::string m_seekIdentity;
    float m_scroll=0;
    std::string m_scrollIdentity,m_scrollRevision;
public:
    CKaraoke();
    ~CKaraoke();
    void Draw(IDirect3DDevice9* device,bool canDraw);
    struct FrameLocks {std::unique_lock<std::recursive_mutex> hud,text;};
    auto LockFrame() {return FrameLocks{m_glass.LockFrame(),m_textGlass.LockFrame()};}
    void ResetText() {m_textRenderer.Invalidate();}
    void CacheWorld();
    void PlaceWorld() {m_place.store(true);}
    void ResetWorld();
    void ReleaseDevice();
    void Shutdown();
};
ADD_FEATURE(CKaraoke,Karaoke);
#endif
