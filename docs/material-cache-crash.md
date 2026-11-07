# Material uncache crash during map load

Reported 7 November 2026 when entering `koth_mannhole` with the 6 November
16:25:03 ReleaseAVX2 DLL (`7e76a4a30844a4a1b37277f20d0f508986d448c5dc722cbe1ed4a5ac9d3403f5`).
The full log was found in TF2's working directory as `crash_log.txt`; the older
`unibox/crash_log.txt` was unrelated. Crash RVA: `0x35af00`, RCX: `0x230`.

A diagnostic relink from the retained objects produced a byte-identical `.text`
section. Its MAP identifies the frame as `Hooks::CMaterial_Uncache::Func`, which
starts at RVA `0x35ae50`. The fault occurs while looking up a material pointer
in `F::Materials.m_mMatList`: the computed bucket address is based on a null
table pointer. The caller is `materialsystem.dll` during client map loading.

The original container had no synchronization. Engine workers could read it
while render-side creation, removal, glow registration or unload changed it.
The observed frame establishes the failing lookup; the log alone does not prove
which overlapping writer or corruption event produced its invalid table.

Replace that public container with a private `CMaterialRegistry`: every lookup,
insertion, erasure and clear uses the same mutex. All call sites, including Glow,
use its methods. No engine call runs under the registry lock: releasing a
reference can synchronously re-enter Uncache. The lookup is allocation-free;
material creation can allocate as before. The change preserves which tracked
materials request variable preservation.

The regression test runs two writers through insertion/rehash/erase/clear while
two readers execute lookups, then checks exact membership after joining. It also
checks synchronous lookup after removal, duplicate/null registration and no
allocation across 100000 existing-material lookups. Run with `-std=c++20 -pthread`.
The test is included in CI. The local build now retains `client.map` alongside
its logs to resolve future crash addresses without recompiling.

Live re-entry into the reported map is still needed to confirm the crash is gone.
