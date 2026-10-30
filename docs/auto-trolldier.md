# Auto Trolldier

Enable **Misc → Movement → Auto trolldier**. The feature and its automation
checkboxes are off by default and support the usual bind/config system.

Do a rocket jump manually or with Auto Rocket Jump. **Auto Market Gardener swap**
switches from your active rocket launcher to a real Market Gardener after the
blast jump begins and Auto Rocket Jump finishes its launch sequence. It remembers
the launcher and returns to it after one resolved swing. A confirmed shovel
`player_hurt` event identifies a hit; an observed completed windup without damage
confirmation returns after a bounded latency-aware wait. A denied or stale swing
request also times out. A confirmed hit is recognized even when a slow frame
skips all positive windup samples; return still waits for the weapon to clear
its windup. Rewinding the local time base cancels stale swing tracking.
It never treats pressing Attack as proof of a hit and
never switches back during a live windup. If no swing starts, landing returns the
launcher. One cycle runs per blast jump; explicit weapon selection cancels the
automatic cycle. Without this checkbox, switch to the Gardener yourself.

**Auto aimbot** temporarily applies the currently enabled aim mode, or the first
applicable enabled key bind's non-Off aim mode when aim is Off. If none exists it
uses Silent. It temporarily enables Auto shoot and Players, and removes the
camera FOV restriction for this garden strike. This supports both an Off→Silent
bind and a separate Auto shoot bind. It restores every effective setting after
the melee adapter returns; config maps, physical keys and bind toggle states are
untouched. With the checkbox off, normal melee aim, target filters and Auto shoot
settings apply to the swing. Planning, steering and flight rendering operate even
when aim is Off, so flight control is independent of manually holding an aim key.

The planner tries passes on both sides of the victim. It accounts for both player
hulls, rejects paths that overlap the victim's body/head, and prefers a longer
continuous melee contact window for the selected target. The chosen side is
retained between commands unless the other side offers a materially better
window or the retained path becomes blocked. Once the swing starts, its target
is reserved until the impact window finishes; a lost/dead victim does not redirect
that swing to another enemy. Final native melee
traces still validate the swing. Cosmetic inventory substitutions do not replace
the required server weapon (Soldier melee item definition 416).

Changing weapon, landing, losing a reachable target or disabling the bind releases
flight control. The swap cycle can finish waiting for an already-started swing
after landing. **Cancel on movement input** releases control while WASD is held.

## Settings

| Setting | Default | Purpose |
| --- | --- | --- |
| Auto Market Gardener swap | Off | Switch to MG after launch, then return to the remembered launcher |
| Auto aimbot | Off | Temporarily enable aim and Auto shoot for the garden strike |
| Target distance | 1400 HU | Initial candidate radius |
| Flight prediction | 0.9 s | Adjustable up to 5 s, capped at 512 Ticks |
| Intercept candidates | 4 | Closest enemies to evaluate, maximum four |
| Flight trace budget | 2048 | Maximum planner traces per command, shared fairly across candidates |
| Cancel on movement input | Off | Allow WASD to interrupt steering |
| Flight rotations | Off | Off, Plain, Silent, Legit, Legit silent |
| Flight rotation speed | 25 | Speed multiplier for Legit modes, like NavBot |
| Draw strafe path | On | Draw the predicted flight up to the intercept marker |
| Strafe path color | Indigo | Path and intercept marker color |
| Strafe path through walls | Off | Disable depth testing for this path |

The collision budget excludes at most four preliminary latency projection traces
and the final melee ray/hull checks. A low budget can leave a reachable path
unexamined and shorten the effective horizon. For 3–5 s horizons with four
candidates, increase Flight trace budget (8192 is a starting point); its maximum
is 16384. The default budget remains 2048. All planner buffers are fixed arrays; the planner adds no per-command
heap allocations. Physics snapshots, traces, entity validation and command writes
run synchronously on the game thread.

## Limits and interactions

The planner uses Source air acceleration (including the projected air-speed cap),
gravity, weapon range attributes, world hull traces and a short latency lead when
Swing account ping is enabled. It replans each command. It predicts enemies from
their current velocity; sudden turns, moving platforms, unmodeled collisions with other players/buildings and
server-specific movement can invalidate a plan. World collisions on the local
path are rejected conservatively. It does not plan wall slides or rocket jumps.

Parachutes, water, ladders, taunts, ghosts, stuns, grappling hooks, base velocity,
active tick shifting, warp and recharge suspend the module. While steering owns
a command, automatic doubletap, navigation movement and post-prediction movement
helpers do not replace it. With Auto aimbot off, the selected aim mode and angle restrictions still
apply, so restrictive or slow aim settings can prevent a swing.

Flight rotations have the same five mode names as NavBot. Plain follows the
flight direction immediately; Legit uses NavBot's angular speed and acceleration
limits with separate tracking state. Silent variants change command angles while
leaving the visible camera free. They follow this module's path and never select
an independent enemy. The melee aimbot takes priority whenever strike validation
succeeds; the selected melee aim mode controls the actual swing. Anti-aim yields
while flight rotations are enabled and Auto Trolldier controls the command.

The path uses a fixed snapshot cache, cleared on cancellation/map change and
expired after 250 ms without a new plan. Rendering reads this snapshot without
looking up or simulating entities. It respects screenshot handling, the renderer's
line budget and the selected depth setting. Rendering is omitted in TEXTMODE.

## Server impact

Intended for the project's `-insecure` HvH/test servers. This changes normal
`CUserCmd` movement and aim, and may set `IN_ATTACK`; it does not add network
messages, increase command frequency, force critical hits or change server weapon
state. A Gardener critical hit still depends on the server's blast-jump state.
Movement/aim checks may flag automated steering or abrupt aim. The kill switch is
the Enabled setting/bind; default is off.

## Validation

Run the portable planner regression test from the repository root:

`g++ -std=c++20 -O2 -Wall -Wextra -pedantic tests/auto_trolldier_test.cpp -o /tmp/auto-trolldier-test`

Then run `/tmp/auto-trolldier-test`. It covers Source acceleration, ballistic
height, stationary and moving intercepts, unreachable targets, target retention,
wall/landing rejection, bounded trace counts, invalid input, allocation-free
solves and rotation wrap/speed/acceleration/convergence checks. Swing completion/hit/timeout tracking and scoped aim restoration are also tested.
These tests do not
establish live-server hit reliability. In-game checks
must cover ascending/descending jumps, crouching, input interruption, weapon
switches, target death and latency with other movement features enabled.

Physics reference: Valve's [Source movement implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp)
and [TF movement overrides](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_gamemovement.cpp).
