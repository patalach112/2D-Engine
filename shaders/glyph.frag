// Aegis2D — bitmap glyph fragment shader (Milestone 9).
//
// Decodes a 5x7 bitmap from two packed row words and renders hard pixels with
// cheap 2x2-supersampled edge AA. Glyph cell: 8x8, glyph occupies columns
// 1..5 and rows 0..6 (left/bottom margin of 1, 0.5 spacing) — same metrics as
// the constexpr font table, so text_width() in aegis_ui matches pixels.
#version 460

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_local;   // [-1, 1]
layout(location = 2) in vec2 v_packed;  // x = rows 0-2, y = rows 3-6 (bit patterns)

layout(location = 0) out vec4 out_color;

uint row_bits(uint word, uint row) {
    return (word >> (5u * row)) & 0x1Fu;
}

void main() {
    const uint lo = floatBitsToUint(v_packed.x);  // rows 0..2
    const uint hi = floatBitsToUint(v_packed.y);  // rows 3..6

    // Cell coordinates: px in [0, 8) x [0, 8); row 0 is the TOP of the glyph.
    const vec2 cell = vec2(v_local.x * 0.5 + 0.5, 0.5 - v_local.y * 0.5) * 8.0;

    float coverage = 0.0;
    // 2x2 supersampling for edge AA — cheap at UI glyph sizes.
    for (int sy = 0; sy < 2; ++sy) {
        for (int sx = 0; sx < 2; ++sx) {
            const vec2 p = cell + vec2(float(sx) - 0.5, float(sy) - 0.5) * 0.5;
            const int col = int(floor(p.x));
            const int row = int(floor(p.y));
            bool on = false;
            if (col >= 1 && col <= 5 && row >= 0 && row <= 6) {
                const uint bits = (row <= 2) ? row_bits(lo, uint(row)) : row_bits(hi, uint(row - 3));
                on = ((bits >> uint(col - 1)) & 1u) != 0u;
            }
            coverage += on ? 0.25 : 0.0;
        }
    }
    if (coverage <= 0.001) {
        discard;
    }
    out_color = vec4(v_color.rgb, v_color.a * coverage);
}
