#pragma once
// ---------------------------------------------------------------------------
// ShaderSources.h
// GLSL 330 core sources, embedded as raw string literals (no shader files).
// ---------------------------------------------------------------------------

namespace shaders {

/// World geometry (static chunks, instanced furniture and doors).
extern const char* const kWorldVertex;
/// Blinn-Phong shading with clustered rectangular area lights, derivative
/// bump mapping, analytic wall AO, procedural grime and height fog.
extern const char* const kWorldFragment;

/// Full-screen triangle generated from gl_VertexID (no vertex buffer).
extern const char* const kFullscreenVertex;
/// 13-tap bloom downsample with optional soft-threshold prefilter.
extern const char* const kBloomDownFragment;
/// 3x3 tent-filter bloom upsample (accumulated additively).
extern const char* const kBloomUpFragment;
/// Bloom composite, ACES tonemapping, vignette, grain and crosshair.
extern const char* const kCompositeFragment;

} // namespace shaders
