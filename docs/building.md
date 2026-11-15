# Building SixBox

## Windows x64

Install Visual Studio 2022 C++ desktop tools, a Windows SDK and NuGet CLI. From
a developer command prompt run:

```bat
nuget restore SixBox.sln
msbuild SixBox.sln /m:2 /p:Configuration=ReleaseAVX2 /p:Platform=x64
```

The FreeType AVX2 release recipe is `.github/scripts/build-release-freetype.ps1`;
`.github/workflows/release-freetype-avx2.yml` pins and downloads its dependencies.
Ordinary MSBuild uses the bundled font backend unless FreeType is explicitly set.

## Linux / Proton

Run `./build.sh` from the repository root. The command stays the same after
source changes. Close TF2 first: the default action builds **Release AVX2 with
static FreeType**, then installs the DLL into the TF2 Proton prefix.

The entry point resolves paths relative to itself, so it also works when invoked
by its absolute path from another directory. Its implementation is tracked in
`.github/scripts/build-local-dll.py`; it does not call temporary task scripts.

## Prepared toolchain

Install Wine, MSVC x64 tools and the Windows SDK, Boost headers, FreeType
2.14.3 sources and the ImGui 1.92.7 FreeType backend before building. The script
validates these dependencies; it does not download proprietary build tools.
A clean checkout contains the complete client source and overlay integration.
Use the variables below for toolchains outside the default locations.

`./build.sh --check` validates prerequisites without compiling or installing.
The default search locations are `.builds/tools/msvc`, `/usr/include/boost`,
`.builds/deps`, `.builds/atlas-final-deps`, and `.builds/release`.

Optional environment overrides:

| Variable | Directory |
| --- | --- |
| `SIXBOX_MSVC_TOOLS` | MSVC version directory containing `bin`, `include`, `lib` |
| `SIXBOX_WINDOWS_SDK` | Windows SDK directory containing `Include` and `Lib` |
| `SIXBOX_FREETYPE_SOURCE` | FreeType source root |
| `SIXBOX_IMGUI_FREETYPE` | Matching ImGui `misc/freetype` backend directory |
| `SIXBOX_BOOST_INCLUDE` | Include directory containing `boost/` |
| `SIXBOX_PROTON_DRIVE` | TF2 Proton `drive_c` directory |

## Rebuild and installation behavior

- Reads the current `ClCompile` source list from `SixBox.vcxproj`, including
  optional files guarded with `Exists(...)`.
- Fingerprints source/header contents, added/deleted files, project/properties,
  the build scripts, dependencies and compiler configuration. A changed input
  rebuilds every client object, including the PCH. An unchanged successful DLL
  can be reused after checksum and PE validation.
- Uses a build lock. Compilation/link failures or edits made during the build
  cancel installation, leaving the installed DLL intact.
- Verifies AMD64 DLL headers, entry point and imports. FreeType is static;
  debug CRT or runtime FreeType DLL dependencies are rejected.
- Saves a timestamped, checksum-verified backup of the previous DLL, installs
  through atomic replacement, and compares the installed SHA256 with the output.

Output: `output/x64/ReleaseAVX2/SixBoxx64ReleaseFreetypeAVX2.dll`.
Default installed filename: `SixBoxx64ReleaseFreetypeAVX2.dll`, under
`~/.local/share/Steam/steamapps/compatdata/440/pfx/drive_c/`.

Build logs, successful input fingerprint, installation receipt and backups live
under `.builds/local-dll/`. Running again with an identical installed DLL does
not create another backup.

`--clean` forces a rebuild, `--no-install` only produces the DLL, and `--jobs N`
sets compile parallelism (default: two). These options are optional; the normal
command is always `./build.sh`.


The project/solution are `SixBox/SixBox.vcxproj` and `SixBox.sln`. The installed
DLL uses the same `SixBoxx64ReleaseFreetypeAVX2.dll` name as the release artifact.
After a successful DLL installation, the helper deploys
`inject-sixbox-offline.bat`. For an existing Steam launch command, the former
`inject-amalgam-offline.bat` becomes a small forwarding wrapper; its previous
contents are backed up. Existing launch options therefore still work. Launcher
files are not changed by `--check` or `--no-install`.

On the first launch of the new client, existing `unibox/` data are copied to
`6box/` through a staging directory and published by rename. The original data
are retained, and an existing `6box/` directory is never overwritten. If the
copy fails, the client uses the old directory for that run. Old default titles
and tags are normalized to SixBox / [6box]; custom labels are preserved. Loot
cache readers accept both the former and new file signatures.

## Optional Linux helpers

[Overlay setup](overlay.md) and [Karaoke setup](karaoke.md) describe the platform
requirements and installation. Releases include `SixBox-Linux-Helpers.zip` with
helper sources, vendor assets, licenses and setup docs. These helpers are
optional for ordinary DLL use; Overlay render requires its Linux helper and
KWin capture-exclusion support. Native Windows does not support that renderer.
