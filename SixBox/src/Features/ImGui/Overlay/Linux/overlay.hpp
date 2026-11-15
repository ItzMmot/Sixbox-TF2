#pragma once
#include "../../../../../include/ImGui/imgui.h"
#include "../../../../Hooks/Direct3DDevice9.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <cstring>
#include <optional>
#include "model_transport.hpp"
#include "mask_codec.hpp"

namespace LocalExtension
{
#pragma pack(push, 1)
struct Header
{
    char magic[8] = { 'S','X','O','V','0','0','0','2' };
    uint64_t sequence = 0, session = 0;
    int32_t width = 0, height = 0, x = 0, y = 0, clientWidth = 0, clientHeight = 0;
    uint32_t mask = 0, count = 0;
    uint32_t effects = 0;
    float glowStrength = .85f, glowRadius = 8.f, stencilThickness = 2.f;
    uint32_t stencilColor = 0xdcffffff;
};
struct Record
{
    uint32_t kind = 0, style = 0;
    float x = 0, y = 0, width = 0, height = 0, size = 0;
    uint32_t color = 0, background = 0;
    char text[192] = {};
};
struct Ack { char magic[8]; uint64_t session, sequence; uint32_t mask; };
#pragma pack(pop)
static_assert(sizeof(Header) == 76 && sizeof(Record) == 228 && sizeof(Ack) == 28);
struct Packet { Header header; std::array<Record, 512> records; };

class Output
{
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::atomic<bool> stop = false;
    std::atomic<int> status = 0;
    std::atomic<uint32_t> acknowledged = 0;
    std::atomic<ULONGLONG> lastAck = 0;
    Packet pending{}, drawing{};
    bool dirty = false;
    std::optional<ModelPixels> pendingModel;
    std::vector<unsigned char> recycledModel;
    uint64_t modelSequence = 0;
    std::atomic<bool> modelCaptureFailure = false, modelEncodingFailure = false;
    std::atomic<ULONGLONG> modelFailureAt = 0;
    uint64_t sequence = 0;
    const uint64_t session = (GetTickCount64() << 24) ^ GetCurrentProcessId();
    double lastPublish = 0;
    uint32_t lastMask = 0;

    static uint32_t Color(Color_t color, float alpha)
    {
        const auto a = uint32_t(std::clamp(float(color.a) * alpha, 0.f, 255.f));
        return uint32_t(color.r) | (uint32_t(color.g) << 8) | (uint32_t(color.b) << 16) | (a << 24);
    }
    static bool Write(const std::filesystem::path& path, const void* data, DWORD size)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        const bool success = WriteFile(file, data, size, &written, nullptr) && written == size;
        CloseHandle(file);
        return success;
    }
    void Run()
    {
        std::filesystem::path directory;
        try
        {
            wchar_t local[32768];
            const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, DWORD(std::size(local)));
            if (!count || count >= std::size(local)) throw std::runtime_error("LOCALAPPDATA unavailable");
            directory = std::filesystem::path(local) / L"SixBox" / L"local-render";
            std::filesystem::create_directories(directory);
            const auto frame = directory / L"frame.bin", temp = directory / L"frame.tmp", ackFile = directory / L"ack.bin";
            Packet packet{};
            ModelEncoder modelEncoder;
            const auto maskFile=directory/L"stencil.bin",maskTemp=directory/L"stencil.tmp";
            while (!stop)
            {
                bool publish;
                std::optional<ModelPixels> model;
                {
                    std::unique_lock lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(16), [&]{ return stop || dirty || pendingModel.has_value(); });
                    if (stop) break;
                    publish = dirty;
                    if (publish) { packet = pending; dirty = false; }
                    model=std::move(pendingModel);pendingModel.reset();
                }
                if (publish)
                {
                    const DWORD bytes = DWORD(sizeof(Header) + packet.header.count * sizeof(Record));
                    if (!Write(temp, &packet, bytes) || !MoveFileExW(temp.c_str(), frame.c_str(), MOVEFILE_REPLACE_EXISTING))
                    { status = 3; acknowledged = 0; continue; }
                }
                if (!(packet.header.mask & 4))
                {
                    DeleteFileW(maskFile.c_str());DeleteFileW(maskTemp.c_str());
                }
                else if (model)
                {
                    try
                    {
                        const auto encoded=modelEncoder.Encode(*model,session);
                        if (!Write(maskTemp,encoded.data(),DWORD(encoded.size())) || !MoveFileExW(maskTemp.c_str(),maskFile.c_str(),MOVEFILE_REPLACE_EXISTING))
                            throw std::runtime_error("Model image write failed");
                        modelEncodingFailure=false;
                    }
                    catch (...) { modelEncodingFailure=true;modelFailureAt=GetTickCount64(); }
                }
                if (model)
                {
                    std::lock_guard lock(mutex);
                    recycledModel=std::move(model->pixels);
                }
                HANDLE file = CreateFileW(ackFile.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                bool valid = false;
                if (file != INVALID_HANDLE_VALUE)
                {
                    Ack ack{}; DWORD read = 0; FILETIME modified{}, now{};
                    GetSystemTimeAsFileTime(&now);
                    const bool metadata = GetFileTime(file, nullptr, nullptr, &modified);
                    const auto value = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
                    const uint64_t age = value(now) >= value(modified) ? value(now) - value(modified) : UINT64_MAX;
                    valid = metadata && age < 2500000 && ReadFile(file, &ack, sizeof(ack), &read, nullptr)
                        && read == sizeof(ack) && !memcmp(ack.magic, "SXACK001", 8) && ack.session == session
                        && ack.sequence && ack.sequence <= packet.header.sequence && ack.mask == packet.header.mask;
                    if (valid) { acknowledged = ack.mask; lastAck = GetTickCount64(); status = 2; }
                    CloseHandle(file);
                }
                if (!valid) { acknowledged = 0; status = 1; }
            }
            std::error_code error;
            std::filesystem::remove(frame, error);
            std::filesystem::remove(temp, error);
            std::filesystem::remove(maskFile, error);
            std::filesystem::remove(maskTemp, error);
        }
        catch (...) { status = 3; acknowledged = 0; }
    }
    bool Append(Record record)
    {
        if (!(Vars::ESP::OverlayRender.Value & record.kind)) return false;
        if ((drawing.header.mask & record.kind) && drawing.header.count < drawing.records.size())
            drawing.records[drawing.header.count++] = record;
        // Explicitly selected elements never leak back into the game capture.
        return true;
    }
public:
    ~Output() { Stop(); }
    bool ModelsReady() const
    {
        const auto now=GetTickCount64();
        return !stop && status==2 && (acknowledged.load() & 4) && now-lastAck.load()<200
            && (!modelEncodingFailure || now-modelFailureAt.load()>=1000);
    }
    void CaptureError(bool failed) { modelCaptureFailure=failed; }
    std::vector<unsigned char> ModelBuffer(size_t bytes)
    {
        std::vector<unsigned char> result;
        { std::lock_guard lock(mutex); result=std::move(recycledModel); }
        result.resize(bytes);return result;
    }
    void Model(std::vector<unsigned char>&& pixels,unsigned width,unsigned height,uint64_t capturedAt)
    {
        if (stop) return;
        { std::lock_guard lock(mutex); pendingModel=ModelPixels{std::move(pixels),width,height,capturedAt,++modelSequence}; }
        wake.notify_one();
    }
    void Stop()
    {
        stop = true; wake.notify_all();
        if (worker.joinable()) worker.join();
        acknowledged = 0;
    }
    const char* Status() const
    {
        if(!GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version"))
            return "Overlay render is Linux/Proton only (selected elements hidden)";
        if ((Vars::ESP::OverlayRender.Value & 4) && (modelCaptureFailure || modelEncodingFailure))
            return "Stencil capture/PNG transfer failed (selected silhouettes hidden)";
        if (status == 3) return "Local renderer: file or worker error (selected elements hidden)";
        if (status == 2 && GetTickCount64() - lastAck.load() < 200) return "Local renderer: connected";
        return "Local renderer: waiting (selected elements hidden)";
    }
    void Begin()
    {
        drawing.header = {};
        drawing.header.session = session;
        drawing.header.effects = (Vars::ESP::OverlayBoxGlow.Value ? 1u : 0u) | (Vars::ESP::OverlayBoxStencil.Value ? 2u : 0u);
        const auto bounded = [](float value, float fallback, float low, float high)
        { return std::isfinite(value) ? std::clamp(value, low, high) : fallback; };
        drawing.header.glowStrength = bounded(Vars::ESP::OverlayBoxGlowStrength.Value / 100.f, .85f, 0.f, 2.f);
        drawing.header.glowRadius = bounded(Vars::ESP::OverlayBoxGlowRadius.Value, 8.f, 1.f, 24.f);
        drawing.header.stencilThickness = bounded(Vars::ESP::OverlayBoxStencilThickness.Value, 2.f, 1.f, 8.f);
        drawing.header.stencilColor = Color(Vars::ESP::OverlayBoxStencilColor.Value, 1.f);
        const unsigned mask = Vars::ESP::OverlayRender.Value & 7;
        if (stop || (!mask && !worker.joinable())) return;
        if (!worker.joinable())
        {
            try { status = 1; worker = std::thread([this] { Run(); }); }
            catch (...) { status = 3; return; }
        }
        // ESP coordinates come from W2S, whose dimensions can differ from
        // ImGui's client size (DPI, render scaling, windowed modes).
        const int sourceWidth = H::Draw.m_nScreenW, sourceHeight = H::Draw.m_nScreenH;
        RECT rect{}; POINT point{};
        HWND window = WndProc::hwWindow;
        if (!mask || !window || IsIconic(window) || GetForegroundWindow() != window || SDK::CleanScreenshot()
            || !GetClientRect(window, &rect) || !ClientToScreen(window, &point) || sourceWidth < 1 || sourceHeight < 1) return;
        drawing.header.width = sourceWidth; drawing.header.height = sourceHeight;
        drawing.header.x = point.x; drawing.header.y = point.y;
        drawing.header.clientWidth = rect.right; drawing.header.clientHeight = rect.bottom;
        drawing.header.mask = mask;
    }
    void End()
    {
        if (!worker.joinable() || stop) return;
        const double now = SDK::PlatFloatTime();
        if (now - lastPublish < .004 && drawing.header.mask == lastMask) return;
        if (!drawing.header.mask && !lastMask && now - lastPublish < .25) return;
        lastMask = drawing.header.mask;
        lastPublish = now; drawing.header.sequence = ++sequence;
        { std::lock_guard lock(mutex); pending = drawing; dirty = true; }
        wake.notify_one();
    }
    bool Box(float x, float y, float w, float h, Color_t color, float alpha, int style)
    {
        Record record; record.kind = 1; record.style = style;
        record.x = x; record.y = y; record.width = w; record.height = h; record.color = Color(color, alpha);
        return Append(record);
    }
    bool Name(float x, float y, const std::string& text, Color_t color, Color_t outline, byte backgroundAlpha, float alpha, float size)
    {
        Record record; record.kind = 2; record.x = x; record.y = y; record.size = size;
        record.color = Color(color, alpha); outline.a = backgroundAlpha; record.background = Color(outline, alpha);
        const size_t count = std::min(text.size(), sizeof(record.text) - 1);
        memcpy(record.text, text.data(), count);
        return Append(record);
    }
};
inline Output& Bridge() { static Output bridge; return bridge; }
const char* Status() { return Bridge().Status(); }
bool ModelTransportReady() { return Bridge().ModelsReady(); }
std::vector<unsigned char> AcquireModelPixels(size_t bytes) { return Bridge().ModelBuffer(bytes); }
void PublishModelPixels(std::vector<unsigned char>&& pixels,unsigned width,unsigned height,uint64_t capturedAt)
{ Bridge().Model(std::move(pixels),width,height,capturedAt); }
void ModelCaptureError(bool failed) { Bridge().CaptureError(failed); }
}
