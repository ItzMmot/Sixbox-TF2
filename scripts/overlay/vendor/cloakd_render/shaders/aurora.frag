#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float strength;
    vec4 tint;
} ubuf;
void main() {
    vec2 p = qt_TexCoord0;
    float wave = sin(p.x * 8.0 + ubuf.time) * 0.14 + sin(p.x * 15.0 - ubuf.time * 0.6) * 0.06;
    float a = exp(-abs(p.y - 0.5 - wave) * 6.0) * ubuf.strength * 0.8;
    // Premultiplied alpha, as required by Qt Quick.
    fragColor = vec4(ubuf.tint.rgb * a, a) * ubuf.qt_Opacity;
}
