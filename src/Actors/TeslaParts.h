#pragma once
// ---------------------------------------------------------------------------
// TeslaParts.h
// Procedurally modelled parts of the makeshift Tesla coil gun, and how they
// fit together. Like all furniture, every part is built from boxes,
// cylinders, tori and tapered limbs; one shared instanced mesh per part is
// used both for parts lying in the world and for the gun in the player's
// hands.
//
// Part space: origin at the centre of the resting footprint, +Y up, +Z the
// part's front.
//   Battery       - a 140 x 75 x 100 mm brick with a label and a four-LED
//                   charge gauge on its front and two terminal studs on top.
//                   One mesh per gauge reading (the LEDs are baked in).
//   Driver        - a steel enclosure (vents, panel, warning sticker, power
//                   switch) with the primary coil - three and a half turns of
//                   copper tubing on standoffs - projecting from its front
//                   along +Z.
//   Coil          - a finely wound former standing on end (+Y), capped, with a
//                   terminal rod on top.
//   TopLoad       - a spun aluminium toroid lying flat, its hub carrying the
//                   breakout spike pointing up (+Y).
//   Frame         - (gun only) pistol grip, trigger, guard and the rail and
//                   brackets that carry the coil.
//
// Gun space: origin under the driver box, +Z out of the muzzle, +Y up. The
// coil lies along +Z: it slides through the driver's primary, the top load
// caps it and the spike points straight ahead; the battery hangs underneath
// as a foregrip.
// ---------------------------------------------------------------------------

#include "Gameplay/Items.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <cstdint>

/// Every mesh the parts are drawn with.
enum class PartMesh : uint8_t {
    Battery0 = 0, ///< Flat: every LED dark.
    Battery1,     ///< Nearly flat: one red LED, blinking.
    Battery2,     ///< Two green LEDs.
    Battery3,
    Battery4,     ///< Full: four green LEDs.
    Driver,
    Coil,
    TopLoad,
    Frame,
    Count
};

inline constexpr int kPartMeshCount = static_cast<int>(PartMesh::Count);

/// One part of the gun (or of it being put together) in the player's hands.
struct ViewModelPart {
    PartMesh  mesh;
    glm::mat4 model; ///< Part -> world.
};

namespace tesla {

/// Builds one part mesh (part space, or gun space for the frame).
MeshData buildMesh(PartMesh mesh);

/// Mesh for an item (batteries show their charge).
PartMesh meshFor(const Item& item);

/// Where a part sits in the assembled gun (part -> gun space).
glm::mat4 mountTransform(PartType type);

/// Resting pose of a part put down at a site: part -> surface space (origin
/// on the surface, +Z the surface's front). `spin` (radians) turns it about
/// the vertical where the site allows it.
glm::mat4 restTransform(PartType type, SiteKind site, float spin);

/// The spike's tip in part space (TopLoad) and in gun space.
glm::vec3 spikeTipLocal();
glm::vec3 spikeTipGun();

/// Rough centre of a part in part space (interaction ranking, sounds).
glm::vec3 centerLocal(PartType type);

} // namespace tesla
