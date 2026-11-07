// Aegis2D — procedural part fragment shader (Milestone 9).
//
// Every part silhouette is an analytic signed-distance function; bodies are
// shaded as lit cylinders (light from upper-left) with panel seams, rivets
// and end caps. Engine nozzles glow with throttle; plumes are cone SDFs with
// mach diamonds. No textures anywhere — pure math.
//
// Coordinate space: v_local in [-1, 1], +y up (world y-up). The flip flag
// mirrors x before shading (mirrored boosters/struts).
#version 460

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_local;
layout(location = 2) in vec4 v_skin;   // kind, variant, clock, throttle
layout(location = 3) in float v_flags; // bit0 ghost, bit1 highlight, bit2 flip

layout(location = 0) out vec4 out_color;

const float K_TANK = 0.0;
const float K_POD = 1.0;
const float K_ENGINE = 2.0;
const float K_PLUME = 3.0;
const float K_DECOUPLER = 4.0;
const float K_SEPARATOR = 5.0;
const float K_FAIRING = 6.0;
const float K_STRUT = 7.0;

float sd_roundbox(vec2 p, vec2 b, float r) {
    const vec2 q = abs(p) - b + r;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

float dot_at(vec2 p, vec2 c, float r) {
    return 1.0 - smoothstep(r, r * 1.9, length(p - c));
}

float band(float y, float center, float width) {  // ring around y == center
    return 1.0 - smoothstep(width, width * 2.6, abs(y - center));
}

float vband(float x, float center, float width) {
    return 1.0 - smoothstep(width, width * 2.6, abs(x - center));
}

// Directional cylinder ramp: brightest left-of-center (light from upper-left).
float cyl_light(float u) {
    return clamp(0.46 + 0.78 * cos((u + 0.34) * 1.18), 0.12, 1.35);
}

void shade_tank(vec2 p, float variant, out float d, out vec3 col) {
    d = sd_roundbox(p, vec2(0.86, 0.97), 0.24);
    const float u = p.x / 0.86;
    col = v_color.rgb * cyl_light(u);
    // panel seam rings
    const float seams = band(p.y, -0.52, 0.022) + band(p.y, 0.14, 0.022) + band(p.y, 0.70, 0.022);
    col *= 1.0 - 0.42 * clamp(seams, 0.0, 1.0);
    // end-cap shading
    col *= 1.0 - 0.20 * smoothstep(0.78, 0.97, abs(p.y));
    // rivet columns near the seams
    float rivets = 0.0;
    rivets += dot_at(p, vec2(0.62, -0.52), 0.038) + dot_at(p, vec2(-0.62, -0.52), 0.038);
    rivets += dot_at(p, vec2(0.62, 0.14), 0.038) + dot_at(p, vec2(-0.62, 0.14), 0.038);
    rivets += dot_at(p, vec2(0.62, 0.70), 0.038) + dot_at(p, vec2(-0.62, 0.70), 0.038);
    col = mix(col, col * 0.38, clamp(rivets, 0.0, 1.0));
    // per-instance livery jitter
    col *= 0.94 + 0.12 * variant;
    // brushed-metal streaks (very subtle)
    col *= 1.0 + 0.05 * sin(p.x * 34.0 + variant * 17.0);
}

void shade_pod(vec2 p, float clock, float variant, out float d, out vec3 col) {
    d = sd_roundbox(p, vec2(0.78, 0.93), 0.42);
    col = v_color.rgb * clamp(0.72 + 0.45 * cos((p.x + 0.36) * 1.32), 0.2, 1.3);
    // nose shading
    col *= 1.0 - 0.22 * smoothstep(0.86, 0.98, p.y);
    // window
    const vec2 wc = vec2(0.0, 0.30);
    const float wd = length(p - wc);
    const float rim = 1.0 - smoothstep(0.245, 0.30, wd);
    const float glass = 1.0 - smoothstep(0.155, 0.19, wd);
    col = mix(col, vec3(0.78, 0.79, 0.82), rim * 0.85);
    col = mix(col, mix(vec3(0.05, 0.09, 0.14), vec3(0.16, 0.30, 0.42), smoothstep(0.02, 0.19, wd)), glass);
    // glass streak
    const float streak = smoothstep(0.075, 0.0, abs((p.x - 0.055) * 0.79 + (p.y - 0.365) * 0.61)) * glass;
    col += vec3(0.42, 0.52, 0.62) * streak;
    // panel line + livery stripe
    col *= 1.0 - 0.30 * band(p.y, -0.18, 0.016);
    col = mix(col, vec3(0.88, 0.44, 0.13), smoothstep(0.085, 0.055, abs(p.y + 0.44)));
    // blinking beacon
    const float beacon = smoothstep(0.085, 0.02, length(p - vec2(0.0, 0.84))) *
                         (0.35 + 0.65 * max(0.0, sin(clock * 3.6 + variant * 9.0)));
    col += vec3(0.95, 0.22, 0.12) * beacon;
}

void shade_engine(vec2 p, float throttle, out float d, out vec3 col) {
    // bell widens toward the bottom (-y)
    const float w = mix(0.30, 0.97, smoothstep(0.80, -0.92, p.y));
    d = abs(p.x) - w;
    d = max(d, abs(p.y + 0.05) - 0.93);
    d = max(d, p.y - 0.96);
    col = v_color.rgb * (0.30 + 0.16 * cos(p.y * 2.4 + 0.6));
    // top mount plate
    col = mix(col, v_color.rgb * 0.95, smoothstep(0.10, 0.03, p.y - 0.74) * step(0.0, p.y));
    // throat rings
    col *= 1.0 - 0.38 * clamp(band(p.y, 0.28, 0.028) + band(p.y, -0.28, 0.028) + band(p.y, -0.66, 0.028), 0.0, 1.0);
    // inner cavity darkening toward the throat
    col *= 1.0 - 0.35 * smoothstep(0.1, -0.9, p.y) * step(p.y, 0.75);
    // emissive nozzle core with throttle
    const float glow = clamp(throttle, 0.0, 1.0);
    col += vec3(1.0, 0.58, 0.20) * smoothstep(0.58, 0.0, length(p - vec2(0.0, -0.60))) * glow;
    col += vec3(1.0, 0.88, 0.60) * smoothstep(0.26, 0.0, length(p - vec2(0.0, -0.80))) * glow;
}

void shade_plume(vec2 p, float clock, float throttle, float variant, out float d, out vec3 col, out float alpha) {
    const float t = (1.0 - p.y) * 0.5;            // 0 nozzle -> 1 tip
    const float w = 0.10 + 0.70 * pow(t, 1.22);    // expanding cone
    d = abs(p.x) - w;                               // soft cone edge (AA handled below)
    const float body = smoothstep(0.0, -0.34 * w - 0.05, d);
    const float diamonds = 0.78 + 0.5 * pow(abs(sin(t * 17.0 - clock * 24.0 + variant * 5.0)), 5.0);
    const vec3 col_a = vec3(1.0, 0.97, 0.90);
    const vec3 col_b = vec3(1.0, 0.74, 0.28);
    const vec3 col_c = vec3(1.0, 0.40, 0.10);
    col = mix(mix(col_a, col_b, smoothstep(0.02, 0.42, t)), col_c, smoothstep(0.42, 1.0, t));
    alpha = pow(1.0 - t, 2.4) * body * diamonds * clamp(throttle, 0.0, 1.0);
    alpha *= smoothstep(1.0, 0.9, t);
}

void shade_decoupler(vec2 p, float variant, out float d, out vec3 col) {
    d = sd_roundbox(p, vec2(0.94, 0.80), 0.06);
    col = v_color.rgb * clamp(0.52 + 0.28 * cos((p.x + 0.30) * 1.22), 0.18, 1.1);
    // top/bottom rails
    const float rail = band(p.y, 0.58, 0.055) + band(p.y, -0.58, 0.055);
    col = mix(col, v_color.rgb * 1.22, clamp(rail, 0.0, 1.0) * 0.8);
    // chevron warning zone (yellow/black diagonal dashes)
    const float dash = step(0.55, fract((p.x + p.y * 0.38 + variant * 3.0) * 2.6));
    const float zone = smoothstep(0.24, 0.17, abs(p.y));
    col = mix(col, mix(vec3(0.82, 0.68, 0.13), vec3(0.07, 0.07, 0.09), dash), zone * 0.92);
    // corner bolts
    float bolts = dot_at(p, vec2(0.76, 0.56), 0.055) + dot_at(p, vec2(-0.76, 0.56), 0.055) +
                  dot_at(p, vec2(0.76, -0.56), 0.055) + dot_at(p, vec2(-0.76, -0.56), 0.055);
    col = mix(col, vec3(0.10, 0.10, 0.12), clamp(bolts, 0.0, 1.0));
}

void shade_separator(vec2 p, float variant, out float d, out vec3 col) {
    d = sd_roundbox(p, vec2(0.94, 0.70), 0.07);
    col = v_color.rgb * 0.34;
    // diagonal hatch
    const float hatch = step(0.56, fract((p.x - p.y) * 2.35 + variant));
    const float zone = smoothstep(0.60, 0.32, abs(p.y));
    col = mix(col, vec3(0.72, 0.74, 0.80), hatch * zone * 0.62 + (1.0 - hatch) * zone * 0.16);
    // grip bars
    col = mix(col, vec3(0.55, 0.57, 0.63), band(p.y, 0.44, 0.06) + band(p.y, -0.44, 0.06));
    // red center line
    col = mix(col, vec3(0.78, 0.14, 0.10), smoothstep(0.05, 0.02, abs(p.y)) * 0.85);
}

void shade_fairing(vec2 p, out float d, out vec3 col) {
    // tapered nose: wide at the bottom, narrow tip on top
    const float halfw = mix(0.28, 0.90, smoothstep(1.02, -0.35, p.y));
    d = abs(p.x) - halfw;
    d = max(d, abs(p.y + 0.02) - 0.97);
    d = max(d, p.y - 0.99);
    col = v_color.rgb * clamp(0.82 + 0.34 * cos((p.x + 0.42) * 1.28), 0.25, 1.35);
    // nose-tip shading
    col *= 1.0 - 0.20 * smoothstep(0.84, 0.99, p.y);
    // vertical + horizontal seams
    col *= 1.0 - 0.40 * vband(p.x, 0.0, 0.014);
    col *= 1.0 - 0.36 * clamp(band(p.y, -0.30, 0.018) + band(p.y, 0.34, 0.018), 0.0, 1.0);
    // specular streak
    const float streak = smoothstep(0.16, 0.0, abs(p.x + 0.28)) * (1.0 - smoothstep(-0.4, 0.95, p.y) * 0.5);
    col += vec3(0.20) * streak;
}

void shade_strut(vec2 p, out float d, out vec3 col) {
    d = sd_roundbox(p, vec2(0.96, 0.30), 0.21);
    col = v_color.rgb * 0.55;
    // diagonal hatch
    const float hatch = step(0.52, fract((p.x + p.y) * 2.15));
    col = mix(col, v_color.rgb * 1.22, hatch * 0.55);
    // end bolts
    const float bolts = dot_at(p, vec2(0.78, 0.0), 0.085) + dot_at(p, vec2(-0.78, 0.0), 0.085);
    col = mix(col, vec3(0.09, 0.09, 0.11), clamp(bolts, 0.0, 1.0));
}

void main() {
    vec2 p = v_local;
    const uint flags = uint(v_flags + 0.5);
    if ((flags & 4u) != 0u) {
        p.x = -p.x;  // mirrored placement (boosters, struts)
    }
    const float kind = v_skin.x;
    const float variant = v_skin.y;
    const float clock = v_skin.z;
    const float throttle = v_skin.w;

    float d = 0.0;
    float alpha = 1.0;
    vec3 col = v_color.rgb;

    if (kind == K_TANK) {
        shade_tank(p, variant, d, col);
    } else if (kind == K_POD) {
        shade_pod(p, clock, variant, d, col);
    } else if (kind == K_ENGINE) {
        shade_engine(p, throttle, d, col);
    } else if (kind == K_PLUME) {
        shade_plume(p, clock, throttle, variant, d, col, alpha);
        alpha *= v_color.a;
        if (alpha < 0.004) {
            discard;
        }
        out_color = vec4(col, alpha);
        return;
    } else if (kind == K_DECOUPLER) {
        shade_decoupler(p, variant, d, col);
    } else if (kind == K_SEPARATOR) {
        shade_separator(p, variant, d, col);
    } else if (kind == K_FAIRING) {
        shade_fairing(p, d, col);
    } else {  // K_STRUT
        shade_strut(p, d, col);
    }

    const float aa = fwidth(d) * 1.35 + 1e-4;
    alpha = smoothstep(aa, -aa, d);

    if ((flags & 1u) != 0u) {          // ghost — desaturated, translucent
        const float luma = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(col, vec3(luma), 0.72);
        alpha *= 0.40;
    }
    if ((flags & 2u) != 0u) {          // highlight — cyan snap outline
        const float stroke = smoothstep(0.085, 0.02, abs(d + 0.045));
        col = mix(col, vec3(0.30, 0.95, 1.0), stroke * 0.9);
        alpha = max(alpha, stroke);
    }
    if (alpha < 0.004) {
        discard;
    }
    out_color = vec4(col, v_color.a * alpha);
}
