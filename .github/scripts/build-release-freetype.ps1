# .github/scripts/build-release-freetype.ps1 - Release FreeType/AVX2 build and packaging
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
Set-Location $root
python .github/scripts/build-aura-shaders.py --check
if ($LASTEXITCODE -ne 0) { throw 'Aura shader source/bytecode validation failed.' }
$deps = Join-Path $root '.builds/release'
$freeTypeRoot = Join-Path $deps 'freetype-install'
$backend = Join-Path $deps 'imgui/misc/freetype'
$header = Get-Content 'Unibox/include/ImGui/imgui.h' -Raw
$upstreamHeader = Get-Content "$deps/imgui/imgui.h" -Raw
$pattern = '(?m)^#define IMGUI_VERSION\s+"([^"]+)"'
if ([regex]::Match($header, $pattern).Groups[1].Value -ne [regex]::Match($upstreamHeader, $pattern).Groups[1].Value) {
    throw 'Bundled ImGui version changed: update the pinned backend in the release workflow.'
}

# Static font library with the same /MD CRT as the client. No runtime font DLLs.
cmake -S "$deps/freetype" -B "$deps/freetype-build" -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_INSTALL_PREFIX=$freeTypeRoot" -DBUILD_SHARED_LIBS=OFF `
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
    -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON `
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
if ($LASTEXITCODE -ne 0) { throw 'FreeType configuration failed.' }
cmake --build "$deps/freetype-build" --config Release --target install --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'FreeType build failed.' }

msbuild Unibox.sln /m:2 /p:Configuration=ReleaseAVX2 /p:Platform=x64 `
    /p:UseFreeType=true "/p:FreeTypeRoot=$freeTypeRoot" "/p:ImGuiFreeTypeRoot=$backend" `
    "/p:TargetName=$env:RELEASE_NAME"
if ($LASTEXITCODE -ne 0) { throw 'Client build failed.' }

# Exercise the actual FreeType glyph loader and retain expansion/glow behavior.
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devcmd = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$test = Join-Path $deps 'test-freetype.cmd'
@"
@echo off
call "$devcmd" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MD /O2 /DIMGUI_ENABLE_FREETYPE /I"$root/Unibox/include" /I"$root/Unibox/include/ImGui" /I"$backend" /I"$freeTypeRoot/include/freetype2" "$root/tests/text_effects_test.cpp" "$root/Unibox/src/Features/ImGui/TextEffects/TextEffects.cpp" "$root/Unibox/include/ImGui/imgui.cpp" "$root/Unibox/include/ImGui/imgui_draw.cpp" "$root/Unibox/include/ImGui/imgui_tables.cpp" "$root/Unibox/include/ImGui/imgui_widgets.cpp" "$backend/imgui_freetype.cpp" /Fe:"$deps/text-effects-test.exe" /link "$freeTypeRoot/lib/freetype.lib"
if errorlevel 1 exit /b 1
"$deps/text-effects-test.exe" "C:/Windows/Fonts/arial.ttf"
"@ | Set-Content $test
& cmd /c $test
if ($LASTEXITCODE -ne 0) { throw 'FreeType text effects regression test failed.' }

$build = Join-Path $root 'output/x64/ReleaseAVX2'
$bundle = Join-Path $root 'output/release'
New-Item -ItemType Directory -Force $bundle | Out-Null
Copy-Item "$build/$env:RELEASE_NAME.dll" $bundle
Copy-Item "$build/$env:RELEASE_NAME.pdb" $bundle
$licenses = Join-Path $deps 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item LICENSE.md "$licenses/Sixbox-LICENSE.md"
Copy-Item "$deps/imgui/LICENSE.txt" "$licenses/ImGui-LICENSE.txt"
Copy-Item "$deps/freetype/LICENSE.TXT" "$licenses/FreeType-LICENSE.txt"
Copy-Item "$deps/freetype/docs/FTL.TXT" "$licenses/FreeType-FTL.txt"
Copy-Item "$deps/freetype/docs/GPLv2.TXT" "$licenses/FreeType-GPLv2.txt"
Compress-Archive -Path "$licenses/*" -DestinationPath "$bundle/Third-party-licenses.zip" -Force
Get-ChildItem $bundle -File | Sort-Object Name | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
} | Set-Content "$bundle/SHA256SUMS.txt" -Encoding ascii
