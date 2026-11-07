# Atlas aimbot adaptation: completion audit

Historical audit of the first adaptation build. Subsequent gameplay regressions,
fixes and the current installed DLL are documented in
[projectile-regressions.md](projectile-regressions.md).

Completed on 5 November 2026. Scope: the four algorithmic areas left incomplete
by the initial adaptation, implemented against SixBox's SDK. Atlas files are not
imported into the project. This document records implementation and validation;
it does not claim a measured improvement in gameplay accuracy.

| Requirement | Implemented behavior | Evidence | State |
| --- | --- | --- | --- |
| Per-command burst scheduler | Own firing position for every command; cooldown/ammo constrained scheduling of the whole series; individual withheld attacks and delayed series starts; CreateMove/Ticks enforcement with current weapon/target/config validation | `Shared/BurstPlanner.h`, Hitscan `CaptureBurstMotion` / `OptimizePattern`, Ticks `BeginBurstCommand` / `EnforceBurstCommand`; `burst_planner_test.cpp` executes 500 deterministic schedules and checks cooldown/ammo/release/float boundaries | Complete |
| Splash seam solver | Finite brush/displacement/stationary-object intersections and gap boundaries; rounded capsule and exact projectile-hull contact intervals; native muzzle/velocity correction and discrete flight alignment; actual impact/current target/radius-damage visibility validation | `World::GatherSeamFaces`, `Shared/SeamGeometry.h`, `Shared/SplashWindow.h`, Projectile `SearchSplashSeams` / `AimSplashContact` / `TestAngle`; geometry tests plus 500 randomized comparisons with an independent slab oracle | Complete |
| Persistent sticky search | Candidate/arc/roll/refinement/flight/air cursors retained across commands; bounded work and compatible motion/latency rebases; invalidation for changed lives/weapon/charge/map/settings; recomputed arrival and complete native verification before firing | `Shared/StickySearch.h`, Projectile `DiscoverSticky` / `FlySticky` / `SearchSticky`; continuation/invalidation/refinement tests and six-target worst-case work reservations in `sticky_search_test.cpp` | Complete |
| Animation reconstruction / resolver feedback | Each missing simulation tick replays at its own clock with interpolated movement and jump/landing/events; selected native pose/gesture continuation, layer order and eight feet-yaw histories; scoped state restoration and latch; records/shots carry life/hypothesis/revision identity | `Shared/AnimationTimeline.h`, `Resolver/AnimationPose.h`, Resolver `ReconstructAnimation` / `CommitShot`, entity/animation hooks and Backtrack; timeline/layer/discontinuity/stale-feedback tests in `animation_timeline_test.cpp` | Complete |

## Integration audit

- Delayed burst restores the engine's pre-command snapshot and executes the
  withheld current command with ordinary movement. Eye samples retain absolute
  shoot positions; future AntiWarp freezes velocity when that series starts.
  Snapshot access requires the active prediction scope, matching handle and data
  size. Scope exit restores the active prediction, command and movement pointers.
- MovementSimulation backups use upstream's networked-field snapshot scope.
  The broader Atlas adaptation was reverted during the projectile/knife regression
  audit; see [follow-up](melee-huntsman-fixes.md#follow-up-raised-spy-knife-rescue-ranger-and-grenade-launcher).
  New burst work retains fixed buffers and does not store paths.
- Sticky search retains up to 96 candidates for each of 16 target/weapon searches.
  Full native flights are bounded atomic work units of at most 512 ticks; no
  half-stepped owning VPhysics object is shallow-copied. Per-command reservations
  cover all six configured targets and smooth retests: 60 flights, 32768 physics
  steps and 512 checks. At most four probes and two verifications per target.
- Sticky geometry respects direct/prefer/only policies; Prefer has a bounded,
  fully validated direct fallback while surface search is deferred. Both arcs,
  six damped finite-difference refinements and ranked/continuing air-time scans
  are supported. Additional muzzle rolls require Silent with compatibility
  restrictions inactive. Current arming/radius/visibility/FOV checks still apply.
- The sticky path bypasses the old owning history maps/vectors. Path buffers are
  reserved and reused, movement lookups do not insert empty histories, and muzzle
  quadratic solves do not allocate vectors. Existing allocations elsewhere in the
  client remain. Erasing direct offsets while iterating and the smooth-retest
  flags were also corrected.
- Seam search retains at most 16 ranked candidates from cached finite faces,
  with third-face corner seeds. Per-command limits are 8192 face pairs, 1536 native
  muzzle setups and 64 seam validations. Up to six muzzle corrections and four
  timing alignment passes precede short traces on every native flight tick.
  Final checks use the actual impact, pulled-out explosion point, arrival target,
  sky/solid rejection and radius-damage visibility. Player/grace filtering is
  scoped to the straight-projectile seam path.
- Animation replay handles at most 24 missing ticks, pose/gesture continuation,
  jump/double-jump/landing fits, layer ordering and life/model/class/parent/scale/
  interpolation discontinuities. Native owning containers and interpolation
  history are never shallow-copied. Invalid continuity seeds a fresh endpoint.
  Engine clocks and player network state are restored after replay.
- Resolver records the final outgoing command after CritHack and last attack
  cancellations. Old-life/old-hypothesis misses cannot cycle a newer hypothesis;
  disabling Auto resolve clears pending feedback while preserving manual controls.

## Verified deliverables

- Seven C++20 portable executables pass with O2, Wall/Wextra/Werror:
  aim_policy, burst_planner, seam_geometry, splash_window, animation_timeline,
  sticky_search and auto_trolldier. Their relevant geometry/policy/search paths
  also assert no heap allocation. All seven are registered in the MSBuild CI
  workflow; GitHub CI has not been pushed or run for this worktree.
- MSVC Release AVX2 / Textmode AVX2 syntax checks pass for the changed native
  integrations. The final clean Release AVX2 build compiled all 272 translation
  units and linked successfully. No old client objects were reused.
- Aura source digests and all 16 embedded SM3 programs pass validation. New
  header registration in the project/filter XML and CI test registration pass.
- Static FreeType 2.14.3 uses the same /MD CRT as the client and the matching
  ImGui 1.92.7 backend. Actual Windows font regression tests pass with Michroma
  and Montserrat: glyph loader, expansion/glow masks, Gaussian falloff, atlas
  repacking, preserved metrics and cached draw geometry.
- A frozen 1178-file source manifest matches the build inputs. PE AMD64/DLL
  headers, entry point, fresh objects and output, and release CRT imports pass.
  No runtime FreeType DLL is needed. Atomic installation and SHA256 comparison
  confirm the Proton copy is identical to the built artifact.

Built artifact: `output/x64/ReleaseAVX2/SixBoxx64ReleaseFreetypeAVX2.dll`.
Installed: `/home/ItzMmotX/.local/share/Steam/steamapps/compatdata/440/pfx/drive_c/Amalgamx64ReleaseFreetypeAVX2.dll`.
Size: 6,368,768 bytes.
SHA256: `5ed694b988f8f34f228c24b429f539351572eb8b58fa9013690ff59ff9c1df0f`.

Backup: `.builds/atlas-final-avx2-freetype/installed-before-atlas-final.dll`.
Local build/installation evidence: `.builds/atlas-final-avx2-freetype/`, including
`inputs.json`, `configuration.json`, compiler/link logs and `installed.json`.

## Practical limits

The search is bounded and does not guarantee discovery of every valid shot on
every map. Hidden intermediate commands/events and animation hypotheses are
inferred from observations, not known server truth. The initial adaptation's
unknown-seed coverage, movement confidence and splash damage scores remain
heuristics. Redirected airblast projectiles keep their existing path.

Gameplay hit-rate, damage, CPU and Proton in-game behavior have not been measured
for this DLL. Those measurements are needed to quantify the improvement; a clean
build and analytic tests do not supply that percentage. Changes are left in the
worktree; no commit or push was requested for this continuation.
