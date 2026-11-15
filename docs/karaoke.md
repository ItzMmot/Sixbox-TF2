# Karaoke

**Visuals → World → Karaoke.** Detects Spotify and YouTube Music through the current Linux desktop's MPRIS media session. Designed for SixBox running under Steam/Proton; this bridge does not implement Windows SMTC.

## Setup

Run once from the repository:

```sh
# scripts/karaoke/install.py — Linux / Proton setup
python3 scripts/karaoke/install.py
```

This installs a user service, `sixbox-karaoke.service`; no root or listening network port is needed. Fedora needs the system package `python3-dbus`. The existing local DLL build command updates an already installed bridge when its source changes. The bridge stays idle when Karaoke is disabled or the client disconnects.

Enable Karaoke, select a media source and the desired outputs. Open ClickGUI to drag the HUD header, use previous/play/pause/next/repeat or click/drag the playback bar. Controls follow the capabilities reported by the player. Repeat cycles Off / Track / List where supported. Browser players must expose an MPRIS session and playback position; YouTube Music's desktop wrappers normally do. A browser may expose another active media tab instead, so select the appropriate source and close competing playback tabs if needed.

## Providers and keys

- **LRCLIB** is the default and needs no key. Timed LRC is preferred; plain text remains plain text if timings are unavailable.
- **Spicy Lyrics** is optional. Open [the developer dashboard](https://developers.spicylyrics.org/dashboard/applications), create your application and obtain its key. For a native-client publishable key, enable access for requests without an Origin header as explained in [the key guide](https://developers.spicylyrics.org/docs/keys). Paste it into the password field under the Spicy provider. A personal local-backend key also works. Syllable highlighting uses the provider's actual timings; tracks may offer only line timings or static lyrics.
- **Musixmatch** is the final fallback and requires your own [developer key](https://developer.musixmatch.com/signup). Availability of full/timed lyrics depends on the key's API plan. No shared or scraped token is bundled.

Fallback order is **Spicy → LRCLIB → Musixmatch**, or **LRCLIB → Musixmatch** with the default provider. Failure of all available providers appears in the HUD. Instrumental recordings are handled without manufacturing lyrics. Provider/uploader attribution and supplied copyright remain visible where required.

Keys are saved in the usual SixBox config as requested. **Remove keys before sharing a config.** Local protocol files are in `%LOCALAPPDATA%\SixBox\Karaoke` inside the TF2 Proton prefix, protected by a private directory and restrictive Linux file permissions. Metadata is sent only to the selected lyrics services and fallbacks. Song history and API keys are not printed into service logs.

## Appearance

Outputs are independent flags: **2D text**, **3D text**, **HUD**. Text settings include font, size, line spacing, context, timing offset, scrolling smoothness, active-line emphasis, colors, opacity, glow and shadow. Retired lines can darken, fade or fall; distance and duration control the fall animation. Plain lyrics scroll smoothly according to the track progress (using the existing scroll smoothing setting) and remain marked as estimated, without inventing provider timestamps. Unknown duration or position keeps manual scrolling available.

3D text can follow a camera-centered arc: each letter turns toward the center, and long lines fit within the placement field of view. The wrap checkbox and maximum angle are under Karaoke 3D. Radius and angle limit freeze with Stay in world, so moving/zooming later does not reshape anchored lines. Flat placement remains available by disabling wrapping. Place it in front of your view, adjust distance/height/scale, optionally follow the camera, hide it behind world geometry, or clip falling lines at the ground. Fixed placement is reset on map transitions. Occlusion uses a trace to the text plane's center rather than per-glyph depth tests.

HUD materials: solid, **frosted glass**, **liquid glass**. Glass captures the game image and applies a two-pass Gaussian blur; liquid mode adds lens refraction, magnification and chromatic separation. Tint, opacity, grain, edge lighting and rounding are adjustable. This is an optical effect, not a translucent rectangle. It requires DX9 Shader Model 3; if resources cannot be created the HUD retains its tinted background. Glass adds full-frame capture and two half-resolution render passes, so lower blur or use the solid material on slow hardware.

The shaders and render-target flow adapt the MIT-licensed work of [Ali A. Jafari](https://github.com/aliajafari/Liquid-Glass-with-WebGL) and [Eray](https://github.com/3r4y/imgui-blur-effect). Original source, licenses and adaptation notes are included in `SixBox/include/ThirdParty/KaraokeGlass/`.

## Volumetric letters, text glass and letter crumble

**Karaoke 3D → Volumetric 3D letters** builds actual triangulated front/back caps and side walls from the selected TTF/OTF font outlines, including counters/holes. Adjust extrusion depth (world units), outline detail, yaw/pitch/roll, side color, shading/specular and the light direction. The flat world-space mode remains available by disabling volume. **Stay in world** lets each newly visible line smoothly follow you for 0.75 seconds, then freezes its position and orientation independently until it leaves the visible context or crumbles away. It overrides Follow view; frozen lines do not keep scrolling with the player. Changing tracks/maps or pressing Place lyrics resets these placements. Geometry is cached on font/lyrics/detail changes; transformed triangles are culled and sorted by view depth. A bounded geometry budget falls back to flat glyphs for excess geometry. World occlusion remains the existing center trace, rather than access to the game's depth buffer.

Contour extraction uses the bundled `stb_truetype`; polygon triangulation uses the ISC-licensed [Mapbox earcut.hpp](https://github.com/mapbox/earcut.hpp), pinned under `SixBox/include/ThirdParty/Earcut/` with its license. Color bitmap glyphs with no vector outline fall back to flat glyphs.

**Karaoke text glass → Lyric text material** is independent of **Karaoke HUD → Karaoke HUD material**. Each can be solid, frosted or liquid. Text glass applies to the letter silhouettes in 2D/HUD and to the actual 3D faces/normals; it does not draw a glass rectangle behind each letter. Text blur, refraction, chromatic separation, frost, edge light, tint strength and opacity have independent controls. Enabling both glass materials adds separate scene/blur resources so their settings do not overwrite each other.

Select **Karaoke text → Old lyric effect → Letter crumble** for individual falling letters. Customize order (left/right/random), stagger, duration, fall distance, spread, spin, depth drift and gravity. Combining marks share their letter's motion and pivot. Positions are derived from playback time: pausing freezes the animation and seeking rewinds it. The stagger span is capped at three seconds per line. Increase visible surrounding lines when combining long crumble animations with fast lyrics; the context setting still bounds the visible history. Fall distance is measured in pixels for 2D/HUD and world units for 3D, with optional ground clipping.

## Breakcore visualizer and notifications

Detailed errors remain visible for five seconds and fade during the next second. The compact status retains the full message as a hover tooltip. Polling the same error does not reopen it; a new track/error does.

Under **Karaoke visualizer**, enable the visualizer and choose **Always** (alongside lyrics) or **No text** (only when no lyrics are available). Adjust height, sensitivity, drum response, glow, particles, bass pulse and secondary color. The display combines 64 logarithmic spectral bands, stereo ribbons, peak markers, reflected bars and transient-driven sparks/halos. Fast attacks and short releases follow broken drum patterns without assuming a fixed BPM.

Audio comes **only from the selected player's isolated sink-input stream** through PulseAudio / PipeWire's PulseAudio compatibility layer. The monitor is always filtered to that input. The module never records the microphone, mixed system output, all browser audio or another player's stream. A separate audio worker analyzes a 2048-sample Hann window at 48 kHz with a 512-sample hop and publishes bounded summaries up to 60 times per second. FFT/PCM capture and file I/O run outside the game/render threads; raw audio is not stored or sent to lyrics providers.

Linux dependencies: `parec`, `pactl` (Fedora: `pulseaudio-utils`) and `python3-numpy`. Missing optional audio dependencies do not disable lyrics. The existing build/install command also updates the audio helper.

**Browser limitation:** a shared Firefox/Chromium stream without a reliable per-tab song label cannot be identified safely. In that case the HUD reports that the music tab cannot be isolated and keeps the visualizer silent. Dedicated Spotify / YouTube Music applications can be matched to their own streams. Native PipeWire streams inherit metadata from their exact client; Flatpak identity is matched through the portal instance and host cgroup, including the MPRIS D-Bus proxy. This handles sandbox PIDs without broadening capture to other players. Multiple ambiguous matches are rejected. There is deliberately no fallback to the desktop mix.

Ownership metadata follows [PipeWire client properties](https://docs.pipewire.org/page_man_pipewire-props_7.html).

Analysis uses NumPy's [real FFT](https://numpy.org/doc/stable/reference/generated/numpy.fft.rfft.html) and [frequency-bin mapping](https://numpy.org/doc/stable/reference/generated/numpy.fft.rfftfreq.html); the capture filter is `parec --monitor-stream` from PulseAudio's tools. Tests cover ownership rejection, missing browser labels, silence, stereo phase cancellation, frequency separation, fast transients, error expiry and stale/invalid frames.

## Maintenance

```sh
# scripts/karaoke/install.py — inspect or remove the user service
systemctl --user status sixbox-karaoke.service
journalctl --user -u sixbox-karaoke.service -n 20
python3 scripts/karaoke/install.py --uninstall
```

A custom Steam library/prefix can be selected with `--prefix /path/to/compatdata/440/pfx`. Play/pause, repeat and seeking require MPRIS support in the selected player; the module does not bypass player restrictions. Source files and tests are under `scripts/karaoke/`, `tests/karaoke_*`, and `SixBox/src/Features/Visuals/Karaoke/`.
