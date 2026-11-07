#pragma once
#include <chrono>
#include <filesystem>
#include <string_view>
#include <string>

namespace SixBoxIdentity
{
    inline constexpr const char* Name="SixBox";
    inline constexpr const char* Tag="[6box]";
    inline constexpr const char* DataDirectory="6box";

    // Copy first, publish by rename, retain the original. A partial copy is never
    // used as the active config directory. Existing SixBox data always wins.
    inline std::filesystem::path DataRoot(const std::filesystem::path& working)
    {
        namespace fs=std::filesystem;
        const auto destination=working/DataDirectory,legacy=working/"unibox";
        std::error_code error;
        if(fs::exists(destination,error)||error) return destination;
        if(!fs::is_directory(legacy,error)||error) return destination;
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        const auto staging=working/(".6box-migration-"+std::to_string(stamp));
        // create_directory reserves this exact staging name before copying.
        if(!fs::create_directory(staging,error)||error) return legacy;
        fs::copy(legacy,staging,fs::copy_options::recursive|fs::copy_options::copy_symlinks,error);
        if(!error) fs::rename(staging,destination,error);
        if(!error) return destination;
        std::error_code cleanup;fs::remove_all(staging,cleanup);
        return legacy; // Preserve access to the originals if migration failed.
    }
    inline bool IsLegacyLabel(std::string_view value,bool tag)
    {
        if(tag)
        {
            if(value.size()<2||value.front()!='['||value.back()!=']') return false;
            value.remove_prefix(1);value.remove_suffix(1);
        }
        return value=="unibox"||value=="Unibox"||value=="UNIBOX"
            ||value=="amalgam"||value=="Amalgam"||value=="AMALGAM"
            ||value=="sixbox"||value=="Sixbox"||value=="SIXBOX"||value=="SixBox"||value=="6box";
    }
}
