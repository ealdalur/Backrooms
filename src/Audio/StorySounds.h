#pragma once
// ---------------------------------------------------------------------------
// StorySounds.h
// The sounds of the way out (registered in SoundBank):
//   * The noclip: reality tearing - a pitch screaming upward through a
//     collapsing bit depth, stuttering, digital shrieks, then a deep blow
//     and nothing.
//   * The player's voice, back "in the real world": a tired man, relieved,
//     talking to himself in a quiet office (formant speech, a small room).
// ---------------------------------------------------------------------------

#include "Audio/SoundBank.h"

#include <cstdint>
#include <vector>

namespace storysfx {

/// What the player says on arriving in the office (for the caption).
const char* monologueText();

std::vector<Sound> makeNoclipTear(uint64_t seed);
std::vector<Sound> makeMonologue(uint64_t seed);

} // namespace storysfx
