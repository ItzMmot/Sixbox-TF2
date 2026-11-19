# Auto Sticky Jump

Auto Sticky Jump is a Demoman movement feature under **Misc → Main → Movement**. It uses
stickies you place yourself and follows your current movement to time a
crouch jump and detonation. Keep moving and aiming manually during the attempt.

## Controls

- Enable **Auto Sticky Jump** and assign **Sticky jump start / cancel**.
  Place your stickies with the supported launcher, release fire and jump, then
  press the key once to start. Press it again to cancel. Holding the key starts
  one attempt.
- **Horizontal**, **Diagonal**, and **Vertical** request a blast velocity
  increment at 20, 45, and 90 degrees above the ground respectively. The
  horizontal heading comes from the camera when the attempt starts.
- **Auto** chooses the preset from the camera's elevation above the horizon on
  the command that starts the jump. At +70 degrees or above it uses Vertical,
  above +15 and below +70 it uses Diagonal, and at +15 or below it uses
  Horizontal. The chosen preset stays fixed through detonation. If you are
  already airborne, Auto uses the elevation on the detonation command.
- **Power** requests a percentage of the largest boost sampled in that
  direction near the existing stickies. At zero power the attempt stays idle.
- **Sticky jump status** shows the target, predicted boost, and observed
  boost in HU/s, plus the current Auto preset and camera elevation in Auto.
  **Sticky jump markers** draws the takeoff marker.

Stock launchers and their reskins, Sticky Jumper, and Quickiebomb are supported.
Scottish Resistance uses view-selective detonation and is unsupported. All
existing stickies associated with the active launcher may detonate, including
distant traps.

## Launch timing

The controller samples local launch geometry around the existing stickies to
normalize power independently of held movement. It freezes the target boost
once the search completes and checks current manual movement for an immediate
detonation window or for a crouch jump with a detonation 2–16 ticks later.
Grounded preflight alternates with immediate checks. Airborne checks use the
current movement and supply crouch and detonation when the window matches.

Planning resumes across commands in cooperative 1 ms slices. Individual engine
calls can exceed the slice. An attempt has an 8-second monotonic timeout.
Changes in walking input or the bomb set feed into live predictions. During
a planned jump, crouch stays held while the controller waits for a detonation
window. A change of weapon, manual fire or jump, and Auto Dodge can cancel an
attempt.

Live probes use scoped movement prediction and a private projectile simulation
to check arming, blast visibility, boost and clearance. Each probe restores
player state, clocks and movement caches. A valid boost is within
`max(20 HU/s, target * 0.15)` of the target magnitude and within 15 degrees
of the requested direction. Reload clocks do not prevent detonation when the
launcher permits secondary attack. The controller sends detonation once and
checks for projectile disappearance and a velocity boost.

## Physics reference

The self-jump model was checked against Valve's public TF2 source:

- [tf_weaponbase_grenadeproj.cpp](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_weaponbase_grenadeproj.cpp): separate self-jump radius.
- [tf_gamerules.cpp](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_gamerules.cpp): base weapon damage restoration, hull radius test, player distance falloff and self damage scaling.
- [tf_player.cpp](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/tf/tf_player.cpp): hull-volume force scaling, crouched force hull, upward direction offset, per-explosion cap and self-push attribute.
- [tf_weapon_grenade_pipebomb.cpp](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_weapon_grenade_pipebomb.cpp) and [tf_weapon_pipebomblauncher.cpp](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_weapon_pipebomblauncher.cpp): native arming and global detonation semantics.

Public SDK behavior must still be checked against the installed game/server
version. The observed boost readout compensates elapsed gravity from the
no-blast baseline; movement/collisions and network corrections during the
confirmation interval can affect that estimate.

## Verification

```sh
g++ -std=c++17 -O2 -Wall -Wextra -pedantic tests/auto_sticky_jump_test.cpp -o /tmp/auto_sticky_jump_test
/tmp/auto_sticky_jump_test
python3 tests/auto_sticky_jump_world_test.py
```

In a private server, check all three directions and Auto with manually placed stickies,
including while walking or airborne. Verify the arming delay, cancellation,
movement and aim preservation, and the status and takeoff marker toggles.
