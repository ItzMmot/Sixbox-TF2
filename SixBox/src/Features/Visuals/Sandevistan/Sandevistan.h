#pragma once
#include "../../../SDK/SDK.h"
#ifndef TEXTMODE
#include "TrailPolicy.h"
#include <array>
#include <mutex>
class CSandevistan
{
    struct Event : Sandevistan::Burst {uint64_t id=0;};
    struct Command
    {
        int number=0,logical=0;
        uint64_t event=0;
        bool consumed=false;
    };
    struct Ghost
    {
        uint64_t event=0;
        float rank=0;
        int command=0,copies=0,boneCount=0,skin=0,body=0,hitbox=0;
        Vec3 origin{},angles{},velocity{},mins{},maxs{};
        const model_t* model=nullptr;
        std::array<matrix3x4,MAXSTUDIOBONES> bones{};
    };
    std::mutex m_mutex;
    std::array<Event,Sandevistan::MaxEvents> m_events{};
    std::array<Command,Sandevistan::MaxCommands> m_commands{};
    std::array<Ghost,Sandevistan::MaxGhosts> m_ghosts{},m_renderGhosts{};
    std::array<double,Sandevistan::MaxGhosts> m_renderBirth{};
    std::array<Vec3,Sandevistan::MaxGhosts> m_renderOffset{};
    Sandevistan::CameraMotion m_cameraMotion;
    Vec2 m_echoMotion{};
    int m_echoFrame=-1;
    bool m_worldPass=false;
    int m_renderCount=0;
    double m_renderNow=0;
    Vec3 m_renderCamera{};
    struct Diagnostics
    {
        uint64_t events=0,captured=0,templateCalls=0,templateHits=0,drawn=0,nearCamera=0,modelMismatch=0,missingHook=0;
    } m_diagnostics;
    double m_nextReport=0;
    uint64_t m_reportedEvents=0,m_reportedDraws=0,m_reportedTemplates=0;
    void ReportDiagnostics();
    CHandle<CTFPlayer> m_owner;
    int m_class=0,m_modelIndex=0,m_nextGhost=0,m_count=0,m_nextEvent=0;
    uint64_t m_serial=0,m_current=0;
    IMaterial* m_materials[3][2]{};
    struct DrawPacket
    {
        matrix3x4 transform{};
        ModelRenderInfo_t info{};
        DrawModelState_t state{};
    };
    struct ViewDrawPacket : DrawPacket {std::array<matrix3x4,MAXSTUDIOBONES> bones{};};
    std::array<DrawPacket,Sandevistan::MaxGhosts> m_worldDraws{};
    std::array<ViewDrawPacket,64> m_viewDraws{};
    size_t m_nextViewDraw=0;
    Event* FindEvent(uint64_t id);
    bool ValidOwner(CTFPlayer* local);
    void ClearLocked();
    bool Latest(double now,double& born);
public:
    static bool Rendering();
    void Reset();
    void MarkCommand(CTFPlayer* local,CUserCmd* command,int kind,int remaining);
    void CapturePrediction(CTFPlayer* local,CUserCmd* command,float simulationTime);
    void InitializeMaterials();
    void ShutdownMaterials();
    void RenderWorld(const CViewSetup* setup);
    bool RenderBody(IVModelRender* render,const DrawModelState_t& state,const ModelRenderInfo_t& info,
                    matrix3x4* bones,DrawModelExecuteFn original);
    void RenderViewmodel(IVModelRender* render,const DrawModelState_t& state,const ModelRenderInfo_t& info,
                         matrix3x4* bones,DrawModelExecuteFn original);
    void DrawHUD();
};
ADD_FEATURE(CSandevistan,Sandevistan);
#endif
