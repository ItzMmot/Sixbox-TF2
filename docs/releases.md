# FreeType AVX2 releases

The **Release FreeType AVX2** GitHub Actions workflow builds and publishes a
Windows x64 release whenever an existing commit is tagged `v*` and the tag is
pushed. Use a version such as `v1.0.0`; tags with a suffix such as `v1.0.0-rc.1`
are published as prereleases. The tagged commit must include the workflow.

You can also select **Actions → Release FreeType AVX2 → Run workflow** and enter
an existing version tag. The checkout and uploaded binaries always correspond
to that tag. A rerun replaces the assets on its existing release.

The workflow uses the repository's built-in `GITHUB_TOKEN` with `contents: write`;
no personal access token or SSH key is needed. It does not publish for pull
requests or ordinary branch pushes. The existing MSBuild CI remains separate.

## Assets

- `Sixboxx64ReleaseFreetypeAVX2.dll`: GUI build, MSVC ReleaseAVX2, x64, `/MD`.
- Matching `.pdb` symbols.
- `Third-party-licenses.zip`.
- `SHA256SUMS.txt` for the preceding files.

These are also retained as an Actions artifact. AVX2 CPU support is required.
FreeType is statically linked, so there is no separate FreeType DLL to install.
Normal Microsoft Visual C++ runtime requirements remain unchanged.

## Build details

FreeType is pinned to `VER-2-14-3`. Optional zlib, bzip2, PNG, HarfBuzz and Brotli
dependencies are disabled. Dear ImGui's FreeType backend is pinned to `v1.92.7`,
matching the bundled headers; the script rejects mismatched versions. Update the
backend pin when updating bundled ImGui.

`Unibox/FreeType.props` is imported only when `UseFreeType=true`. It adds the
backend source, includes, static library and `IMGUI_ENABLE_FREETYPE` definition.
Existing build configurations retain their current default rasterizer. The font
expansion/glow adapter supports both stb and FreeType while preserving the
native FreeType hinting flags. Before publication, the workflow compiles and runs
`tests/text_effects_test.cpp` against the built FreeType library and Windows Arial.

The build/package script is `.github/scripts/build-release-freetype.ps1`.
Its dependency checkouts are prepared by the workflow; it is intended to run
inside a Windows runner with Visual Studio 2022, CMake and restored NuGet packages.
