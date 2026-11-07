// tests/identity_migration_test.cpp — staged legacy data migration and branding.
#include "../SixBox/src/Features/Configs/ConfigMigration.h"
#include <cassert>
#include <fstream>
#include <iterator>
static std::string read(const std::filesystem::path& p)
{std::ifstream file(p);return {std::istreambuf_iterator<char>(file),{}};}
int main()
{
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("sixbox-migration-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"unibox"/"Core");
    fs::create_directories(root/"unibox"/"Visuals");
    std::ofstream(root/"unibox"/"Core"/"crate_loot_cache.txt")<<"old cache";
    std::ofstream(root/"unibox"/"ItzMmotX BOTX.json")<<"original binds";
    std::ofstream(root/"unibox"/"active.cfg")<<"active profile";
    const auto migrated=SixBoxIdentity::DataRoot(root);
    assert(migrated==root/"6box");
    assert(read(migrated/"Core"/"crate_loot_cache.txt")=="old cache");
    assert(read(migrated/"ItzMmotX BOTX.json")=="original binds");
    assert(read(migrated/"active.cfg")=="active profile");
    assert(fs::is_directory(migrated/"Visuals"));
    assert(read(root/"unibox"/"ItzMmotX BOTX.json")=="original binds");
    std::ofstream(migrated/"active.cfg")<<"new profile";
    assert(SixBoxIdentity::DataRoot(root)==migrated);
    assert(read(migrated/"active.cfg")=="new profile"); // No overwrite on repeat.
    for(const auto& file:fs::directory_iterator(root))
        assert(file.path().filename().string().find(".6box-migration-")!=0);
    fs::create_directories(root/"fresh");
    assert(SixBoxIdentity::DataRoot(root/"fresh")==root/"fresh"/"6box");
    assert(!fs::exists(root/"fresh"/"6box"));
    assert(SixBoxIdentity::IsLegacyLabel("unibox",false));
    assert(SixBoxIdentity::IsLegacyLabel("Amalgam",false));
    assert(SixBoxIdentity::IsLegacyLabel("[unibox]",true));
    assert(SixBoxIdentity::IsLegacyLabel("[SIXBOX]",true));
    assert(!SixBoxIdentity::IsLegacyLabel("my custom title",false));
    assert(!SixBoxIdentity::IsLegacyLabel("[custom]",true));
    assert(!SixBoxIdentity::IsLegacyLabel("unibox",true));
    fs::remove_all(root);
}
