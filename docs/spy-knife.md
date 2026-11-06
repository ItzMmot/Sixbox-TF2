# Spy knife: calibrated backstabs, trickstabs and visualization

Settings are in **Aimbot → Weapons → Melee**. Precise knife defaults on;
Warp behind, Auto trickstabs and Backstab visualization default off. Automation
requires the normal aim activation, Auto shoot and Auto backstab. It respects
attack readiness, target filters, Razorback avoidance and knife regeneration.

## Avoiding body slashes

Precision now owns the automated knife path completely. The raised-knife fast
path and historical melee fallback cannot override its rejection. Other melee
weapons and manual play outside active auto-aim retain their existing behavior.

A contact must pass padded backstab geometry over position uncertainty and both
yaw bounds, plus an inset finite ray/hull trace. Backstab safety increases those
margins. At the scoped BOTX setting of 0.9, this deliberately rejects narrow edge
windows. Fast unpredictable target yaw and stale observations also block attacks.
The normal native geometry is documented in Valve's
[knife implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_weapon_knife.cpp).

A successful warp approach sends movement only. The attack waits for a subsequent
command and acknowledgement of the completed movement packet. The owner brakes
while waiting, then validates the current target again. An aborted or manual warp
also establishes an acknowledgement barrier, preventing a fallback slash in the
same packet. Timeout or failed contact cancels the attempt.

The final command is checked again after late angle/movement helpers: weapon and
target identity, spawn, sampled pose, actual transmitted angles, held aim and
native contact must still agree. An invalid attack is removed, and an automatically
armed knife Doubletap is cancelled. The outgoing rewind tick corresponds to the
pose that was actually checked. Server aim holding exists independently of client
angles; see Valve's
[command processing](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/player_command.cpp).

These are client-side safeguards, not a measured miss rate or a guarantee against
unobserved server-side movement, yaw, custom game rules or prediction differences.
Live gameplay validation remains necessary.

## Auto trickstabs

Modes: **Auto**, **Sidestab**, **Matador**. Side routes seek a rear opening;
Matador has a short lateral bait followed by a side reversal. This is ordinary
ground movement and does not spend charged ticks. The planner forecasts target
movement through native MoveSim, probes a local route with actual collisions, and
executes only a route reaching a padded knife contact. It recomputes the approach
as observations arrive and validates the current contact before attacking.

Future target movement advances for trickstab planning, while shifted warp
commands do not multiply elapsed target time. The current implementation supports
grounded, non-ducking owners and grounded targets; stair/drop/air trickstabs are
not implemented. The planner does not decloak, recharge ticks, switch weapons or
promise that an opponent will react to a feint.

Search storage is fixed; native probes restore player prediction, clock, current
command and movement pointers. The global MoveSim snapshot policy remains the
upstream networked-only policy. Warp checks at most 42 movement steps with a soft
2 ms budget; trickstab target/route probes share a 96-step limit and soft 3 ms
budget per rendered frame. Atomic native calls can finish past a deadline.

## Backstab visualization

The selected/nearby target gets a facing arrow, native rear-sector outline,
highlighted stricter angular sector and optional route. A compact HUD reports
readiness or the reason for rejection: angle, reach, obstruction, protection,
uncertain pose, cooldown or movement acknowledgement. The displayed percentage
is geometric rear margin, not hit probability. A drawn sector alone does not
prove an unobstructed or reachable attack.

Geometry and target data are cached on the game thread. World and HUD renderers
only consume bounded snapshots; they do not enumerate or change entities. Hidden
menus, screenshots, expiry and map changes hide/reset the presentation. The HUD
uses the current client font and accent, with measured text width.

## BOTX and verification

The tuning script creates or reuses an aim-key → melee → Spy → third-slot branch
in **ItzMmotX BOTX**, enabling safety 0.9, Auto trickstabs (Auto), prediction 0.6 s,
and the visualization/route. The overlay has its own Spy + melee + third-slot
branch, so it remains visible without holding the aim key; movement automation
still requires aim activation. Earlier binds, Huntsman settings and unrelated
values remain intact. A timestamped backup and 360 out-of-scope scenario checks
precede installation. Restart with the updated DLL, then explicitly reload the
profile so an older in-memory profile does not overwrite the new branch.

Portable tests cover finite shift budgets, routes, Matador reversal, conservative
geometry versus native geometry, stricter drift thresholds, acknowledgement /
expiry boundaries and policy allocations. MSVC regular/Textmode checks and the
full FreeType AVX2 build verify native integration. Gameplay outcomes still need
testing on the intended server.

Server impact: automation can send silent aim rotations, and Warp behind spends
charged extra UserCmds through the existing CL_Move transport. It is intended for
the project's authorized `-insecure` HvH servers. Movement automation defaults off
and is disabled by compatibility mode. No entity-position teleport or new custom
packet transport is used.

Installed artifact (2026-11-06): `7cf780d51d0e35f81e248978a1b28aeefc9cd83dee2a1ef62417d16cd69289f5`
(6434816 bytes, 274 compiled client units). Output and Proton hashes match;
previous `f30318cf…` DLL and both old/tuned profile copies are verified.
The new gameplay behavior has not yet been measured in a live game.
