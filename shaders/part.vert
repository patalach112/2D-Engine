// Aegis2D — procedural part vertex shader (Milestone 9).
//
// Same no-vertex-buffer design as quad.vert; additionally forwards the
// per-instance skin payload (part kind, variant jitter, clock, throttle) and
// the flag bitmask to the fragment stage.
#version 460

layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5));

layout(location = 0) in vec4 a_rect;   // xy = world center, zw = size
layout(location = 1) in vec4 a_tint;   // rgba base tint / livery
layout(location = 2) in float a_rot;   // CCW radians
layout(location = 3) in vec4 a_skin;   // kind, variant, clock, throttle
layout(location = 4) in float a_flags; // kPartFlag* bitmask

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_local; // [-1, 1]
layout(location = 2) out vec4 v_skin;
layout(location = 3) out float v_flags;

void main() {
    const vec2 corner = kCorners[gl_VertexIndex];

    const float cos_r = cos(a_rot);
    const float sin_r = sin(a_rot);
    const vec2 rotated = vec2(corner.x * cos_r - corner.y * sin_r,
                              corner.x * sin_r + corner.y * cos_r);

    const vec2 world = a_rect.xy + rotated * a_rect.zw;
    gl_Position = pc.projection * vec4(world, 0.0, 1.0);

    v_color = a_tint;
    v_local = corner * 2.0;
    v_skin = a_skin;
    v_flags = a_flags;
}
