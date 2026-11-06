# Projectile regressions after the Atlas adaptation

Further melee/Huntsman fixes, selective BOTX settings and the latest installed
DLL are documented in [melee-huntsman-fixes.md](melee-huntsman-fixes.md).

Source fixes prepared on 5 November 2026. The permanent local build command performs compilation and installation
together. Check the latest installation receipt in `.builds/local-dll/installed.json`.

## Fixed in source

- Huntsman captured head bones only when the target already had its head selected.
  The new Target_t starts with hitbox -1; BuildDirects assigns head priority later.
  The safe-point path therefore received no pose and rejected the shot. The pose
  is now captured before MoveSim independently of that later selection, copied
  into fixed owned storage, with a current SetupBones fallback when no usable
  backtrack record exists.
- The 7x7 head grid now ranks cheap geometric checks first and performs at most
  four native muzzle/ballistic solves (normally one), rather than 49 solves for
  every candidate prediction tick. Timing deferral stops immediately and retries
  on the following prediction tick; native hitbox/flight validation remains.
- Trace mode no longer invokes the Face-mode polygon/seam discovery. Sticky
  discovery respects the same mode. Trace ranks its ordinary candidates but
  stops at the first validated hit instead of exhaustively searching later frames
  and refining an already successful hit.
- Optional seam/muzzle/refinement/sticky-search work shares a soft 1.5 ms budget
  per rendered frame across targets and commands, including Doubletap. An atomic
  native flight/geometry query can overrun the deadline; this is not a bound on
  total aimbot time or a measured FPS guarantee. Deferred sticky search keeps its
  cursor. Ordinary direct validation/fallback remains available.
- Removed the additional confidence hard gate applied to direct projectiles and
  splash through the existing Hit chance slider. The original movement-history
  Hit chance check remains. Confidence only weights splash ranking, with a
  positive floor, rather than independently forbidding a native-verified hit.

## Rescue Ranger: diagnosis still open

The user reports no automatic shot even against stationary exposed players,
while prediction lines appear. That rules out movement confidence as a complete
explanation. No separate verified fix is claimed yet.

The next DLL automatically writes a bounded local diagnostic file while Rescue
Ranger is aimed at a target: `unibox/ProjectileRescue.txt` in the TF2 directory.
It records target selection/angle search/native setup/flight/collision/timing,
CanHit's actual returned result, weapon readiness, Auto shoot, and the aimbot's
attack request. There is no chat/party output or configuration edit. Formatting
and file IO occur at most once per two seconds and stop after 32 records;
trace-loop updates only change fixed primitive fields. Handles are closed after
each write. A short in-game test after rebuilding is needed to identify which
stage rejects this weapon and finish its fix.

## Checks and local build

- Seven portable aim/ATD regression executables pass. New head-cell tests exercise
  success, fallback, immediate timing deferral, empty masks and the four-solve
  limit, including their existing no-allocation assertion.
- MSVC Release AVX2 and Textmode AVX2 syntax checks pass for the final projectile
  source. The build/install Python scripts pass bytecode syntax validation.
- Built and installed through `./build.sh`: 272 client translation units,
  Release AVX2 with static FreeType, 6,373,376 bytes. The installed SHA256 is
  `5197e9b967b822c3ea39056bc22381c74f75d6c0ffac23dda05981cf9d2e7d67`.
  The previous DLL is preserved in `.builds/local-dll/backups/` and its original
  checksum was verified. This revision still needs the in-game Rescue Ranger
  test; no measured FPS or gameplay fix for that weapon is claimed.

Run `./build.sh` from the repository root. This is the permanent build command;
see [building.md](building.md) for prerequisites, cache/backup behavior and paths.
The helper rebuilds on header/source changes and installs the verified DLL in
Proton automatically. Close TF2 before running it.
