// Aegis2D — instanced quad fragment shader (sprites, particles, UI).
//
// softness == 0  -> hard-edged quad (UI / rockets / panels)
// softness >  0  -> radial alpha fade scaled by softness (glow particles)
#version 460

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_local;
layout(location = 2) in float v_softness;

layout(location = 0) out vec4 out_color;

void main() {
    const float distance_from_center = length(v_local);   // 0 center .. ~1.41 corner
    const float fade = clamp(1.0 - distance_from_center * v_softness, 0.0, 1.0);
    out_color = vec4(v_color.rgb, v_color.a * fade);
}
