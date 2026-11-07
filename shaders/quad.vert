// Aegis2D — instanced quad vertex shader (sprites, particles, UI).
//
// No geometry buffers: the six vertices of a quad are synthesized from
// gl_VertexIndex, and every per-draw datum comes from per-instance attributes.
#version 460

layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5));

// Per-instance attributes (binding 1).
layout(location = 0) in vec4 a_rect;        // xy = world position (center), zw = size
layout(location = 1) in vec4 a_tint;        // rgba in [0, 1]
layout(location = 2) in vec2 a_params;      // x = rotation (radians), y = softness [0..1]

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_local;      // quad-local coords in [-1, 1]
layout(location = 2) out float v_softness;

void main() {
    const vec2 corner = kCorners[gl_VertexIndex];

    const float cos_r = cos(a_params.x);
    const float sin_r = sin(a_params.x);
    const vec2 rotated = vec2(corner.x * cos_r - corner.y * sin_r,
                              corner.x * sin_r + corner.y * cos_r);

    const vec2 world = a_rect.xy + rotated * a_rect.zw;
    gl_Position = pc.projection * vec4(world, 0.0, 1.0);

    v_color = a_tint;
    v_local = corner * 2.0;
    v_softness = a_params.y;
}
