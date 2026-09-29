#pragma once
// ---------------------------------------------------------------------------
// TeslaSounds.h
// Everything heard around the makeshift Tesla coil gun, synthesised like the
// rest of the game (Audio/SynthKit):
//   * Handling the parts: grabbing one (a scuff, a hard clack, loose bits
//     rattling inside), swapping one for another (set down, pick up), each
//     part snapping into the gun (a noisy "chhk", sometimes a few ratchet
//     clicks), and the gun powering up (relay clack, the rising whine of the
//     tank capacitors charging, a test spark).
//   * The discharge: a seamless loop of the roaring, crackling buzz of a
//     solid-state coil (a spark on every interrupter pulse, snaps, sizzle),
//     sharp cracks as it breaks out or an arc lands, the dry, noise-only click
//     of the trigger on a flat battery, and the piezo low-battery chirp.
//   * Filing cabinets: steel drawers rolling out on ball-bearing runners to
//     their stop, and rolling back in to slam shut (sheet-metal modes).
//   * The entities under the arc: the Wanderer's electrified shriek and its
//     last wail (formant speech, buzzing with the current), the Stalker's
//     inhuman screech and its death coming apart into steam, and the sizzle,
//     crackle and deep whump of a body vaporising.
// Registered in SoundBank under the ids from SoundId::PartPickup on.
// ---------------------------------------------------------------------------

#include "Audio/SoundBank.h"

#include <cstdint>
#include <vector>

namespace teslasfx {

std::vector<Sound> makePartPickup(uint64_t seed);
std::vector<Sound> makePartSwap(uint64_t seed);
std::vector<Sound> makeAssembleSnap(uint64_t seed);
std::vector<Sound> makePowerUp(uint64_t seed);
std::vector<Sound> makeArcLoop(uint64_t seed);
std::vector<Sound> makeZap(uint64_t seed);
std::vector<Sound> makeDryClick(uint64_t seed);
std::vector<Sound> makeBatteryLow(uint64_t seed);
std::vector<Sound> makeCabinetOpen(uint64_t seed);
std::vector<Sound> makeCabinetClose(uint64_t seed);
std::vector<Sound> makeWandererPain(uint64_t seed);
std::vector<Sound> makeWandererDeath(uint64_t seed);
std::vector<Sound> makeStalkerPain(uint64_t seed);
std::vector<Sound> makeStalkerDeath(uint64_t seed);
std::vector<Sound> makeVaporize(uint64_t seed);

} // namespace teslasfx
