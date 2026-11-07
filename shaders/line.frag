#version 460 core
// Aegis2D — specialized line-segment fragment shader (Milestone 4).
// Soft band across the line width: alpha fades from the segment axis to the
// edges. softness == 0 gives a hard edge (via a small clamp so the
// smoothstep edges never coincide); softness == 1 gives a fully soft
// triangular falloff.
layout(location = 0) in float v_across;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_softness;

layout(location = 0) out vec4 out_color;

void main() {
    const float d = abs(v_across);
    const float softness = clamp(v_softness, 1e-4, 1.0);
    const float edge0 = 1.0 - softness;
    const float alpha = 1.0 - smoothstep(edge0, 1.0, d);
    if (alpha <= 0.001) {
        discard;
    }
    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
