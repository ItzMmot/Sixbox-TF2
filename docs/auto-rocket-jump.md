# Auto rocket jump

Settings: **Misc → Main → Movement → Jumping**.

Enable **Auto rocket jump** using a bind to automate floor jumps. The existing
**Auto c-tap** mode keeps the player's own aim. The reload fix applies to both:
when a loaded launcher is busy reloading, an attack first interrupts the native
reload without jumping. The launch sequence waits for the weapon and owner fire
timers to permit a shot. An empty launcher finishes loading its first rocket
before interruption. Restored cooldowns are respected; a stalled reload wait
expires after five seconds. Disabling the feature, switching weapons, dying or
leaving a map resets its state.

This follows the native `ItemBusyFrame` reload interruption in
[Valve's TF2 weapon implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_weaponbase.cpp).
The feature does not edit reload or attack timers.

## Surface priority and boost modes

A validated floor shot always takes priority over a wall shot. Wall search runs
only if no usable floor plan exists; it cannot steal the first grounded command
while a floor launch waits for stable grounding. Surface normals distinguish
walkable floors/slopes from walls, including the rocket's actual first impact.

**Rocket jump mode** appears immediately below **Auto rocket jump**:

- **Diagonal** (default): the existing muzzle-derived offset behind movement,
  combining upward and forward push. It retains the legacy airborne speedshot
  timing. Auto c-tap continues to use the player's aim and its original timing.
- **Vertical**: ranks reachable blast points by their predicted upward impulse.
  Near-center floor explosions are preferred; the solver leads the future player
  position rather than aiming at the current feet while the player runs away.
- **Horizontal**: ranks blast points by forward impulse, including splash distance
  falloff. Floor shots boost along movement input, then existing horizontal
  velocity when no input is held, then view yaw at rest. Wall shots boost away
  from the wall along its normal. It does not select a backwards floor boost.

Directed floor modes test fifteen offsets (0–112 HU behind movement), refining
motion lead at most three times per offset. All candidates reuse one native
movement simulation and fixed-size snapshots. Wall modes sample nine impact
heights in each of the existing seven viewing directions. Every candidate must
have a clear muzzle path, an appropriate actual impact surface and reachable
splash. Directed modes crouch after firing and retain impact timing in the air;
they can use a floor blast before landing when it is within reach.

The scoring model projects normalized self-push toward the selected axis and
estimates ordinary blast falloff. It follows the player-center plus 10 HU direction
bias in [Valve's player damage implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/tf/tf_player.cpp).
It ranks this bounded set of reachable points; it does not guarantee an absolute
maximum velocity under arbitrary server plugins, force multipliers, changing
geometry or future air-control inputs. Vertical prioritizes added upward force;
it does not cancel horizontal momentum already carried by the player.

## Wall jumps

Enable **Wall rocket jumps** as well as **Auto rocket jump**. Face a nearby wall
(or keep it within 90 degrees of the viewing direction) and activate the normal
Auto rocket jump bind. **Wall search distance** defaults to 112 Hammer units and
ranges from 32 to 160. The wall option defaults to off.

The planner checks seven directions around the player; Diagonal uses foot height
and the directed modes also test higher/lower wall impacts. It traces
from the actual weapon muzzle, uses the launcher's projectile speed, rejects sky,
floors, ramps and blocked shots, and requires the predicted blast to reach the
player. Movement is estimated over at most ten server ticks, with gravity while
airborne and a swept player hull against the world. A twelve-unit blast margin
allows for the short movement estimate. It preserves world-space movement when
aiming the shot, then ducks after launch; a grounded jump is timed for impact.
Airborne wall shots keep their own impact timing. Floors are evaluated first;
wall shots serve as a fallback.

Stock launchers and their reskins, Direct Hit and Cow Mangler use the wall
planner. Beggar's Bazooka retains its existing load/release behavior and does not
use automatic wall shots. The planner searches local geometry; it does not plan
or steer an entire map rollout. Fast air control and moving geometry still need
in-game validation.

## Server behavior

The feature changes ordinary user-command attack, jump, duck and view-angle
inputs. It preserves native weapon cooldowns and introduces no extra packets.
Use the project's existing `-insecure` HvH setup. The wall option can be disabled
independently.

## Portable regression tests

Compile `tests/auto_rocket_jump_test.cpp` with a C++17 compiler, then run it.
It covers loaded and empty reloads, restored attack cooldowns, timeout/reset,
floor-before-wall selection (including pending floor launches), directional
boost ranking/falloff, invalid geometry, muzzle intersection roots and impact
timing for stock and Direct Hit rockets. These tests do not replace testing wall jumps inside TF2.
