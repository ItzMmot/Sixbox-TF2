# Karaoke glass provenance

Liquid refraction, rounded-distance field, grain and lighting are ported to HLSL
SM3 from Ali A. Jafari's MIT shader:
https://github.com/aliajafari/Liquid-Glass-with-WebGL/blob/main/src/shaders/fragment.glsl
The original fragment shader is included alongside its complete license.

The DX9 capture, clamped sampler, separable Gaussian and render-target lifecycle
are adapted from Eray's MIT example:
https://github.com/3r4y/imgui-blur-effect/blob/main/src/blur.cpp
Its complete MIT license is included. SixBox uses two distinct ping-pong targets,
restores the complete device state, and releases all default-pool resources on
reset; it never reads a texture while that texture is a render target.

The OverShifted/LiqiudGlassHLSL shader is not included, because its original
algorithm has no explicit license grant.
