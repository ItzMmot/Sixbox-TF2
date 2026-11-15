#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Karaoke
{
    // Fixed binary IPC: Python struct '<IIQQd262f', never PCM or mixed system audio.
    struct AudioPacket
    {
        uint32_t magic=0,flags=0;
        uint64_t sequence=0,identity=0;
        double sampled=0;
        float rms=0,peak=0,bass=0,snare=0,treble=0,onset=0;
        std::array<float,64> bands{};
        std::array<float,96> left{},right{};
    };
    static_assert(sizeof(AudioPacket)==1080);
    inline bool ValidAudio(const AudioPacket& audio,double now,uint64_t identity)
    {
        if(audio.magic!=0x31414236||audio.identity!=identity||!std::isfinite(audio.sampled)||now-audio.sampled<-.1||now-audio.sampled>.35)return false;
        const float scalar[]={audio.rms,audio.peak,audio.bass,audio.snare,audio.treble,audio.onset};
        for(float value:scalar)if(!std::isfinite(value)||value<0||value>1.01f)return false;
        for(float value:audio.bands)if(!std::isfinite(value)||value<0||value>1.01f)return false;
        for(float value:audio.left)if(!std::isfinite(value)||std::abs(value)>1.01f)return false;
        for(float value:audio.right)if(!std::isfinite(value)||std::abs(value)>1.01f)return false;
        return true;
    }
    struct ErrorNotice
    {
        std::string text,identity;
        double appeared=0;
        void Update(const std::string& error,const std::string& track,double now)
        {
            if(error!=text||track!=identity){text=error;identity=track;appeared=now;}
        }
        float Alpha(double now) const {return text.empty()?0.f:float(std::clamp(6.-(now-appeared),0.,1.));}
    };
    inline float Envelope(float previous,float target,float dt,float attack,float release)
    {
        dt=std::clamp(dt,0.f,.1f);
        return previous+(target-previous)*(1-std::exp(-dt/(std::max)(.001f,target>previous?attack:release)));
    }
    struct ScreenPoint {float x=0,y=0;bool visible=false;};
    inline ScreenPoint Project(const float* matrix,float x,float y,float z,float width,float height)
    {
        const float w=matrix[12]*x+matrix[13]*y+matrix[14]*z+matrix[15];
        if(!std::isfinite(w)||w<=.01f||width<=0||height<=0)return {};
        const float sx=width*.5f+(matrix[0]*x+matrix[1]*y+matrix[2]*z+matrix[3])/w*width*.5f;
        const float sy=height*.5f-(matrix[4]*x+matrix[5]*y+matrix[6]*z+matrix[7])/w*height*.5f;
        return {sx,sy,std::isfinite(sx)&&std::isfinite(sy)};
    }
    struct Segment {std::string text;double start=0,end=0;};
    struct Line {std::string text;double start=0,end=0;std::vector<Segment> segments;};
    struct Credit {std::string name,url;};
    struct State
    {
        std::string title,artist,album,source,bus,track,identity,repeat="None",lyricsRevision,error,controlError,provider,copyright;
        std::vector<Line> lines;
        std::vector<Credit> credits;
        double position=0,duration=0,rate=1;
        uint64_t received=0,ack=0;
        bool positionKnown=true,playing=false,synced=false,instrumental=false,canPlay=false,canNext=false,canPrevious=false,canSeek=false,canRepeat=false;
    };
    inline double Position(const State& state,uint64_t now)
    {
        const double elapsed=now>=state.received?(std::min)(double(now-state.received)*.001,3.):0.;
        const double value=state.position+(state.playing&&state.positionKnown?elapsed*state.rate:0.);
        return std::clamp(value,0.,state.duration>0?state.duration:1e8);
    }
    inline double TrackProgress(const State& state,double position)
    {
        if(!state.positionKnown||!std::isfinite(position)||!std::isfinite(state.duration)||state.duration<=0)return 0;
        return std::clamp(position/state.duration,0.,1.);
    }
    inline double PlainLinePosition(const State& state,double position)
    {return TrackProgress(state,position)*(state.lines.empty()?0:state.lines.size()-1);}
    inline double RetirementTime(const State& state,size_t next)
    {
        if(next>=state.lines.size())return state.duration;
        if(state.synced)return state.lines[next].start;
        return state.lines.size()>1?state.duration*double(next)/double(state.lines.size()-1):0;
    }
    inline int ActiveLine(const State& state,double position)
    {
        if(state.lines.empty()) return -1;
        if(!state.synced) return int(PlainLinePosition(state,position));
        const auto it=std::upper_bound(state.lines.begin(),state.lines.end(),position,
            [](double value,const Line& line){return value<line.start;});
        return it==state.lines.begin()?-1:int(it-state.lines.begin()-1);
    }
    inline float Progress(double position,double start,double end)
    {
        if(!std::isfinite(position)||!std::isfinite(start)||!std::isfinite(end)) return 0;
        return end>start?float(std::clamp((position-start)/(end-start),0.,1.)):float(position>=start);
    }
    struct OldLine {float alpha=1,shade=1,drop=0,angle=0;};
    inline OldLine Retire(int mode,double elapsed,float strength,float gravity,float seconds)
    {
        const float t=std::clamp(float(elapsed)/(std::max)(.1f,seconds),0.f,1.f);
        strength=std::clamp(strength,0.f,1.f);
        if(mode>=3) return {};
        if(mode==0) return {1.f,1.f-strength,0,0};
        if(mode==1) return {1.f-strength*t,1,0,0};
        return {1.f-t*t,1.f,gravity*t*t,.18f*t};
    }
}
