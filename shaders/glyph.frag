// Aegis2D — vector glyph fragment shader (Milestone 10.5).
//
// The atlas is a single R8_UNORM coverage texture (tabular: baked straight
// from vector outlines by stb_truetype). Bilinear sampling in hardware gives
// smooth, correctly anti-aliased edges at any size — no supersampling loop,
// no bitmap decoding.
#version 460

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) out vec4 out_color;

void main() {
    const float coverage = texture(u_atlas, v_uv).r;
    if (coverage <= 0.004) {
        discard;
    }
    out_color = vec4(v_color.rgb, v_color.a * coverage);
}
