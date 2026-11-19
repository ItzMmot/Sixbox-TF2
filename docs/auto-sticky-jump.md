# Auto Sticky Jump

**Misc → Main → Movement → Auto sticky jump**. Hold a sticky launcher as Demoman,
stand on clear ground, then enable the module (a toggle bind is recommended).
Both ordinary sticky launchers and the Sticky Jumper are supported, including
the Jumper's separate practice-projectile type.
The module does not start/control commands while ClickGUI or the game menu is open;
opening a menu cancels the current sequence.
Auto walk picks up the current velocity direction, falling back to movement input
or camera yaw when stationary.
The default is one jump per activation; disable/re-enable to plan another.

- **Horizontal / Vertical** rank floor placements for forward or upward push.
- **Plain / Silent / Legit / Silent legit** control camera rotation. Legit modes
  wait for the camera to reach the actual firing angle; rotation speed is adjustable.
- **Auto walk** walks to the marked takeoff position. With it disabled, walk there
  yourself. Placement and approach keep full run input; the module never
  counter-strafes to wait for a sticky.
- **Minimum takeoff distance** sets a lower bound. The actual distance includes
  current speed, acceleration, weapon cooldown, the second shot and arm time. Routes check player-hull
  clearance, step height and supporting floor rather than walking through walls.
- **Sticky count** selects one or two. **Sticky doubletap** appears for two and
  uses already charged ticks through the normal command transport. It does not
  recharge automatically. Server packet limits and the weapon's real cooldown
  apply: both shots fit in one burst only when enough commands are available;
  otherwise the charged commands shorten the interval and the second shot waits.
  Prediction's tickbase rewind at burst startup does not cancel the sequence;
  weapon cooldown and arm checks still use the owner's current game time.
- **Flush first** detonates old stickies before planning, waits for their removal
  in both the entity list and launcher count, and handles directional Scottish
  Resistance detonation. It takes priority over Preserve existing traps.
- **Preserve existing traps** blocks a new sequence while old stickies exist.
  Disable it only if detonating those traps along with the jump is intended.
- **Repeat on landing** automatically plans another jump after landing.

The sequence solves a low ballistic arc using the real launcher muzzle, charge
speed and upward grenade impulse, then verifies it with the existing projectile
simulator. It waits for the actual new projectiles to appear and settle, adjusts
to their observed positions, and checks all bombs' arm times before detonation.
Attachment uses the authoritative touched flag; an old network velocity does not
make a stuck sticky look airborne. Missing/invalid creation timestamps use the
first observation as a conservative age bound.
`tf_grenadelauncher_livetime`, `sticky_arm_time` and Haste/King modifiers are read
from the game. Scottish Resistance's directional detonation aims at the bombs.
The second shot waits for the native charge to clear and for next-primary-attack
plus a command margin. A stale first-shot charge cannot count as a second sticky.
Launch timing searches a continuous blast corridor along the actual velocity,
with half-tick samples and two ticks of jump/detonation lead. The takeoff marker
and path follow that selected window; passing the original marker is not a
cancellation condition. Current blast contact uses the player collision hull and
the same visibility trace as Auto Detonate. Detonation remains available during
reload through the launcher busy frame; the generic secondary-attack cooldown
does not block starting the jump. Player attack restrictions still apply.
The jump presses jump + crouch; detonation waits for airborne upward movement
and a valid, unobstructed blast window. Missing bombs, blocked routes, changed
weapons/settings, death and timeouts cancel the sequence. Manual attack takes over.
Auto Dodge also cancels the sequence when it takes movement control, including
when a threat is detected during the late predicted movement check.

Lead distance, low-arc pitch and half-tick corridor calculations live in
`Features/Aimbot/Shared/StickyJumpMath.h`.

## Rendering

**Sticky jump rendering** opens the arrow, filled human silhouette marker,
approach line, sticky trajectory, status HUD, scale, colors and depth settings.
These are rendered by Jump Coach even when its main toggle is off. The silhouette
uses the actual animated player model and visible attached equipment, with an
independent bone palette/transform for each mesh. It is hidden when the camera
intersects it.

**Jump coach flight preview** enables the coach's ordinary movement prediction
and HUD during this jump, including for Demoman when Soldier only is selected.
It predicts from observed player velocity after the boost, not a fabricated exact
blast velocity. Ordinary Jump Coach settings still control that flight display.

Calibration uses server-observed bombs and current attributes, but cannot promise
an exact result for arbitrary server plugins or a virtual weapon whose statistics
differ from the server weapon. Check the HUD on your server; two stickies can
inflict substantially more self damage than one.

## Checks

`tests/auto_sticky_jump_test.cpp` covers arm-time attributes/runes, real release
edges, packet budgets, momentum lead, stale-charge/cooldown gates, contact gates and detonation windows. Native
MSVC GUI/Textmode checks cover the adapter and command/render integration.
`python3 tests/auto_sticky_jump_controller_test.py` runs the production command
controller and entity-cache predicate with a fake engine: stock/practice bombs,
one/two shots, both jump modes, all rotations, manual/automatic walking, flushing,
and a backward clock step at doubletap startup. Shot/route planning and
diagnostic file output are stubbed; jump and detonation buttons are checked.
In-game timing and live server behavior require gameplay testing.

## Diagnostics

`6box/Core/sticky-jump.log` records stage changes and cancellations, including
attachment, arm readiness, line of sight, raw projectile velocity/timestamp and
current/future blast scores. It is local, bounded to roughly 1 MiB and written
on state changes and at most ten times per second near the takeoff window.
It also records the generic secondary-attack clock for diagnosing reload-related
failures; that clock does not gate sticky detonation. Version 5 also records the
weapon definition index and native launcher bomb count to expose discrepancies
between the launcher and observed projectile list.
