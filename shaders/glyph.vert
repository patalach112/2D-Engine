// Aegis2D — vector glyph vertex shader (Milestone 10.5).
//
// quad.vert with one extra varying: the atlas UV rect survives as two packed
// 16-bit unorm words (unpacked here to normalized UVs). The glyph quad is
// axis-aligned and square-free — each glyph keeps its own cell aspect.
#version 460

layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5));

layout(location = 0) in vec4 a_rect;
layout(location = 1) in vec4 a_tint;
layout(location = 2) in uvec2 a_params; // x = (u0, v0), y = (u1, v1) — 16-bit unorm pairs

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

vec2 unpack_uv16(uint packed) {
    return vec2(float(packed & 0xFFFFu), float(packed >> 16)) * (1.0 / 65535.0);
}

void main() {
    const vec2 corner = kCorners[gl_VertexIndex];
    const vec2 world = a_rect.xy + corner * a_rect.zw;
    gl_Position = pc.projection * vec4(world, 0.0, 1.0);

    const vec2 uv0 = unpack_uv16(a_params.x);   // top-left (atlas, y down)
    const vec2 uv1 = unpack_uv16(a_params.y);   // bottom-right
    // corner.y == +0.5 is the TOP of the quad in y-up world space, which is
    // the TOP of the glyph — it maps to v0 (atlas rows run downward).
    v_uv = vec2(mix(uv0.x, uv1.x, corner.x + 0.5), mix(uv0.y, uv1.y, 0.5 - corner.y));
    v_color = a_tint;
}
