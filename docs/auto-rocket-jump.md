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

## Wall jumps

Enable **Wall rocket jumps** as well as **Auto rocket jump**. Face a nearby wall
(or keep it within 90 degrees of the viewing direction) and activate the normal
Auto rocket jump bind. **Wall search distance** defaults to 112 Hammer units and
ranges from 32 to 160. The wall option defaults to off.

The planner checks seven directions around the player at foot height. It traces
from the actual weapon muzzle, uses the launcher's projectile speed, rejects sky,
floors, ramps and blocked shots, and requires the predicted blast to reach the
player. Movement is estimated over at most ten server ticks, with gravity while
airborne and a swept player hull against the world. A twelve-unit blast margin
allows for the short movement estimate. It preserves world-space movement when
aiming the shot, then ducks after launch; a grounded jump is timed for impact.
Airborne wall shots keep their own impact timing. If no wall qualifies, the
existing floor-jump planner remains available.

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
invalid geometry, muzzle intersection roots and impact timing for stock and
Direct Hit rockets. These tests do not replace testing wall jumps inside TF2.
