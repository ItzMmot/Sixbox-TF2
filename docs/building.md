# Local Linux / Proton build

Run `./build.sh` from the repository root. The command stays the same after
source changes. Close TF2 first: the default action builds **Release AVX2 with
static FreeType**, then installs the DLL into the TF2 Proton prefix.

The entry point resolves paths relative to itself, so it also works when invoked
by its absolute path from another directory. Its implementation is tracked in
`.github/scripts/build-local-dll.py`; it does not call temporary task scripts.

## Prepared toolchain

This workspace already has Wine, MSVC, the Windows SDK, Boost headers, FreeType
2.14.3 sources and the ImGui 1.92.7 FreeType backend. The script uses those local
dependencies; it does not install a toolchain on a new machine.

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

- Reads the current `ClCompile` source list from `Unibox.vcxproj`, including
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

Output: `output/x64/ReleaseAVX2/Sixboxx64ReleaseFreetypeAVX2.dll`.
Default installed filename: `Amalgamx64ReleaseFreetypeAVX2.dll`, under
`~/.local/share/Steam/steamapps/compatdata/440/pfx/drive_c/`.

Build logs, successful input fingerprint, installation receipt and backups live
under `.builds/local-dll/`. Running again with an identical installed DLL does
not create another backup.

`--clean` forces a rebuild, `--no-install` only produces the DLL, and `--jobs N`
sets compile parallelism (default: two). These options are optional; the normal
command is always `./build.sh`.
