# Sandevistan

Sandevistan settings occupy the left World column; Karaoke occupies the right column. On narrow windows the columns stack.

`Visuals → World → Sandevistan` creates a colored afterimage trail on warp and doubletap. Disabled by default. All controls are stored in normal visual configs.

- **Start color / End color:** oldest to newest sampled pose, including alpha. Reverse gradient changes direction; gradient bias shifts the transition. Fading is independent of color rank.
- **World copies / Trail lifetime / opacity:** up to 24 poses per burst, 0.15–3 seconds. Fade in, fade curve and lifetime stagger control how they disappear.
- **Warp copy spacing:** sets minimum spacing for short trails without discarding poses. Stationary copy spread adds separation based on the body width; stationary doubletap produces a symmetric lateral fan. Long moving trails retain their captured positions. Upward drift and copy scale add motion and expansion.
- **Copy material:** Flat, Additive or Wireframe. Copies through walls is optional. Hide near camera tests the entire scaled and offset body hull against the actual rendering camera, with a minimum 4-HU margin. This also hides a predicted shot pose that would surround the camera.
- **Copy draw budget:** caps world model draws across overlapping trails. Fixed buffers retain at most 8 events and 96 poses.
- **First person echo:** offset copies of the current hands/weapon pose, with independent count, spread and opacity. Direction follows smoothed camera angular velocity and lateral/vertical player movement; there is no constant leftward offset. These are visual echoes, not historical weapon animation snapshots.
- **Speed lines:** optional short accents along screen edges, with count and strength controls.

A burst requires at least two distinct generated commands; recharge and ordinary movement do not trigger it. Positions and angles are recorded while the generated command's predicted state is active, before EnginePrediction restores it. Bone evaluation no longer runs inside prediction. At render time, the normal entity pipeline prepares a body draw; its live seed draw is suppressed and its palette is frozen separately at each recorded transform. Old copies retain that frozen skeleton until expiry. Re-prediction is deduplicated; sampling retains both trajectory endpoints when at least two copies are requested. The world trail captures the player's body model; separate wearable/weapon entities are not copied.

History clears on level shutdown, death, disconnect, class/model changes and disabling the module. Clean screenshots suppress the effect. Each copy owns separate draw matrices and bones so deferred rendering cannot collapse the trail onto the last scratch buffer. Materials and render overrides are restored after drawing. No movement, tick scheduling or attack logic is changed.

Policy tests:

```sh
g++ -std=c++17 -O2 tests/sandevistan_test.cpp -o /tmp/sandevistan-test
/tmp/sandevistan-test
```

The body trail was confirmed working in-game after the render-palette fix. Compatibility across every chams/viewmodel combination has not been exhaustively tested.

The body pass follows the [Source SDK entity model pipeline](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_baseanimating.cpp) to obtain valid studio/hardware/LOD state rather than constructing it manually.

Runtime counters are written at most once per second while the trail is active to `6box/Core/sandevistan.log` (bounded to 1 MiB). They distinguish captured positions, body seed calls, successful seed callbacks, emitted copies and camera culling. They do not contain config contents or API keys.
