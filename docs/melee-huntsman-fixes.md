# Melee / Huntsman regression fixes

5 November 2026. These changes follow the reports that melee did not swing and
Huntsman withheld shots or hit bodies. In-game results still need verification;
source-level causes and regression checks are recorded below.

## Code

- Huntsman safe-point velocity used gravity already expressed in HU/s², then
  multiplied it by server gravity again. The shared FlightVelocity policy now
  subtracts acceleration × flight seconds exactly once. The grid plane uses
  normalized directions; physical velocity retains its actual magnitude.
- The former head check extended an artificial ray far beyond the player-hull
  contact. ArrowTouch's actual choice uses a finite velocity × server-frame trace,
  with a nearest-box fallback from 16 HU beyond contact. The new allocation-free
  policy models both paths against the captured pose. Grid candidates first
  estimate player-hull contact; final native validation uses the actual contact
  and velocity. Head-intended bow flights use one-tick collision intervals.
- Melee previously abandoned a target if a history container existed but no
  record survived all rewind-time filters. It now falls back to predicted/current
  collision bounds. Historical skeleton availability is not required to validate
  physical melee reach. Fresh fallback geometry is not sent as an old backtrack
  pose, and missing bones are not dereferenced by on-shot visualization.
- Failed local movement initialization falls back to current reach rather than
  using a default origin as the simulated shooting position.
- Swing-window waiting has an engagement state: consecutive checks on the same
  target cannot keep deferring a valid attack forever. Starting a swing resets it.
  The dedicated ATD strike path is unchanged.

The finite arrow touch/fallback and melee ray-before-hull behavior were checked
against Valve's public implementations:
[arrow](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/tf/tf_projectile_arrow.cpp),
[melee](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_weaponbase_melee.cpp).
Intermediate future server poses remain unknown; this is not a headshot guarantee.

## Selective BOTX tuning

The original config is backed up under `.builds/config-tuning/`. Existing bind
entries, existing variable values and all groups were preserved. Four new bind
conditions are children of the existing aim key:

- Sniper → projectile weapon → primary slot: Huntsman and its reskins only.
- Melee weapon: melee settings only.

Both scopes use Silent auto aim/auto shoot, disable lead restriction and Doubletap
inside that scope, and retain the original aim-key activation.
Huntsman uses head-only auto selection, safe points, straight charge shots,
1.5-second maximum flight prediction, no extra movement-history chance gate, and
no legacy vertical head offsets/pull. Melee retains swing prediction and ping
accounting, disables optional swing waiting and teammate whip priority, and keeps
auto backstab. Other weapons retain all original maps and defaults.

The script checked 48 input/class/type/slot combinations against the old config;
outside the two intended scopes, effective values remained identical. This is a
reliability-oriented starting preset, not a measured universal optimum.

## Checks

All seven portable aim/ATD regression programs pass. Added tests cover physical
gravity units, finite arrow head/body selection, the case where a distant head
on an infinite ray would disagree with the server's nearby-body fallback, and
bounded consecutive melee deferral. The existing no-allocation assertion covers
these policies too. MSVC native syntax checks pass, including Textmode AVX2.

The permanent command is `./build.sh`; it builds FreeType AVX2 and installs the
verified DLL with a previous-version backup. Reload `ItzMmotX BOTX` after restart.

## Installed artifact

The permanent build command completed all 272 client translation units and
installed the new FreeType AVX2 DLL in the TF2 Proton prefix. The built and
installed SHA256 match:
`cb5469c1782539d373f343b8c11ed82d1c64b0eb2a97f5a566bca87ee5879921`.
The previous `5197e9b9…` DLL is backed up under `.builds/local-dll/backups/`.
Final verification also confirmed every original BOTX bind/value/group remains
unchanged and all newly scoped overrides are present. Runtime gameplay validation
is still needed after loading this DLL and reloading the profile.

## Follow-up: raised Spy knife, Rescue Ranger and grenade launcher

Compared the affected code with upstream Unibox at
[71cbc2dd](https://github.com/GatoPotato658/Unibox/tree/71cbc2dd5fee4c145946904ecd31bbc51f9f8137).
The upstream projectile simulator matches our pre-transfer simulator. Upstream
MoveSim still uses `PC_NETWORKED_ONLY`; the Atlas adaptation had expanded this to
`PC_EVERYTHING`. Restored upstream's snapshot scope while retaining allocation
checks and reusable storage. This is a targeted regression rollback, not proof
that the broader copy alone caused every reported failure.

Projectile prediction now takes its starting position from the last networked
sample when accounting for ping, following upstream's time-base correction.
Uncompensated prediction avoids the additional choke pre-roll. The same target
seed is used for reflected-projectile prediction.

The direct search previously consumed an offset as soon as its analytic solve
became feasible, before native collision validation. A rejected first flight
therefore had no nearby arrival frame to try. Keep up to four candidates within
three extra ticks; original candidates precede retries, and retries share the
existing optional-search time budget. Each still requires native collision,
obstruction, aim and timing checks. Explicit clipping against the simulated
collision object supplements spatial-partition queries for moved targets; a
nearer world/entity obstruction still wins.

For an already raised Spy knife, auto-fire can validate the current command ray
(and hull only after a clear ray) and current backstab geometry directly. It
requires active aim, auto-shoot, attack readiness, an enemy target and permitted
attributes, and respects Razorback avoidance. It does not trust the ready flag
alone. This path does not depend on historical bones, doubletap charge, an
extrapolated second victim yaw, or an old smack timer. Regular melee acquisition
uses current bounds for FOV instead of requiring a historical hitbox pose.

Bounded local diagnostics: `SpyKnife.txt` records the knife pipeline;
`ProjectileRescue.txt` now also records grenade launchers with `projectile-fix-2`,
weapon ID, actual tested candidate time, tick interval and final distance to its
aim point. Earlier v1 records paired the last analytic solve with the last tested
flight, which could be different candidates: their apparent timing discrepancy
alone was not sufficient to diagnose a one-tick error.

Portable policy checks cover backstab geometry (including pitch/side cases),
finite contact windows and bounded retry counts without allocations. MSVC checks
cover regular and Textmode AVX2 builds. Native gameplay still needs retesting:
these checks cannot prove live engine collision/animation behavior.

Follow-up DLL built and installed on 2026-11-06: SHA256
`9c63ad86a230f1bbbb7b70177c3784a9ed34d505f36df1edb81ca46ea03cda60`. All 272 client translation units completed;
the installed file and output DLL hashes match. Previous `cb5469c1…` DLL
was verified in `.builds/local-dll/backups/`. Gameplay validation remains pending.
