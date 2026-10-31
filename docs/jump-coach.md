# Jump Coach

Enable **Misc → Main → Movement → Jump coach**. It is disabled by default and
works for ordinary airborne movement across classes; Soldier only is optional.
All settings participate in the existing visual/main config and bind system.

Jump Coach is an observer: it does not press buttons, select weapons, change
command angles, steer the player or send additional packets. It can be used
with Auto Rocket Jump and Auto Trolldier. The HUD identifies automatic movement;
when Auto RJ owns a launch, its rocket advice yields to that launch.

## Reading the display

- **Solid path:** estimated movement if the current forward/side input and yaw
  stay fixed. It is recalculated every command; future mouse movements are not
  extrapolated.
- **Dashed path:** a reference path that continually applies the indicated
  optimal A/D strafe. This maximizes immediate horizontal speed gain, not route
  progress or distance from obstacles. It does not select a destination.
- **Landing ring:** estimated floor contact and approximate hull footprint. It
  is drawn only after a standable collision or ground probe; a truncated path
  does not produce a fictional landing marker.
- **Speed / Vz:** observed horizontal and vertical speed in Source HU/s.
- **Launch:** horizontal speed change from the last observed grounded speed
  (or the first airborne sample if enabled mid-flight).
- **Peak loss:** observed loss from the peak XY speed of the current jump.
- **Gain efficiency:** smoothed ratio of this command's modelled air-speed gain
  to the theoretical best gain. It measures input quality, not measured server
  accuracy. Braking or coasting gives zero. It is not a route-quality score.
- **Strafe A/D / camera bar:** the recommended side and yaw alignment for that
  side, without W/S. The bar's centre is aligned; positive Source yaw is left.
  Aligning the camera may briefly require an adjustment opposite the strafe key.
- **Landing time / distance / speed:** estimated time and horizontal travel to
  floor contact and speed at contact, before subsequent grounded friction.

On the ground, the HUD shows current speed and prompts a jump. Only in air hides
it until takeoff. The normal launch/jump impulse is taken from the observed
velocity rather than invented while grounded. HUD position is a percentage of
the screen, and its scale/colour are configurable. The existing custom client
font is used; long labels are ellipsized without creating new font sizes per
frame. World lines obey depth unless Path through walls is enabled.

## Rocket timing

For an active Soldier rocket launcher, the advisor searches up to fifteen future
fire commands near the estimated landing. It uses the launcher's projectile
speed and muzzle offset, refines the downward aim, checks muzzle clearance and
actual floor impact, interpolates the predicted player hull at impact, checks
self-blast proximity/visibility and rejects non-positive boost directions.
Current projectile-collidable players/buildings can obstruct the shot; they are
not extrapolated into the future.

The floor target respects the existing **Rocket jump mode**: Vertical uses the
landing area, while Diagonal/Horizontal offset behind movement for forward push.
The floor cross marks the selected impact point. The default desired impact is
30 ms before landing, adjustable with Impact before landing. This is a coaching
window, not a promise of an exact server-side boost or optimal damage.

- **Rocket in ...:** prepare to aim at the marker; re-evaluate the timer as the
  path changes.
- **Aim ... window now:** the timing fits, but the visible camera is more than
  six degrees from the marker. It does not tell you to shoot elsewhere.
- **Shoot now:** the estimated fire command is current and the camera is near
  the marker. The actual command still belongs to the user/other modules.
- **Cooldown / load / interrupt reload:** weapon state prevents a useful
  immediate shot. An empty magazine does not receive a fabricated shell-loading
  deadline. A loaded but reloading weapon asks for an interruption first.
- **No shot window:** no tested floor shot matched the timing and clearance
  margins. It does not mean every possible wall/floor jump is impossible.

The advisor supports ordinary straight rockets, including attribute-modified
speed and uncharged Cow Mangler primary shots. Beggar's load/release bursts,
charged Cow Mangler shots, weapon deployment delays and wall-shot planning are
outside this version. Holding another weapon prompts equipping the launcher;
it does not swap automatically. Advice is suppressed while Auto RJ owns a launch.

Rockets already in flight and future blast impulses are not added to the flight
path. Once a real blast changes local velocity, the next command recalculates
from it. This keeps an optimistic hypothetical boost from masquerading as an
observed one. Likewise, future crouch transitions/double jumps/airblasts and
moving-platform motion are not simulated.

## Prediction and limits

The portable solver uses Source-style capped air acceleration with uncapped
wishspeed in the acceleration term, two gravity half-steps, up to four hull-sweep
bumps per tick, clipping against contact planes and two-HU ground categorization.
Surface friction, air-control attributes, class maximum wishspeed, gravity and
server tick interval are sampled on the game thread. The hull uses the existing
small origin-compression margin to avoid false start-solid contacts.

Prediction is an approximation on current world/prop geometry; players and
buildings are not movement obstacles in the flight model. Stair stepping,
future moving objects, water/ladder/charge/parachute/grapple/air-current/rocketpack
movement and special server forces are not modelled. Unsupported movement states
hide the coach. A pause, death, disconnect or map shutdown clears its state.
The horizon is configurable from 0.25–5 seconds, capped at 512 simulated ticks.
The display uses the effective horizon if the tick cap is reached.

The flight trace budget is shared between solid and dashed paths. Rocket timing
has its own bounded candidate checks. Buffers and callback state use fixed arrays;
no per-tick heap container or native entity-mutation movement simulation is used.
Render passes consume copied snapshots through an indicator cache, never live
player entities. Stale snapshots expire after 250 ms. World geometry renders in
the normal Source effects hook; the HUD follows the existing screenshot/private
capture-overlay policy. TEXTMODE excludes the feature.

Source references:
[air acceleration and collision movement](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp),
[TF2 air-control modifiers](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_gamemovement.cpp).
The implementation is a bounded snapshot model, not a claim of bit-identical
native movement prediction.

## Validation

    c++ -std=c++20 -O2 -Wall -Wextra -Werror tests/jump_coach_test.cpp -o /tmp/jump-coach-test
    /tmp/jump-coach-test

Tests cover analytic ballistic motion, flat/sloped landings, displaced hull feet,
wall/corner sliding, ceilings, invalid traces, sky rejection, trace/horizon caps,
time interpolation, optimal strafe gain against a dense angular search, mirrored
A/D guidance, shot-time bounds and zero allocations in the portable solve.
C++ integration and TEXTMODE AVX2 checks are separate from in-game validation.

A useful live check is a local jump map: enable the coach, leave Only in air off,
launch with Auto RJ or manually, release W/S and follow an A/D yaw cue. Compare
solid versus dashed paths, the landing ring and the actual landing. Test stock
and Direct Hit cooldowns, a loaded/empty reload, map changes and screenshots.
