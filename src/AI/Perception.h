#pragma once
// ---------------------------------------------------------------------------
// Perception.h
// What entities know about the player each frame, and the sounds entities
// make (turned into positional audio by the Soundscape).
// ---------------------------------------------------------------------------

#include "Render/Frustum.h"

#include <glm/glm.hpp>
#include <cstdint>

/// The player as the entities perceive them.
struct PlayerView {
    glm::vec3 eye{0.0f};
    glm::vec3 feet{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    int       level = 0;
    Frustum   frustum;            ///< The player's actual view volume (for "am I being looked at").
    bool      viewBlocked = false; ///< Staring into a terminal / blacked out: sees nothing else.
};

/// Continuous entity state the Soundscape follows every frame.
struct EntityAudioState {
    bool      stalkerActive = false;
    glm::vec3 stalkerPosition{0.0f};
    bool      wandererActive = false;
    glm::vec3 wandererHead{0.0f};      ///< Where its voice comes from.
    float     wandererAgitation = 0.0f; ///< 0..1: murmuring -> crying out.
};

/// A one-shot sound made by an entity.
struct EntitySound {
    enum class Type : uint8_t {
        StalkerSkitter, ///< Claws scrabbling on carpet as it rushes unseen.
        StalkerHiss,    ///< A sharp exhale as it darts for cover.
        WandererStep,   ///< A heavy, dragging bare footstep.
        WandererPain,   ///< Shrieking with current running through it.
        WandererDeath,  ///< Its last, falling wail as it burns away.
        StalkerPain,    ///< An inhuman screech under the arc.
        StalkerDeath,   ///< The screech coming apart into hissing steam.
        Vaporize,       ///< A body sizzling and crackling away to nothing.
    };
    Type      type;
    glm::vec3 position;
    float     intensity; ///< 0..1
};
