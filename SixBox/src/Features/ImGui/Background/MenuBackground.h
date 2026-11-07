#pragma once
#include "../../../SDK/SDK.h"
#ifndef TEXTMODE
#include "BackgroundPolicy.h"
#include <ImGui/imgui.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class CMenuBackground
{
    struct Job
    {
        uint64_t generation=0;std::string path,base;
        MenuBackground::Processing processing{};HWND owner=nullptr;
        bool picker=false,reload=false;
    };
    struct Result
    {
        uint64_t generation=0;MenuBackground::Image image;
        std::string path,error;bool picker=false;
    };
    std::thread m_worker;
    std::mutex m_jobs;
    std::condition_variable m_wake;
    Job m_job;Result m_result;
    bool m_hasJob=false,m_hasResult=false;
    std::atomic<bool> m_stop=false,m_picker=false;
    std::atomic<HWND> m_dialog=nullptr;
    std::atomic<uint64_t> m_generation=0;
    std::recursive_mutex m_gpu;
    IDirect3DDevice9* m_device=nullptr;
    IDirect3DTexture9* m_texture=nullptr;
    MenuBackground::Image m_image;
    MenuBackground::Processing m_seenProcessing{};
    std::string m_seenPath,m_error;
    uint64_t m_due=0,m_uploadAfter=0;
    bool m_pending=false,m_reload=false,m_forceDecode=false,m_loading=false,m_wasEnabled=false,m_clear=false,m_uploadFailed=false;
    struct Style
    {
        MenuBackground::Placement placement{};
        Color_t tint{},top{},bottom{};
        float opacity=.85f,panels=.65f,dim=.25f,vignette=.25f,parallax=0;
        int area=0;bool enabled=false;
    } m_style;
    float m_parallaxX=0,m_parallaxY=0;
    void Worker();
    void Queue(Job job);
    bool Upload(IDirect3DDevice9* device);
    static UINT_PTR CALLBACK DialogHook(HWND dialog,UINT message,WPARAM,LPARAM parameter);
public:
    ~CMenuBackground();
    std::unique_lock<std::recursive_mutex> LockFrame() {return std::unique_lock<std::recursive_mutex>(m_gpu);}
    void Update(IDirect3DDevice9* device);
    void Draw(ImDrawList* draw,ImVec2 minimum,ImVec2 maximum,float alpha);
    ImU32 PanelColor(ImU32 color) const;
    void Pick(HWND owner);
    void Reload() {m_reload=true;}
    void Clear();
    bool Picking() const {return m_picker.load();}
    bool Loading() const {return m_loading;}
    const char* Error() const {return m_error.c_str();}
    int Width() const {return m_image.width;}
    int Height() const {return m_image.height;}
    bool Ready() const;
    int Area() const {return m_style.area;}
    void ReleaseDevice();
    void Shutdown();
};
ADD_FEATURE(CMenuBackground, MenuBackground);
#endif
