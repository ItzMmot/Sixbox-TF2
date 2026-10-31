# Aura: continuous model coating

**Visuals → ESP → Aura** renders animated fire, smoke and other procedural
textures over and around the actual model silhouette. Select an ESP group,
enable that group in Active groups, choose its Targets, then enable Aura.

The renderer uses a mask of the model's rendered geometry. It replaces the
previous bone-emitted billboard implementation: there are no particle sprites,
spawn points or particle counts. The approach was informed by BeautifulHands
and HandFusion in the two user-provided Deadcode archives. This is a separate
Direct3D 9 implementation for TF2, not a Java module or a direct GLSL port.

For first-person models, use **Viewmodel hands** and **Viewmodel weapon** targets.
Separate groups allow different effects on each. Player groups also cover their
world weapons and cosmetics. Other existing targets include health, ammo,
powerups, objectives and projectiles. Arbitrary map geometry and models the
engine never submits for drawing are not added to the mask.

## Appearance

Modes: Fire, Hologram, Embers, Plasma, Lightning, Aurora, Frost, Circuit, Hex shield,
Vortex, Radar, Digital rain, Liquid metal and Rainbow. Liquid metal defaults to
alpha blending; other modes default to additive blending. Additive blend can be
overridden.

The catalogue contains different pattern families, not just palettes: Hologram
has scan lines and dropouts; Circuit has right-angle traces and travelling nodes;
Hex shield has pulsating hexagonal panels; Radar combines range rings and a
rotating beam; Digital rain has falling glyph columns; Liquid metal uses
contour-derived normals and moving reflection bands (not actual scene reflections).
Lightning has jagged, intermittently rerouted bolts; Vortex has rotating spirals.

Coverage selects **Full** (surface plus surrounding coating), **Outline** (outer
coating, transparent interior), or **Fill** (texture only inside the silhouette).
The original model is still drawn normally underneath the effect.

| Setting | Effect |
| --- | --- |
| Scale / Coating thickness | Overall coating radius; thickness is in screen pixels |
| Width / Height | Horizontal and vertical spread of the softened contour |
| Velocity | Procedural flow and upward distortion speed; no particle physics |
| Surface fill / Outer coating | Interior and surrounding layer strengths |
| Edge softness | Feathering of the outer layer |
| Opacity / Intensity / Soft glow | Transparency, brightness and soft contour halo |
| Turbulence / Noise scale / Animation speed | Procedural texture deformation and evolution |
| Wind strength / Wind speed / Wind direction | Oscillating screen-space flow and its direction |
| Movement response | Coating displacement against averaged model-owner velocity |
| Camera sway | Response to yaw/pitch changes, with angle wrapping |
| Motion smoothing | Time constant of the frame-rate-independent motion filter |
| Flicker / Pulse / Swirl | Brightness variation and procedural swirling |
| Maximum distance | World model range in Source HU; excludes first-person models |
| Quality | Three, four or five noise octaves |
| Visible sources only | Shared scene-depth testing, with a source-visibility fallback when depth cannot be shared |
| Preset colors / Use group color | Mode palette or the group's base ESP color; group color wins |
| Core color / Edge color | Custom colors and alpha when both overrides are off |

For the most obvious initial comparison with BeautifulHands, use Aurora or Fire,
Coverage Full, Coating thickness 20–30, Surface fill 0.3–0.5 and Outer coating 1.
For a pure smoky border, use Outline and increase Edge softness. Set Animation
speed to zero to freeze procedural animation; motion response remains separate.

All settings are stored in main and visual configs. Old configs without Aura
leave it disabled. Settings shared with the old particle version are retained;
its particle-only emission, lifetime, spread, following and count keys are no
longer used. New controls fall back to their defaults.

`ModeCatalog: 2` identifies the revised catalogue in saved configs. Loading a
legacy config maps Smoke, Nebula, Toxic mist and Ink to Aurora, Solar to Fire,
and Spirit to Lightning; all other selections stay unchanged. This prevents an
old numeric Smoke selection from unexpectedly becoming Hologram. Explicit new
Hologram/Circuit/etc. selections are preserved on the next load.

## Pipeline and practical limits

Models are redrawn into eight full-resolution, reusable mask textures with an
unlit white override. The mask preserves the geometry's silhouette and gaps;
it does not preserve alpha cutouts from the model's original material texture.
The translucent mask material leaves scene depth unchanged. A visible mask uses
shared depth testing when the scene supplies compatible non-MSAA depth. If depth
cannot be shared (including MSAA), a world-only visibility trace filters sources
instead. First-person masks use their current viewmodel projection.

A fixed pool of 128 reference-counted commands transfers mask texture handles
and plain settings through Source's material queue. The callback acquires the
native D3D texture while its render target is bound, after all models in a layer
have been drawn. Standalone/MSAA targets are resolved into a reusable GPU texture.
This works with immediate
and queued rendering without forcing the material queue to flush per model.
Pending slots cannot be reused until the queue releases them. The Direct3D pass
never dereferences game entities or bones. Code-owned frame storage is fixed;
there are no per-frame heap allocations for source lists or command objects.

At Present, two half-resolution Gaussian passes produce a soft contour field.
The selected procedural shader samples both the sharp mask and displaced soft
field, fills the silhouette, and forms a wind-distorted outer coating. All
sixteen programs (fourteen appearances and two blur variants) are embedded as
SM3 bytecode; no runtime D3DCompiler library is needed.

The limit is **eight active Aura groups**, each merging its selected models into
one mask. First-person groups take priority, then existing group order. Models
in a merged group share its palette and averaged movement response; this is not
an individual fluid simulation for every model. More groups and high resolution
increase GPU memory and rendering cost. Eight RGBA masks use approximately
63 MiB at 1920×1080, plus two half-resolution blur textures and driver overhead.

Visible sources only affects mask creation. Its trace fallback checks the source
center and does not clip partially hidden models pixel by pixel. The final surrounding coating is a
screen-space effect, so it may overlap adjacent foreground edges. A geometry
submission behind a wall is required even when depth testing is disabled; Aura
does not force otherwise culled models to render. There is no scene refraction
or temporal smoke-trail simulation.

Direct3D state, render targets, viewport and depth attachment are restored after
the pass. Stale frames, epoch changes, map/material reload, resolution changes,
device reset and unload discard outdated masks or release resources. Screenshots,
Anti-OBS, the engine UI and the separate camera view suppress the effect.
TEXTMODE excludes rendering. The feature changes no packets or rotations.

## Maintenance and checks

`AuraShader.h` contains the readable HLSL; `AuraBytecode.h` embeds the compiled
variants. After an HLSL change, regenerate using system D3DCompiler 47 on Windows:

    python .github/scripts/build-aura-shaders.py

Both CI build paths validate the source digest and SM3 program headers:

    python .github/scripts/build-aura-shaders.py --check

Portable control, allocation and concurrent command-slot checks:

    c++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread tests/aura_test.cpp -o /tmp/aura-test
    /tmp/aura-test

The actual embedded shaders also have a Windows D3D9 render test, run in a hidden
window. In a Visual Studio developer prompt, with a graphical session available:

    cl /nologo /std:c++20 /EHsc /O2 /MD tests/aura_shader_test.cpp /Fe:aura-shader-test.exe /link d3d9.lib user32.lib
    aura-shader-test.exe

Optionally pass an existing output directory to save fourteen TGA previews.
The test uses synthetic geometry masks and checks surface/outer coverage, Fill
and Outline isolation, opacity zero, motion response, frozen animation and an
empty mask. It also exercises the production native-texture capture helper,
standalone/MSAA surface resolves and storage reuse. The shader checks passed
under Wine/D3D9 with all sixteen embedded shaders.
They do not validate TF2's live model-capture path, appearance or performance.

The render-pass flags and MSAA depth constraint follow Valve’s public
[studio-render interface](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/istudiorender.h)
and [shader API](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/shaderapi/ishaderapi.h).
