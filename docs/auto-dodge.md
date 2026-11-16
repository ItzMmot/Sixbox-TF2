# Auto Dodge

Enable **Movement → Auto dodge → Auto dodge**. It is off by default. The toggle
and settings support the ordinary config and bind system, including hold and
toggle binds. Intended environment: private servers launched with `-insecure`.

| Setting | Default | Range |
| --- | --- | --- |
| Dodge prediction time | 0.8 s | 0.2–1.5 s |
| Dodge calculation budget | 4 ms | 1–16 ms |
| Dodge safety margin | 48 HU | 0–128 HU |
| Dodge jumping | On | On/off |
| Dodge crouching | On | On/off |
| Dodge placed stickies | On | On/off |

The controller anticipates direct hits and nearby blast zones, including armed
enemy stickies on surfaces. It compares the current input, stopping and eight
directions, with jump/duck variations. It preserves safe input and prefers lower
damage exposure, then clearance, then a smaller change to the current input.
Leaving an existing sticky danger zone is allowed. Similar alternatives retain
the preceding direction to reduce oscillation.

Dodge routes keep a 32 HU setback ahead of the player's horizontal bounds.
Ground support is checked along the entire projected route at intervals no
larger than 12 HU, including between fast simulation steps. The projection uses
the platform's height while jumping; a platform at the far end cannot hide a
pit in the middle. Automatic jumps are disabled when surrounding edge probes
within that setback lose support. Narrow bridges can still be traversed along
their axis; retreating from an existing edge remains possible.

After each alternative, continue native simulation with neutral movement for
up to one second to verify landing and braking beyond the prediction horizon.
Reject routes that drift off an edge, land dangerously or cannot settle in that
window. A player already airborne over a gap may steer back onto stable ground.
Terrain probes share the command's trace limits, with a 256-trace allowance
reserved from projectile/splash work and at most 768 terrain traces. Unknown or
unfinished terrain checks reject the alternative even after time-budget expiry.
These checks constrain projectile dodges; they do not replace manual movement
with an independent edge guard when there is no projectile threat. Future
knockback, disappearing platforms and server kill triggers are not predicted.

A dodge owns movement for one command. It cancels pending Rocket Jump, Trolldier
and SpyKnife movement plans and overrides ordinary movement helpers and NavBot.
The controller changes movement and jump/duck buttons; aiming, attack buttons,
weapon selection and other command fields are preserved. Existing aiming
features can still turn the view, with the dodge direction retained in world
coordinates. Manual jump/duck inputs remain available when the respective
automatic action is disabled. Automatic jumps are single grounded presses.

Supported classes include rockets and sentry rockets, pipes/cannonballs,
stickies, arrows/bolts, flares, jars, balls, cleavers and ordinary energy
projectiles. Spell and unknown custom projectile classes are excluded. World
collision prediction includes impacts, grenade bounces and sticky attachment;
other players and buildings can intercept projectiles. Blast visibility is
checked against world geometry and props.

The client often lacks an authoritative grenade fuse. An available reflected
client detonation timer is used; otherwise the grenade's predicted positions
are conservatively treated as possible blast locations throughout the horizon.
Remote sticky detonation is treated as possible at any time after arming.
This can produce early avoidance around pipes and sticky traps.

The forecast uses network origins, their age, outgoing latency and anticipated
command queuing. It advances the player with the preceding input during the
same outgoing delay, without adding rendered interpolation again. Actor motion,
future reflections, grenade angular state and server-side decisions can differ
from the forecast. No dodge is guaranteed.

Search is limited to the 32 most urgent threats, 2048 local movement ticks and
4096 explicit traces per command. The configurable calculation budget is shared
between the early and late checks; other helpers' execution time is excluded.
Up to one quarter is used to refine projectile collisions, reserving three
quarters for movement. Every selected threat first gets a complete conservative
forecast, so an interrupted refinement never discards the remaining barrage or
a previously completed forecast. Refinement runs in estimated time-to-danger
order, including network age, splash size, sticky arming and available fuse
times; closest distance breaks ties. Enumeration order cannot hide an urgent
projectile at the end of the entity group.

Unrefined impact projectiles can explode anywhere along their flight prefix.
Unrefined bouncing/attaching pipes use a reachable-volume bound. Their cover is
treated as unknown and exposed. These forecasts can overestimate danger; raising
the budget permits more collision/cover refinement. Completed refinements still
model world/actor impacts, bounces and blast visibility normally.

The budget is a time target, not a hard cutoff. Once threats are available,
complete the native baseline and a minimum escape comparison, even if the
target expires. Try the prior direction first, then a direction away from or
across the most urgent threat; try the opposite when the first escape cannot
improve the input. There are at most three minimum alternative attempts per
pass with supported starts; unsupported launch directions are rejected before
simulation. A stopping route gets a final minimum comparison if none of those
escapes improves the input. A changed late helper input can receive
one minimum late pass; unchanged input gets none. Optional alternatives stop at
the deadline. Unknown splash cover is conservatively exposed when no trace/time
allowance remains. Only complete route comparisons authorize movement; an
invalid simulation, unsafe terrain or lack of a better route can still preserve
the input. Native operations, the minimum pass and restoration can overrun the
selected budget. The trace count excludes internal engine movement traces.

Duplicate candidates are removed. Routes are checked in persistence/escape/input
preference order, stopping at a fully clear route. An unchanged late world
input reuses its earlier assessment, and latency preroll is computed once for
all routes. Segment/box distance uses piecewise quadratic minimization with
broad-phase rejection instead of iterative sampling. Probes restore full
player, clock, prediction and movement-cache state; opponent extrapolation is
unchanged.

Suspended during death, observer/non-walking movement, swimming, taunts, kart,
shield charge, stun, grappling, paused/demo/game-menu states and active
shift/warp/recharge/anti-warp. A dodge prevents a new doubletap from starting on
its command. Disabling the feature, respawning or changing player/map clears
the stored decision.

## Verification

Portable checks:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic tests/auto_dodge_test.cpp -o /tmp/auto-dodge-test
/tmp/auto-dodge-test
python3 tests/auto_dodge_movement_test.py
python3 tests/auto_dodge_controller_test.py
g++ -std=c++17 -O2 tests/auto_dodge_benchmark.cpp -o /tmp/auto-dodge-benchmark
/tmp/auto-dodge-benchmark
```

The Python checks compile production probe/controller methods against a fake
engine. They verify restoration, ownership, checkpoint reuse, minimum comparisons
after deadline expiry in movement/physics/splash, configurable refinement work,
mass fire with the urgent projectile last, conservative coverage after an
interrupted refinement, and changed/unchanged late input using injected operation
costs. Terrain regressions cover setback/retreat, a middle gap with a far-side
landing, fast crossings, narrow bridges, expiry during terrain checks and
post-horizon sliding into a pit. Geometry is checked against an independent high-precision
reference. The benchmark measures CPU geometry only. These checks do not
validate the Windows ABI or substitute for a TF2 session. Build the DLL directly
with the installed Wine/MSVC toolchain.

Private-server acceptance: rockets aimed at feet and adjacent walls; pipes
before/after a bounce; newly placed and armed sticky groups; simultaneous fire;
narrow passages and ledges; airborne movement; latency and choke; all competing
movement helpers; config reload and hold/toggle binds. Check that safe manual
input is retained, no attack or weapon selection is introduced by the dodge,
and disabling immediately releases movement.
