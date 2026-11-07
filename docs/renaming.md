# SixBox / 6box identity

Product name: **SixBox**. Short tag and data directory: **6box**, **[6box]**.
The existing GitHub repository remains `ItzMmot/Sixbox-TF2`; upstream attribution
and license notices remain intact.

- Solution/project: `SixBox.sln`, `SixBox/SixBox.vcxproj`.
- Source and bundled includes: `SixBox/src/`, `SixBox/include/`.
- FreeType AVX2 artifact and installed DLL: `SixBoxx64ReleaseFreetypeAVX2.dll`.
- Launcher: `scripts/inject-sixbox-offline.bat`, deployed on the next successful
  local build. The previous launcher name forwards to it for existing Steam
  launch options; the original launcher is backed up.
- Runtime data: `6box/`, including profiles, player lists, skins, nav data,
  materials, loot caches and logs. First-run copying preserves the original
  `unibox/` directory and never overwrites existing SixBox data.
- New default menu title: `SixBox`; default tag: `[6box]`. Exact former default
  labels in loaded configs are migrated while user-defined labels stay intact.
- Console/messages, notice repository link, material identifiers, release
  workflow names and download links use the new identity.

Historical crash reports may mention former DLL names. Compatibility code
intentionally recognizes the old data directory, launcher and loot-cache magic.
The checkout's parent directory is independent of the project identity.

Launcher deployment happens only after a successful local build and DLL
installation. Configuration migration takes effect on the first launch of the
new client; existing Steam launch options continue to work through the wrapper.
