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
    // Negative softness selects disc mode: an inscribed analytic circle
    // (v_local in [-1, 1], radius 1) with fwidth() anti-aliasing. Used for
    // planets, moons, atmosphere rims and beacon dots — still no textures.
    if (v_softness < 0.0) {
        const float d = length(v_local) - 1.0;
        const float aa = fwidth(d) * 1.5 + 1e-4;
        const float alpha = 1.0 - smoothstep(-aa, aa, d);
        out_color = vec4(v_color.rgb, v_color.a * alpha);
        return;
    }

    const float distance_from_center = length(v_local);   // 0 center .. ~1.41 corner
    const float fade = clamp(1.0 - distance_from_center * v_softness, 0.0, 1.0);
    out_color = vec4(v_color.rgb, v_color.a * fade);
}
