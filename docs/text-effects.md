# Text expansion and glow

Settings are in **Visuals → Menu → Debug**, immediately below **Custom font**.
Enable **Debug options** if the Debug section is hidden.

| Setting | Range | Default |
| --- | --- | --- |
| Text expansion | 0–2 screen pixels | 0 |
| Text glow strength | 0–200% | 0 |
| Text glow radius | 0–12 screen pixels | 4 |

Expansion thickens glyph strokes. Glow is a Gaussian-filtered halo in the text's
own color. Strength zero disables glow; radius zero also disables it. Icon glyphs
keep their normal appearance. The shared menu/overlay font sources, including
merged fallback characters and monospace text, receive the effects.

Values apply when a slider is released. The renderer rebuilds font caches after
a settings change. These three settings are saved as visual settings through the
normal config system. The existing Custom font path retains its existing
non-persistent behavior.

Effects are baked into glyph alpha masks; cached text still draws one quad per
glyph. Glyph advance and text layout metrics are preserved. The adapter wraps the
bundled ImGui 1.92.7 stb font loader without modifying third-party sources or
adding a runtime font library dependency. A reusable scratch buffer bounds glyph
processing to 256×256 raster pixels; larger glyphs retain their normal rendering.

`tests/text_effects_test.cpp` checks mask dilation, Gaussian falloff, atlas growth
and repacking, unchanged text metrics, and cached draw geometry with the actual
bundled ImGui loader. It runs with both RGBA and alpha atlas formats. Pass the
path to a TTF/OTF as its single argument. Tests have been run with Michroma Regular
and Montserrat Medium; native in-game appearance still needs visual confirmation.
