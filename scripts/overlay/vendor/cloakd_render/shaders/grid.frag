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
    vec2 p = qt_TexCoord0 * vec2(20.0, 12.0) + vec2(ubuf.time * 0.2, 0.0);
    vec2 line = smoothstep(vec2(0.94), vec2(0.99), fract(p));
    float a = max(line.x, line.y) * ubuf.strength * 0.7;
    fragColor = vec4(ubuf.tint.rgb * a, a) * ubuf.qt_Opacity;
}
