#pragma once
// ---------------------------------------------------------------------------
// DoomSounds.h
// Sound effects and music of the terminal Doom clone (see Gameplay/Doom),
// synthesised like everything else, but voiced for a 1993 PC:
//   * Effects are rendered at full rate, then pushed through the DMX sound
//     path of the era: 11 kHz sample-and-hold and 8-bit quantisation.
//   * Gun actions are pure noise (friction bursts and pitchless knocks).
//   * Monster voices: a buzzy sawtooth gliding through vowel formants,
//     ring-modulated and roughened with noise (snarls, screams, gurgles).
//   * Music: a sequenced metal loop played on 2-operator FM voices with
//     modulator self-feedback (the OPL2 / AdLib voice), with FM drums.
// Registered in SoundBank under the SoundId::Doom* ids.
// ---------------------------------------------------------------------------

#include "Audio/SoundBank.h"

#include <cstdint>
#include <vector>

namespace doomsfx {

std::vector<Sound> makePistol(uint64_t seed);
std::vector<Sound> makeShotgun(uint64_t seed);
std::vector<Sound> makeImpSight(uint64_t seed);
std::vector<Sound> makeTrooperSight(uint64_t seed);
std::vector<Sound> makeMonsterPain(uint64_t seed);
std::vector<Sound> makeMonsterDeath(uint64_t seed);
std::vector<Sound> makeClaw(uint64_t seed);
std::vector<Sound> makeFireball(uint64_t seed);
std::vector<Sound> makeExplode(uint64_t seed);
std::vector<Sound> makePlayerPain(uint64_t seed);
std::vector<Sound> makePlayerDeath(uint64_t seed);
std::vector<Sound> makeItemUp(uint64_t seed);
std::vector<Sound> makeWeaponUp(uint64_t seed);
std::vector<Sound> makeDoor(uint64_t seed);
std::vector<Sound> makeSwitch(uint64_t seed);
std::vector<Sound> makeMusic(uint64_t seed);

} // namespace doomsfx
