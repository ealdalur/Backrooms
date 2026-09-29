#pragma once
// ---------------------------------------------------------------------------
// ShaderSources.h
// GLSL 330 core sources, embedded as raw string literals (no shader files).
// ---------------------------------------------------------------------------

namespace shaders {

/// World geometry (static chunks, instanced furniture, doors, terminals).
extern const char* const kWorldVertex;
/// Blinn-Phong shading with clustered rectangular area lights, derivative
/// bump mapping, analytic wall AO, procedural grime, live terminal screens
/// and height fog.
extern const char* const kWorldFragment;

/// World-space geometry without instancing (the Stalker's body and sprites).
extern const char* const kShadowVertex;
/// The Stalker: a light-swallowing silhouette whose edges boil away into
/// smoke (noise-dissolved, dithered), soot motes and two pinprick eyes.
extern const char* const kShadowFragment;
extern const char* const kLightningVertex;   ///< Tesla gun discharges (additive ribbons / glows).
extern const char* const kLightningFragment;

/// Full-screen triangle generated from gl_VertexID (no vertex buffer).
extern const char* const kFullscreenVertex;
/// 13-tap bloom downsample with optional soft-threshold prefilter.
extern const char* const kBloomDownFragment;
/// 3x3 tent-filter bloom upsample (accumulated additively).
extern const char* const kBloomUpFragment;
/// Bloom composite, ACES tonemapping, vignette, grain, dread effects
/// (fear-driven desaturation, aberration, pulsing vignette), fade to black
/// and the crosshair.
extern const char* const kCompositeFragment;

} // namespace shaders
