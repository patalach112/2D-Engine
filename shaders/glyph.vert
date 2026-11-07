// Aegis2D — bitmap glyph vertex shader (Milestone 9).
//
// quad.vert with one extra varying: the packed 5x7 row bitmask survives as
// raw float bits (floatBitsToUint in the fragment shader). The glyph cell is
// always square.
#version 460

layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5));

layout(location = 0) in vec4 a_rect;
layout(location = 1) in vec4 a_tint;
layout(location = 2) in vec2 a_params; // x,y = packed 5x7 row words (bit patterns)

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_local;
layout(location = 2) out vec2 v_packed;

void main() {
    const vec2 corner = kCorners[gl_VertexIndex];
    const vec2 world = a_rect.xy + corner * a_rect.zw;
    gl_Position = pc.projection * vec4(world, 0.0, 1.0);

    v_color = a_tint;
    v_local = corner * 2.0;          // [-1, 1]
    v_packed = a_params;
}
