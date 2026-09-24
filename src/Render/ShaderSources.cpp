// ---------------------------------------------------------------------------
// ShaderSources.cpp
// ---------------------------------------------------------------------------
#include "Render/ShaderSources.h"

namespace shaders {

// ============================================================================
// World vertex shader
// ============================================================================
const char* const kWorldVertex = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec2 aMatInfo;  // x = material layer, y = chunk-local light index (-1 = none)
layout(location = 4) in mat4 aModel;    // per-instance model matrix (locations 4..7)

uniform mat4 uViewProj;
uniform int  uLightBase;                // chunk's offset into the global light list

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
flat out int vMaterial;
flat out int vLightIndex;

void main() {
    vec4 world  = aModel * vec4(aPosition, 1.0);
    vWorldPos   = world.xyz;
    // Model matrices are rigid (rotation + translation): no inverse-transpose needed.
    vNormal     = mat3(aModel) * aNormal;
    vUV         = aUV;
    vMaterial   = int(aMatInfo.x + 0.5);
    vLightIndex = aMatInfo.y < -0.5 ? -1 : uLightBase + int(aMatInfo.y + 0.5);
    gl_Position = uViewProj * world;
}
)GLSL";

// ============================================================================
// World fragment shader
// ============================================================================
const char* const kWorldFragment = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
flat in int vMaterial;
flat in int vLightIndex;

layout(location = 0) out vec4 oColor;

// ---- Materials -------------------------------------------------------------
uniform sampler2DArray uAlbedo;          // rgb = albedo (sRGB decoded by hardware)
uniform sampler2DArray uSurface;         // r = height, g = specular mask, b = emissive mask
uniform vec4  uMaterialParams[8];        // x = spec, y = shininess, z = bump (m), w = emissive gain

// ---- Clustered lights --------------------------------------------------------
uniform samplerBuffer  uLightData;       // 2 texels per light: (center.xyz, halfX), (halfZ, color.rgb)
uniform samplerBuffer  uLightIntensity;  // 1 texel per light: current flicker output
uniform usamplerBuffer uGridCells;       // per grid cell: (first index, count)
uniform usamplerBuffer uGridIndices;     // flattened light index lists
uniform vec2  uGridOrigin;
uniform float uGridCellSize;
uniform ivec2 uGridDims;
uniform float uLightRange;
uniform float uLightPower;

// ---- Architecture (edge map for analytic ambient occlusion) --------------------
uniform usampler2D uEdgeMap;             // per world cell: (west edge type, south edge type)
uniform vec2  uEdgeOrigin;
uniform ivec2 uEdgeDims;
uniform float uCellSize;
uniform float uWallHalf;
uniform float uArchHalfWidth;
uniform float uDoorHalfWidth;
uniform float uCeilingHeight;
uniform float uCeilingTile;

// ---- Camera / atmosphere --------------------------------------------------------
uniform vec3  uCameraPos;
uniform vec3  uAmbient;                  // indirect light arriving from above
uniform vec3  uAmbientDown;              // indirect light arriving from below (floor bounce)
uniform vec3  uFogColor;
uniform float uFogDensity;

const int MAT_WALLPAPER = 0;
const int MAT_CARPET    = 1;
const int MAT_CEILING   = 2;

const uint EDGE_OPEN = 0u;
const uint EDGE_WALL = 1u;
const uint EDGE_ARCH = 2u;

const float PI = 3.14159265;
const uint  MAX_LIGHTS_PER_CELL = 48u;

// ---- Procedural helpers ---------------------------------------------------------
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1, 0, 0));
    float n010 = hash13(i + vec3(0, 1, 0));
    float n110 = hash13(i + vec3(1, 1, 0));
    float n001 = hash13(i + vec3(0, 0, 1));
    float n101 = hash13(i + vec3(1, 0, 1));
    float n011 = hash13(i + vec3(0, 1, 1));
    float n111 = hash13(i + vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += amp * valueNoise(p);
        p = p * 2.03 + vec3(17.1, 5.3, 11.7);
        amp *= 0.5;
    }
    return sum / 0.9375;
}

// ---- Derivative-based bump mapping (Mikkelsen 2010, no tangents required) -----------
vec3 perturbNormal(vec3 N, vec3 pos, vec2 uv, float layer, float bumpScale) {
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    float h0 = texture(uSurface, vec3(uv, layer)).r;
    float hx = texture(uSurface, vec3(uv + duvdx, layer)).r;
    float hy = texture(uSurface, vec3(uv + duvdy, layer)).r;
    float dBs = (hx - h0) * bumpScale;
    float dBt = (hy - h0) * bumpScale;

    vec3 dpdx = dFdx(pos);
    vec3 dpdy = dFdy(pos);
    vec3 r1 = cross(dpdy, N);
    vec3 r2 = cross(N, dpdx);
    float det = dot(dpdx, r1);
    vec3 grad = sign(det) * (dBs * r1 + dBt * r2);
    return normalize(abs(det) * N - grad);
}

// ---- Analytic ambient occlusion from the wall layout --------------------------------
// Distance along an edge from `along` to the edge's nearest solid part.
float solidDistanceAlong(uint type, float along) {
    if (type == EDGE_WALL) return 0.0;
    float halfWidth = (type == EDGE_ARCH) ? uArchHalfWidth : uDoorHalfWidth;
    return max(halfWidth - abs(along - 0.5 * uCellSize), 0.0);
}

// Occlusion contributed by one edge. `perp` is the signed distance to the edge
// line, `nPerp` the normal component across it.
float edgeOcclusion(uint type, float perp, float along, bool vertical, float nPerp) {
    if (type == EDGE_OPEN) return 1.0;
    float dPerp = abs(perp) - uWallHalf;
    // Skip the wall's own faces and anything embedded in the wall slab.
    if (vertical && (abs(nPerp) > 0.5 || dPerp < 0.02)) return 1.0;
    float d = length(vec2(max(dPerp, 0.0), solidDistanceAlong(type, along)));
    return mix(0.42, 1.0, smoothstep(0.0, 0.65, d));
}

float ambientOcclusion(vec3 P, vec3 N) {
    bool vertical = abs(N.y) < 0.5;
    float ao = 1.0;
    // Wall-to-floor and wall-to-ceiling junctions.
    if (vertical) {
        ao *= mix(0.55, 1.0, smoothstep(0.0, 0.45, P.y));
        ao *= mix(0.78, 1.0, smoothstep(0.0, 0.35, uCeilingHeight - P.y));
    }
    vec2 rel = (P.xz - uEdgeOrigin) / uCellSize;
    ivec2 cell = ivec2(floor(rel));
    if (any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell + 1, uEdgeDims))) return ao;

    vec2  local = P.xz - (uEdgeOrigin + vec2(cell) * uCellSize);
    uvec2 here  = texelFetch(uEdgeMap, cell, 0).rg;
    uint  east  = texelFetch(uEdgeMap, cell + ivec2(1, 0), 0).r;
    uint  north = texelFetch(uEdgeMap, cell + ivec2(0, 1), 0).g;

    ao *= edgeOcclusion(here.r, local.x,              local.y, vertical, N.x); // west  (x = 0)
    ao *= edgeOcclusion(east,   local.x - uCellSize,  local.y, vertical, N.x); // east  (x = S)
    ao *= edgeOcclusion(here.g, local.y,              local.x, vertical, N.z); // south (z = 0)
    ao *= edgeOcclusion(north,  local.y - uCellSize,  local.x, vertical, N.z); // north (z = S)
    return max(ao, 0.25);
}

// ---- Rectangular fluorescent area lights ------------------------------------------
// Diffuse uses the closest point on the emitter rectangle; specular uses the
// point where the reflection ray meets the emitter plane (representative
// point method). Attenuation: inverse square, windowed to reach zero at range.
vec3 evaluateLights(vec3 P, vec3 N, vec3 V, vec3 albedo, float specIntensity, float shininess) {
    ivec2 gc = ivec2(floor((P.xz - uGridOrigin) / uGridCellSize));
    if (any(lessThan(gc, ivec2(0))) || any(greaterThanEqual(gc, uGridDims))) return vec3(0.0);

    uvec2 cellInfo = texelFetch(uGridCells, gc.y * uGridDims.x + gc.x).rg;
    uint  count = min(cellInfo.y, MAX_LIGHTS_PER_CELL);
    vec3  R = reflect(-V, N);
    float specNorm = (shininess + 8.0) / (8.0 * PI);
    vec3  sum = vec3(0.0);

    for (uint i = 0u; i < count; ++i) {
        int li = int(texelFetch(uGridIndices, int(cellInfo.x + i)).r);
        float intensity = texelFetch(uLightIntensity, li).r;
        if (intensity <= 0.001) continue;

        vec4 d0 = texelFetch(uLightData, li * 2);
        vec4 d1 = texelFetch(uLightData, li * 2 + 1);
        vec3 C = d0.xyz;
        vec2 halfSize = vec2(d0.w, d1.x);
        vec3 color = d1.yzw;

        vec3 Q = vec3(clamp(P.x, C.x - halfSize.x, C.x + halfSize.x), C.y,
                      clamp(P.z, C.z - halfSize.y, C.z + halfSize.y));
        vec3  Lv = Q - P;
        float dist2 = dot(Lv, Lv);
        float dist = sqrt(dist2);
        vec3  L = Lv / max(dist, 1e-4);
        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;                 // back-facing

        // Troffers emit mostly downward; fade continuously to zero at the
        // emitter plane so wall tops do not show a hard cut-off line.
        float emitterCos = clamp(L.y * 5.0, 0.0, 1.0) * (0.25 + 0.75 * max(L.y, 0.0));
        float r = dist / uLightRange;
        float window = clamp(1.0 - r * r * r * r, 0.0, 1.0);
        float atten = window * window / (dist2 + 0.8);

        vec3 Ls = L;
        if (R.y > 1e-3) {
            vec3 hit = P + R * ((C.y - P.y) / R.y);
            vec3 Qs = vec3(clamp(hit.x, C.x - halfSize.x, C.x + halfSize.x), C.y,
                           clamp(hit.z, C.z - halfSize.y, C.z + halfSize.y));
            Ls = normalize(Qs - P);
        }
        vec3  H = normalize(Ls + V);
        float spec = pow(max(dot(N, H), 0.0), shininess) * specNorm * specIntensity * max(dot(N, Ls), 0.0);

        sum += color * (intensity * atten * emitterCos) * (albedo * NdotL + vec3(spec));
    }
    return sum * uLightPower;
}

void main() {
    float layer = float(vMaterial);
    vec4  params = uMaterialParams[vMaterial];
    vec3  geomN = normalize(vNormal);

    vec3  toCam = uCameraPos - vWorldPos;
    float camDist = length(toCam);
    vec3  V = toCam / max(camDist, 1e-4);

    vec3  albedo = texture(uAlbedo, vec3(vUV, layer)).rgb;
    vec3  surf = texture(uSurface, vec3(vUV, layer)).rgb;
    float specMask = surf.g;
    float emissiveMask = surf.b;

    // Large-scale, world-space variation that breaks up texture repetition.
    if (vMaterial == MAT_WALLPAPER) {
        float g = fbm(vWorldPos * vec3(0.55, 0.3, 0.55));
        albedo *= mix(0.80, 1.05, g);
        float lowGrime = 1.0 - smoothstep(0.0, 0.7, vWorldPos.y + (g - 0.5) * 0.5);
        albedo *= 1.0 - 0.18 * lowGrime;
        // Non-repeating water stains with a darker tide line at their edge.
        float s = fbm(vWorldPos * vec3(0.3, 0.42, 0.3) + vec3(3.1, 0.0, 7.7));
        float stain = smoothstep(0.60, 0.70, s);
        float tide = smoothstep(0.575, 0.60, s) * (1.0 - smoothstep(0.60, 0.65, s));
        albedo = mix(albedo, albedo * vec3(0.82, 0.70, 0.46), stain * 0.5);
        albedo *= 1.0 - 0.10 * tide;
    } else if (vMaterial == MAT_CARPET) {
        float d = fbm(vec3(vWorldPos.x * 0.18, 7.3, vWorldPos.z * 0.18));
        float wet = smoothstep(0.58, 0.68, d);
        float rim = smoothstep(0.54, 0.58, d) * (1.0 - smoothstep(0.58, 0.64, d));
        albedo *= mix(1.0, 0.66, wet) * (1.0 - 0.10 * rim);
        specMask = max(specMask, wet * 0.9);
    } else if (vMaterial == MAT_CEILING) {
        vec2 tileCoord = vWorldPos.xz / uCeilingTile;
        vec2 tileId = floor(tileCoord);
        albedo *= 0.93 + 0.1 * hash13(vec3(tileId, 3.7));
        // A few water-damaged tiles: a distorted stain with tide rings, clipped to the tile.
        if (hash13(vec3(tileId, 9.1)) < 0.07) {
            vec2 local = tileCoord - tileId;
            vec2 centre = 0.3 + 0.4 * vec2(hash13(vec3(tileId, 1.3)), hash13(vec3(tileId, 5.9)));
            float radius = 0.2 + 0.25 * hash13(vec3(tileId, 7.7));
            float d = length(local - centre) / radius + 0.35 * (fbm(vec3(vWorldPos.x * 6.0, 2.0, vWorldPos.z * 6.0)) - 0.5);
            float inside = 1.0 - smoothstep(0.85, 1.0, d);
            float ring = exp(-pow((d - 0.93) / 0.05, 2.0)) + 0.5 * exp(-pow((d - 0.6) / 0.04, 2.0));
            albedo = mix(albedo, albedo * vec3(0.80, 0.66, 0.42), inside * 0.6);
            albedo *= 1.0 - 0.18 * ring;
        }
    }

    // Fade relief out with distance to avoid shimmering on far surfaces.
    vec3 N = geomN;
    float bump = params.z * clamp(1.0 - camDist / 35.0, 0.0, 1.0);
    if (bump > 0.0) N = perturbNormal(geomN, vWorldPos, vUV, layer, bump);

    float ao = ambientOcclusion(vWorldPos, geomN);

    // Hemispherical ambient approximating inter-reflection: surfaces facing
    // down (ceiling, undersides) receive strong warm bounce from the lit
    // carpet and walls; upward-facing surfaces receive the dimmer ceiling bounce.
    vec3 ambient = mix(uAmbientDown, uAmbient, N.y * 0.5 + 0.5);
    vec3 color = albedo * ambient * ao;
    color += evaluateLights(vWorldPos, N, V, albedo, params.x * specMask, params.y) * mix(0.55, 1.0, ao);

    // Emissive diffuser panels follow their fixture's flicker.
    if (vLightIndex >= 0) {
        color += albedo * emissiveMask * params.w * texelFetch(uLightIntensity, vLightIndex).r;
    }

    // Humid, yellowish squared-exponential haze.
    float fog = 1.0 - exp(-pow(camDist * uFogDensity, 2.0));
    color = mix(color, uFogColor, fog);

    oColor = vec4(color, 1.0);
}
)GLSL";

// ============================================================================
// Post-processing
// ============================================================================
const char* const kFullscreenVertex = R"GLSL(
#version 330 core
out vec2 vUV;
void main() {
    // Vertices 0,1,2 -> (0,0), (2,0), (0,2): one triangle covering the screen.
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* const kBloomDownFragment = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 oColor;

uniform sampler2D uSource;
uniform vec2  uTexel;       // 1 / source resolution
uniform int   uPrefilter;   // 1 on the first pass: extract bright areas
uniform float uThreshold;
uniform float uKnee;

vec3 prefilter(vec3 c) {
    float brightness = max(c.r, max(c.g, c.b));
    float soft = clamp(brightness - uThreshold + uKnee, 0.0, 2.0 * uKnee);
    soft = soft * soft / (4.0 * uKnee + 1e-5);
    float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-5);
    return c * contribution;
}

vec3 tap(vec2 offset) { return texture(uSource, vUV + offset * uTexel).rgb; }

void main() {
    // 13-tap filter (Jimenez, "Next Generation Post Processing in Call of Duty").
    vec3 a = tap(vec2(-2.0,  2.0)), b = tap(vec2(0.0,  2.0)), c = tap(vec2(2.0,  2.0));
    vec3 d = tap(vec2(-2.0,  0.0)), e = tap(vec2(0.0,  0.0)), f = tap(vec2(2.0,  0.0));
    vec3 g = tap(vec2(-2.0, -2.0)), h = tap(vec2(0.0, -2.0)), i = tap(vec2(2.0, -2.0));
    vec3 j = tap(vec2(-1.0,  1.0)), k = tap(vec2(1.0,  1.0));
    vec3 l = tap(vec2(-1.0, -1.0)), m = tap(vec2(1.0, -1.0));
    vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (uPrefilter == 1) col = prefilter(col);
    oColor = vec4(max(col, vec3(0.0)), 1.0);
}
)GLSL";

const char* const kBloomUpFragment = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 oColor;

uniform sampler2D uSource;
uniform vec2  uTexel;
uniform float uRadius;

vec3 tap(vec2 offset) { return texture(uSource, vUV + offset * uTexel * uRadius).rgb; }

void main() {
    vec3 s = tap(vec2(-1.0,  1.0)) + 2.0 * tap(vec2(0.0,  1.0)) + tap(vec2(1.0,  1.0))
           + 2.0 * tap(vec2(-1.0, 0.0)) + 4.0 * tap(vec2(0.0, 0.0)) + 2.0 * tap(vec2(1.0, 0.0))
           + tap(vec2(-1.0, -1.0)) + 2.0 * tap(vec2(0.0, -1.0)) + tap(vec2(1.0, -1.0));
    oColor = vec4(s * (1.0 / 16.0), 1.0);
}
)GLSL";

const char* const kCompositeFragment = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 oColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uTime;
uniform vec2  uResolution;
uniform float uCrosshairHighlight;  // 0..1, ring shown when a door is interactable

// ACES filmic curve (Narkowicz 2015 fit).
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float grainNoise(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb + texture(uBloom, vUV).rgb * uBloomStrength;
    vec3 c = aces(hdr * uExposure);

    // Soft optical vignette.
    vec2 q = vUV - 0.5;
    q.x *= uResolution.x / uResolution.y;
    c *= mix(0.70, 1.0, smoothstep(0.95, 0.25, length(q)));

    c = pow(c, vec3(1.0 / 2.2));

    // Animated film grain (applied in display space).
    float n = grainNoise(gl_FragCoord.xy + fract(uTime * 7.31) * vec2(113.1, 71.7));
    c += (n - 0.5) * 0.035;

    // Minimal crosshair: centre dot plus a ring when a door can be used.
    float r = length(gl_FragCoord.xy - uResolution * 0.5);
    float dotMask = 1.0 - smoothstep(1.5, 2.5, r);
    float ringMask = uCrosshairHighlight * (1.0 - smoothstep(0.8, 1.6, abs(r - 7.0)));
    c = mix(c, vec3(1.0), max(dotMask * 0.7, ringMask * 0.85));

    oColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)GLSL";

} // namespace shaders
