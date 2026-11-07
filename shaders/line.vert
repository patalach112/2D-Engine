#version 460 core
// Aegis2D — specialized line-segment vertex shader (Milestone 4).
// One instance = one world-space segment. The quad is expanded in the vertex
// shader from the segment direction; no vertex buffers, no line topology
// (GL lineWidth is ignored on most drivers) — segments are filled quads.
//
// Instance attributes (location 0..2, binding 0, stride == 40 bytes):
//   a_rect   vec4  — (ax, ay, bx, by) world-space segment endpoints
//   a_tint   vec4  — rgba color
//   a_params vec2  — x = thickness (WORLD units), y = softness [0..1]
//
// Push constants: mat4 projection (column-major, GLSL mat4).
// Caps: the segment is extended by half the thickness at both ends so that
// consecutive segments of a polyline overlap instead of leaving wedge gaps
// at the joints (orbit paths are polylines).
layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

layout(location = 0) in vec4 a_rect;
layout(location = 1) in vec4 a_tint;
layout(location = 2) in vec2 a_params;

layout(location = 0) out float v_across;   // [-1, 1] across the line width
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_softness;

const vec2 corners[6] = vec2[](vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                               vec2(0.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
    const vec2 a = a_rect.xy;
    const vec2 b = a_rect.zw;
    const vec2 ab = b - a;
    const float len = length(ab);
    const vec2 dir = len > 1e-8 ? ab / len : vec2(1.0, 0.0);
    const vec2 normal = vec2(-dir.y, dir.x);

    const float half_width = max(a_params.x, 0.0) * 0.5;
    const vec2 corner = corners[gl_VertexIndex];

    // Extend by half_width at both ends (overlapping round-ish caps).
    const vec2 position = mix(a - dir * half_width, b + dir * half_width, corner.x)
        + normal * (corner.y * half_width);

    gl_Position = pc.projection * vec4(position, 0.0, 1.0);
    v_across = corner.y;
    v_color = a_tint;
    v_softness = a_params.y;
}
