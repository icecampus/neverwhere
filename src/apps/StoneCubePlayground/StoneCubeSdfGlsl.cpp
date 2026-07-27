#include "pch.h"

#include "StoneCubeSdfGlsl.h"

namespace stonecube {

const char* raymarchFragmentSource() {
    return R"GLSL(
#version 330

// Uniform block (7 x vec4, std140; same order as Params/Uniforms on the CPU).
uniform vec4 boxSize;    // xyz = half extents, w = bevel
uniform vec4 shape1;     // x = voroScale, y = jitter, z = grooveDepth, w = grooveK
uniform vec4 shape2;     // x = fbmAmp, y = fbmFreq, z = bumpStrength, w = seed
uniform vec4 look;       // x = lightYaw, y = lightPitch, z = moss, w = toneSeed
uniform vec4 camera;     // x = yaw, y = pitch, z = dist, w = unused
uniform vec4 target;     // xyz = orbit target
uniform vec4 resolution; // xy = render size (pixels)

out vec4 fragColor;

#define voroScale   shape1.x
#define jitter      shape1.y
#define grooveDepth shape1.z
#define grooveK     shape1.w
#define fbmAmp      shape2.x
#define fbmFreq     shape2.y
#define bumpStr     shape2.z
#define seed        shape2.w
#define lightYaw    look.x
#define lightPitch  look.y
#define mossAmt     look.z
#define toneSeed    look.w

// ---------------------------------------------------------------------------
// Hashes / voronoi / fbm (iq's "Voronoi - rocks" principles)
// ---------------------------------------------------------------------------

vec3 hash3f(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

// Returns (F1, F2, cellId).
vec3 voro(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    float id = 0.0;
    vec2 res = vec2(100.0);
    for (int k = -1; k <= 1; k++)
    for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
        vec3 b = vec3(float(i), float(j), float(k));
        vec3 r = b - f + mix(vec3(0.5), hash3f(p + b), jitter);
        float d = dot(r, r);
        if (d < res.x) {
            id = dot(p + b, vec3(1.0, 57.0, 113.0));
            res = vec2(d, res.x);
        } else if (d < res.y) {
            res.y = d;
        }
    }
    return vec3(sqrt(res), abs(id));
}

float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    const vec3 o = vec3(1.0, 0.0, 0.0);
    float n000 = hash3f(i).x;
    float n100 = hash3f(i + o.xyy).x;
    float n010 = hash3f(i + o.yxy).x;
    float n110 = hash3f(i + o.xxy).x;
    float n001 = hash3f(i + o.yyx).x;
    float n101 = hash3f(i + o.xyx).x;
    float n011 = hash3f(i + o.yxx).x;
    float n111 = hash3f(i + o.xxx).x;
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float f = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
        f += a * vnoise(p);
        p = p * 2.0 + 11.7;
        a *= 0.5;
    }
    return f;
}

// ---------------------------------------------------------------------------
// Stone cube SDF: round box with voronoi boulders bulging out of the cells
// ---------------------------------------------------------------------------

float sdRoundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

// (dist, cellFactor, cellId)
vec3 map(vec3 p) {
    float dBox = sdRoundBox(p, boxSize.xyz, boxSize.w);
    vec3 v = voro(voroScale * p + seed);
    float f = clamp(grooveK * (v.y - v.x), 0.0, 1.0);
    // Bulge out inside cells, grooves along the cell borders.
    float d = dBox - grooveDepth * f;
    d += fbmAmp * (fbm(fbmFreq * p) - 0.5);
    return vec3(d, f, v.z);
}

float mapDist(vec3 p) { return map(p).x; }

vec3 calcNormal(vec3 p) {
    const vec2 e = vec2(0.0015, -0.0015);
    return normalize(e.xyy * mapDist(p + e.xyy) + e.yyx * mapDist(p + e.yyx) +
                     e.yxy * mapDist(p + e.yxy) + e.xxx * mapDist(p + e.xxx));
}

float raymarch(vec3 ro, vec3 rd, out float cellF, out float cellId) {
    float t = 0.01;
    for (int i = 0; i < 160; ++i) {
        vec3 m = map(ro + rd * t);
        if (m.x < 0.0008 * t + 0.0004) {
            cellF = m.y;
            cellId = m.z;
            return t;
        }
        // Relaxed step: the voronoi bulge + fbm displacement break Lipschitz.
        t += 0.7 * m.x;
        if (t > 30.0) break;
    }
    cellF = 0.0;
    cellId = 0.0;
    return -1.0;
}

float calcAO(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 1; i <= 5; ++i) {
        float h = 0.01 + 0.05 * float(i);
        occ += (h - mapDist(p + n * h)) * sca;
        sca *= 0.75;
    }
    return clamp(1.0 - 2.0 * occ, 0.0, 1.0);
}

float softShadow(vec3 ro, vec3 rd) {
    float res = 1.0;
    float t = 0.02;
    for (int i = 0; i < 32; ++i) {
        float h = mapDist(ro + rd * t);
        res = min(res, 8.0 * h / t);
        t += clamp(h, 0.02, 0.2);
        if (res < 0.005 || t > 6.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

vec3 bumpNormal(vec3 p, vec3 n) {
    const vec2 e = vec2(0.002, 0.0);
    float ref = fbm(fbmFreq * 4.0 * p);
    vec3 grad = vec3(fbm(fbmFreq * 4.0 * (p + e.xyy)) - ref,
                     fbm(fbmFreq * 4.0 * (p + e.yxy)) - ref,
                     fbm(fbmFreq * 4.0 * (p + e.yyx)) - ref) / e.x;
    grad -= n * dot(n, grad);
    return normalize(n + bumpStr * grad);
}

// ---------------------------------------------------------------------------

void main() {
    vec2 uv = (2.0 * gl_FragCoord.xy - resolution.xy) / resolution.y;

    // Orbit camera.
    float cy = cos(camera.x);
    float sy = sin(camera.x);
    float cp = cos(camera.y);
    float sp = sin(camera.y);
    vec3 ro = target.xyz + camera.z * vec3(cp * sy, sp, cp * cy);
    vec3 fwd = normalize(target.xyz - ro);
    vec3 rgt = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(rgt, fwd);
    vec3 rd = normalize(uv.x * rgt + uv.y * up + 1.6 * fwd);

    vec3 bg = mix(vec3(0.65, 0.75, 0.85), vec3(0.25, 0.30, 0.38), 0.5 + 0.5 * rd.y);
    vec3 col = bg;

    float cellF;
    float cellId;
    float t = raymarch(ro, rd, cellF, cellId);
    if (t > 0.0) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);
        n = bumpNormal(p, n);

        // Per-stone tint from the cell id.
        float idHash = fract(sin(cellId * 17.31 + toneSeed * 91.7) * 43758.5453);
        vec3 rockCol = mix(vec3(0.40, 0.40, 0.43), vec3(0.60, 0.57, 0.50), idHash);
        rockCol *= 0.85 + 0.30 * idHash;
        // Moss on up-facing noisy spots.
        float mossMask = smoothstep(0.35, 0.75, n.y) *
            smoothstep(0.4, 0.6, fbm(2.0 * p + toneSeed));
        rockCol = mix(rockCol, vec3(0.24, 0.36, 0.15), mossAmt * mossMask);
        // Grooves darken.
        rockCol *= 0.45 + 0.55 * cellF;

        vec3 lightDir = normalize(vec3(cos(lightPitch) * sin(lightYaw),
            sin(lightPitch), cos(lightPitch) * cos(lightYaw)));
        float ao = calcAO(p, n);
        float dif = clamp(dot(n, lightDir), 0.0, 1.0);
        float sha = softShadow(p + n * 0.01, lightDir);
        float sky = 0.5 + 0.5 * n.y;

        col = rockCol * (0.15 + 0.30 * ao * sky) + rockCol * dif * sha * 1.15;
        // Fog blends to the background with distance.
        col = mix(col, bg, clamp(1.0 - exp(-0.015 * t * t), 0.0, 1.0));
    }

    // Ground plane under the cube: catches the soft shadow, anchors the block.
    float groundY = -(boxSize.y + 0.65);
    if (rd.y < -0.001) {
        float gt = (groundY - ro.y) / rd.y;
        if (gt > 0.0 && (t < 0.0 || gt < t)) {
            vec3 gp = ro + rd * gt;
            vec3 lightDir = normalize(vec3(cos(lightPitch) * sin(lightYaw),
                sin(lightPitch), cos(lightPitch) * cos(lightYaw)));
            float sha = softShadow(gp + vec3(0.0, 0.01, 0.0), lightDir);
            vec3 gcol = bg * 0.6 * (0.35 + 0.65 * sha);
            col = mix(gcol, bg, clamp(1.0 - exp(-0.02 * gt * gt), 0.0, 1.0));
        }
    }

    fragColor = vec4(pow(col, vec3(0.4545)), 1.0);
}
)GLSL";
}

const char* fullscreenVertexSource() {
    return R"GLSL(
#version 330

void main() {
    vec2 pos = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";
}

const char* blitVertexSource() {
    return R"GLSL(
#version 330

out vec2 v_uv;

void main() {
    vec2 pos = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    v_uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";
}

const char* blitFragmentSource() {
    return R"GLSL(
#version 330

uniform sampler2D demo_tex;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    fragColor = vec4(texture(demo_tex, v_uv).rgb, 1.0);
}
)GLSL";
}

} // namespace stonecube
