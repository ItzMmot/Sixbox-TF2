// tests/karaoke_test.cpp — player clock, timed highlighting, retirement and perspective.
#include "../SixBox/src/Features/Visuals/Karaoke/KaraokeModel.h"
#include <cassert>
#include <limits>
int main()
{
    using namespace Karaoke;
    ErrorNotice notice;notice.Update("provider failure","track",100);
    assert(notice.Alpha(104)==1&&notice.Alpha(105.5)==.5f&&notice.Alpha(106)==0);
    notice.Update("provider failure","track",108);assert(notice.Alpha(108)==0); // polling doesn't reopen it
    notice.Update("new error","track",108);assert(notice.Alpha(108)==1);
    notice.Update("new error","new track",120);assert(notice.Alpha(120)==1);
    notice.Update("","new track",121);assert(notice.Alpha(121)==0);
    AudioPacket audio;audio.magic=0x31414236;audio.identity=7;audio.sampled=100;audio.flags=1;
    assert(ValidAudio(audio,100.1,7)&&!ValidAudio(audio,100.4,7)&&!ValidAudio(audio,100.1,8));
    audio.bands[3]=std::numeric_limits<float>::quiet_NaN();assert(!ValidAudio(audio,100.1,7));
    float fast=Envelope(0,1,1.f/60,.004f,.05f);assert(fast>.98f);
    assert(Envelope(fast,0,1.f/60,.004f,.05f)<fast);
    State state;state.received=1000;state.position=10;state.duration=20;state.playing=true;
    assert(Position(state,2000)==11);assert(Position(state,900)==10);assert(Position(state,100000)==13);
    state.playing=false;assert(Position(state,2000)==10);
    state.playing=true;state.positionKnown=false;assert(Position(state,2000)==10);
    state.positionKnown=true;state.position=19.5;assert(Position(state,2000)==20);
    state.lines={{"one",1,2,{}},{"two",3,4,{}}};state.synced=true;
    assert(ActiveLine(state,0)==-1&&ActiveLine(state,1)==0&&ActiveLine(state,3)==1);
    state.synced=false;assert(ActiveLine(state,4)==0);
    state.duration=20;assert(PlainLinePosition(state,10)==.5&&TrackProgress(state,10)==.5);
    assert(ActiveLine(state,20)==1&&RetirementTime(state,1)==20);
    assert(TrackProgress(state,-1)==0&&TrackProgress(state,30)==1);
    state.positionKnown=false;assert(PlainLinePosition(state,10)==0);state.positionKnown=true;
    state.duration=0;assert(PlainLinePosition(state,10)==0);state.duration=20;
    assert(Progress(2,1,3)==.5f&&Progress(0,1,3)==0&&Progress(4,1,3)==1);
    assert(Progress(1,1,1)==1&&Progress(std::numeric_limits<double>::quiet_NaN(),1,2)==0);
    auto old=Retire(0,1,.8f,100,1);assert(old.shade<.21f&&old.alpha==1);
    old=Retire(1,1,.8f,100,1);assert(old.alpha<.21f&&old.drop==0);
    old=Retire(2,.5,1,120,1);assert(old.drop==30&&old.alpha==.75f&&old.angle>0);
    old=Retire(2,100,1,120,1);assert(old.alpha==0&&old.drop==120);
    float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0};
    auto a=Project(matrix,1,0,2,800,600),b=Project(matrix,1,0,4,800,600);
    assert(a.visible&&b.visible&&a.x==600&&b.x==500); // Perspective, not a distance-scaled 2D label.
    assert(!Project(matrix,0,0,-2,800,600).visible);
    assert(!Project(matrix,0,0,0,800,600).visible);
}
