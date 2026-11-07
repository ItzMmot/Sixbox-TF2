#include "MenuBackground.h"
#ifndef TEXTMODE
#include "BackgroundImageIO.h"
#include "../../Configs/Configs.h"
#include "../../Binds/Binds.h"
#include <commdlg.h>
#include <filesystem>
#include <chrono>

#pragma comment(lib,"comdlg32.lib")

namespace
{
    std::wstring Wide(const std::string& s)
    {
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
        if(length<=0) return {};
        std::wstring out(size_t(length),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),length);return out;
    }
    std::string Utf8(const wchar_t* s)
    {
        const int length=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);
        if(length<=1) return {};
        std::string out(size_t(length),0);WideCharToMultiByte(CP_UTF8,0,s,-1,out.data(),length,nullptr,nullptr);out.pop_back();return out;
    }
    std::filesystem::path Resolve(const std::string& name,const std::string& base)
    {
        auto path=Wide(name);
        if(!path.empty()&&path.front()==L'/') {path=L"Z:"+path;std::replace(path.begin(),path.end(),L'/',L'\\');}
        std::filesystem::path out(path);
        return out.is_relative()?std::filesystem::path(Wide(base))/out:out;
    }
    template<class T> void Set(ConfigVar<T>& setting,const T& value) {setting.Value=value;setting[DEFAULT_BIND]=value;}
}
CMenuBackground::~CMenuBackground() {Shutdown();}
void CMenuBackground::Queue(Job job)
{
    if(m_stop.load()) return;
    std::lock_guard lock(m_jobs);
    if(m_stop.load()) return;
    m_job=std::move(job);m_hasJob=true;
    if(!m_worker.joinable())
    {
        try {m_worker=std::thread(&CMenuBackground::Worker,this);}
        catch(const std::exception&) {m_hasJob=false;m_picker.store(false);m_loading=false;m_error="Could not start the image worker.";return;}
    }
    m_wake.notify_one();
}
UINT_PTR CALLBACK CMenuBackground::DialogHook(HWND dialog,UINT message,WPARAM,LPARAM parameter)
{
    if(message==WM_INITDIALOG)
    {
        auto* options=reinterpret_cast<OPENFILENAMEW*>(parameter);
        auto* self=reinterpret_cast<CMenuBackground*>(options->lCustData);
        const HWND window=GetParent(dialog)?GetParent(dialog):dialog;
        self->m_dialog.store(window);
        if(self->m_stop.load()) PostMessageW(window,WM_CLOSE,0,0);
    }
    return 0;
}
void CMenuBackground::Worker()
{
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    MenuBackground::Image raw;std::filesystem::path cached;int cachedLongest=0;
    for(;;)
    {
        Job job;
        {
            std::unique_lock lock(m_jobs);m_wake.wait(lock,[&]{return m_stop.load()||m_hasJob;});
            if(m_stop.load()) break;
            job=std::move(m_job);m_hasJob=false;
        }
        Result result;result.generation=job.generation;result.picker=job.picker;
        try
        {
            if(job.picker)
            {
                std::array<wchar_t,32768> file{};
                OPENFILENAMEW options{};options.lStructSize=sizeof(options);options.hwndOwner=job.owner;
                options.lpstrFilter=L"Images (PNG, JPEG, BMP, GIF, TIFF)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All files\0*.*\0";
                options.lpstrFile=file.data();options.nMaxFile=DWORD(file.size());options.lpstrTitle=L"Choose ClickGUI background";
                options.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_ENABLEHOOK;
                options.lpfnHook=DialogHook;options.lCustData=reinterpret_cast<LPARAM>(this);
                if(GetOpenFileNameW(&options)) result.path=Utf8(file.data());
                else if(CommDlgExtendedError()) result.error="The image picker could not be opened. Enter a file path instead.";
                m_dialog.store(nullptr);
            }
            else
            {
                const auto path=Resolve(job.path,job.base);const int longest=job.processing.blur>.01f?1024:MenuBackground::MaxDimension;
                std::error_code ec;const auto bytes=std::filesystem::file_size(path,ec);
                if(!ec&&bytes>64ull*1024*1024) result.error="Image file is too large (maximum 64 MB).";
                else if(path.empty()||job.path.empty()) result.error="Choose an image file first.";
                else if(job.reload||path!=cached||longest!=cachedLongest||!MenuBackground::ValidImage(raw))
                {
                    MenuBackground::Image decoded;
                    if(MenuBackground::Decode(path,decoded,result.error,longest)) {raw=std::move(decoded);cached=path;cachedLongest=longest;}
                }
                auto cancelled=[&]{return m_stop.load()||m_generation.load()!=job.generation;};
                if(result.error.empty()&&!cancelled()) result.image=MenuBackground::Process(raw,job.processing,cancelled);
            }
        }
        catch(const std::exception&) {result.error="The image could not be processed.";m_dialog.store(nullptr);}
        if(m_stop.load()) break;
        if(result.generation==m_generation.load())
        {std::lock_guard lock(m_jobs);m_result=std::move(result);m_hasResult=true;}
        else if(job.picker) m_picker.store(false);
    }
    if(SUCCEEDED(com)) CoUninitialize();
}
void CMenuBackground::Pick(HWND owner)
{
    if(m_stop.load()||m_picker.exchange(true)) return;
    Job job;job.picker=true;job.owner=owner;job.generation=++m_generation;
    Queue(std::move(job));
}
void CMenuBackground::Clear()
{
    std::scoped_lock settings(F::Binds.m_mMutex);
    Set(Vars::Menu::BackgroundImage::Path,std::string{});Set(Vars::Menu::BackgroundImage::Enabled,false);
    ++m_generation;m_pending=false;m_loading=false;m_error.clear();m_clear=true;
    if(const HWND dialog=m_dialog.load()) PostMessageW(dialog,WM_CLOSE,0,0);
}
bool CMenuBackground::Ready() const
{return m_style.enabled&&!m_seenPath.empty()&&m_texture&&!m_image.bgra.empty();}
ImU32 CMenuBackground::PanelColor(ImU32 color) const
{
    if(!Ready()) return color;
    const float factor=MenuBackground::Finite(m_style.panels,.65f,0,1);
    return (color&0x00ffffffu)|(ImU32(float(color>>24)*factor)<<24);
}
bool CMenuBackground::Upload(IDirect3DDevice9* device)
{
    if(!device||!MenuBackground::ValidImage(m_image)||FAILED(device->TestCooperativeLevel())) return false;
    Microsoft::WRL::ComPtr<IDirect3DTexture9> fresh;
    bool dynamic=false;
    if(FAILED(device->CreateTexture(m_image.width,m_image.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&fresh,nullptr)))
    {
        dynamic=true;
        if(FAILED(device->CreateTexture(m_image.width,m_image.height,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&fresh,nullptr))) return false;
    }
    D3DLOCKED_RECT lock{};
    if(FAILED(fresh->LockRect(0,&lock,nullptr,dynamic?D3DLOCK_DISCARD:0))) return false;
    for(int y=0;y<m_image.height;++y) std::memcpy(static_cast<uint8_t*>(lock.pBits)+size_t(y)*lock.Pitch,
        m_image.bgra.data()+size_t(y)*m_image.width*4,size_t(m_image.width)*4);
    fresh->UnlockRect(0);if(m_texture)m_texture->Release();m_texture=fresh.Detach();return true;
}
void CMenuBackground::ReleaseDevice()
{
    std::lock_guard lock(m_gpu);
    if(m_texture) {m_texture->Release();m_texture=nullptr;}
    m_device=nullptr;m_uploadAfter=0;
}
void CMenuBackground::Shutdown()
{
    std::thread worker;
    {
        std::lock_guard lock(m_jobs);
        m_stop.store(true);++m_generation;worker=std::move(m_worker);
    }
    if(const HWND dialog=m_dialog.load()) PostMessageW(dialog,WM_CLOSE,0,0);
    m_wake.notify_all();if(worker.joinable()) worker.join();
    ReleaseDevice();
}
void CMenuBackground::Update(IDirect3DDevice9* device)
{
    if(m_stop.load()||G::Unload) return;
    if(device!=m_device) {ReleaseDevice();m_device=device;}
    if(m_clear) {ReleaseDevice();m_device=device;m_image={};m_seenPath.clear();m_clear=false;}
    Result ready;bool received=false;
    {std::lock_guard lock(m_jobs);if(m_hasResult) {ready=std::move(m_result);m_hasResult=false;received=true;}}
    if(received&&ready.picker) m_picker.store(false);
    if(received&&ready.generation==m_generation.load())
    {
        if(ready.picker)
        {
            m_picker.store(false);
            std::scoped_lock settings(F::Binds.m_mMutex);
            if(m_loading) m_reload=true;
            if(!ready.path.empty()) {Set(Vars::Menu::BackgroundImage::Path,ready.path);Set(Vars::Menu::BackgroundImage::Enabled,true);m_reload=true;}
            if(!ready.error.empty()) m_error=std::move(ready.error);
        }
        else
        {
            m_loading=false;m_uploadFailed=false;m_error=std::move(ready.error);
            if(MenuBackground::ValidImage(ready.image))
            {
                m_image=std::move(ready.image);m_uploadAfter=0;
                if(!Upload(device)) {if(m_texture){m_texture->Release();m_texture=nullptr;}m_error="Texture upload failed; waiting for the graphics device.";m_uploadFailed=true;m_uploadAfter=GetTickCount64()+500;}
            }
        }
    }
    const uint64_t now=GetTickCount64();
    {
        std::scoped_lock settings(F::Binds.m_mMutex);
        using namespace Vars::Menu::BackgroundImage;
        const auto process=MenuBackground::Processing::Clean({Blur.Value,Brightness.Value,Contrast.Value,Saturation.Value});
        const bool enabled=Enabled.Value;
        m_style={MenuBackground::Placement{MenuBackground::Fit(std::clamp(Fit.Value,0,3)),Scale.Value,OffsetX.Value,OffsetY.Value,Rotation.Value,FlipX.Value,FlipY.Value},
            Tint.Value,GradientTop.Value,GradientBottom.Value,Opacity.Value,Panels.Value,Dim.Value,Vignette.Value,Parallax.Value,std::clamp(Vars::Menu::BackgroundImage::Area.Value,0,2),enabled};
        if(Path.Value!=m_seenPath||process!=m_seenProcessing||enabled!=m_wasEnabled||m_reload)
        {
            ++m_generation;m_seenPath=Path.Value;m_seenProcessing=process;m_wasEnabled=enabled;
            m_forceDecode=m_forceDecode||m_reload;m_reload=false;
            m_pending=enabled&&!Path.Value.empty();m_due=now+120;m_loading=m_pending;
        }
    }
    if(m_pending&&now>=m_due&&!m_picker.load())
    {
        Job job;job.generation=m_generation.load();job.path=m_seenPath;job.base=F::Configs.m_sCorePath;
        job.processing=m_seenProcessing;job.reload=m_forceDecode;m_forceDecode=false;
        Queue(std::move(job));m_pending=false;
    }
    if(!m_texture&&!m_image.bgra.empty()&&m_style.enabled&&now>=m_uploadAfter)
    {if(!Upload(device))m_uploadAfter=now+500;else if(m_uploadFailed) {m_uploadFailed=false;m_error.clear();}}
}
void CMenuBackground::Draw(ImDrawList* draw,ImVec2 minimum,ImVec2 maximum,float alpha)
{
    if(!Ready()||!draw) return;
    const float width=maximum.x-minimum.x,height=maximum.y-minimum.y;
    if(width<=0||height<=0) return;
    const auto mouse=ImGui::GetIO().MousePos;
    const float px=std::clamp((mouse.x-(minimum.x+maximum.x)*.5f)/(width*.5f),-1.f,1.f);
    const float py=std::clamp((mouse.y-(minimum.y+maximum.y)*.5f)/(height*.5f),-1.f,1.f);
    const float blend=1.f-std::exp(-std::clamp(ImGui::GetIO().DeltaTime,0.f,.1f)/.12f);
    m_parallaxX+=(px-m_parallaxX)*blend;m_parallaxY+=(py-m_parallaxY)*blend;
    const float parallax=MenuBackground::Finite(m_style.parallax,0,0,40);
    auto placement=m_style.placement;
    placement.x+=m_parallaxX*parallax*2.f/width;placement.y+=m_parallaxY*parallax*2.f/height;
    const auto tint=m_style.tint;
    const float opacity=MenuBackground::Finite(m_style.opacity,.85f,0,1)*std::clamp(alpha,0.f,1.f);
    const ImU32 color=IM_COL32(tint.r,tint.g,tint.b,int(tint.a*opacity));
    draw->PushClipRect(minimum,maximum,true);
    MenuBackground::Layout(width,height,m_image.naturalWidth>0?m_image.naturalWidth:m_image.width,m_image.naturalHeight>0?m_image.naturalHeight:m_image.height,placement,[&](const MenuBackground::Quad& quad) {
        std::array<ImVec2,4> p,uv;
        for(int i=0;i<4;++i) {p[i]={minimum.x+quad.position[i].x,minimum.y+quad.position[i].y};uv[i]={quad.uv[i].x,quad.uv[i].y};}
        draw->AddImageQuad(reinterpret_cast<ImTextureID>(m_texture),p[0],p[1],p[2],p[3],uv[0],uv[1],uv[2],uv[3],color);
    });
    auto overlay=[&](Color_t c){return IM_COL32(c.r,c.g,c.b,int(c.a*alpha));};
    draw->AddRectFilledMultiColor(minimum,maximum,overlay(m_style.top),overlay(m_style.top),overlay(m_style.bottom),overlay(m_style.bottom));
    const int dim=int(MenuBackground::Finite(m_style.dim,.25f,0,1)*255*alpha);
    if(dim) draw->AddRectFilled(minimum,maximum,IM_COL32(14,16,22,dim));
    const float vignette=MenuBackground::Finite(m_style.vignette,.25f,0,1);
    if(vignette>0)
    {
        auto edge=[&](int x,int y) {const float dx=float(x)/8-1,dy=float(y)/8-1;
            return IM_COL32(12,14,18,int(std::clamp((dx*dx+dy*dy)*.5f,0.f,1.f)*vignette*255*alpha));};
        for(int y=0;y<16;++y) for(int x=0;x<16;++x)
        {
            const ImVec2 a{minimum.x+width*x/16,minimum.y+height*y/16},b{minimum.x+width*(x+1)/16,minimum.y+height*(y+1)/16};
            draw->AddRectFilledMultiColor(a,b,edge(x,y),edge(x+1,y),edge(x+1,y+1),edge(x,y+1));
        }
    }
    draw->PopClipRect();
}
#endif
