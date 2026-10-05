// ---------------------------------------------------------------------------
// OfficeLayout.cpp
// ---------------------------------------------------------------------------
#include "World/OfficeLayout.h"

#include "Math/Random.h"
#include "Render/MeshBuilder.h"
#include "World/Decals.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <string>

using world::EdgeAxis;
using world::EdgeType;

namespace office {
namespace {

constexpr float S  = world::kCellSize;
constexpr float T  = world::kWallThickness;
constexpr float kPi = 3.14159265f;
const glm::vec3 kUp(0.0f, 1.0f, 0.0f);

// Salts of the office's random streams and ids.
constexpr uint64_t kSaltCell     = 0x0FF1'0001ull;
constexpr uint64_t kSaltTerminal = 0x0FF1'0002ull;
constexpr uint64_t kSaltPhone    = 0x0FF1'0003ull;
constexpr uint64_t kSaltCabinet  = 0x0FF1'0004ull;
constexpr uint64_t kSaltText     = 0x0FF1'0005ull;

// Partitions.
constexpr float kPod         = 1.9f;  ///< Half size of a four-cubicle pod.
constexpr float kPanelHeight = 1.45f;
constexpr float kPanelHalf   = 0.03f; ///< Half thickness.

/// The cubicle the player arrives in: cell, and quadrant (-1 / +1 along x and z).
constexpr int kSpawnX = 8, kSpawnZ = 7;
constexpr int kSpawnQx = -1, kSpawnQz = -1;

constexpr glm::ivec2 kConferenceDoor(11, kDepth - 1);

bool inside(int gx, int gz) { return gx >= 0 && gx < kWidth && gz >= 0 && gz < kDepth; }

bool isRoom(Zone z) {
    return z == Zone::Office || z == Zone::CornerSuite || z == Zone::Breakroom || z == Zone::Conference || z == Zone::ExitLobby;
}

/// Cells of one room share an id (the two-cell rooms are open inside).
int roomId(int gx, int gz) {
    switch (zone(gx, gz)) {
    case Zone::CornerSuite: return (gx < kWidth / 2 ? 0 : 1) + (gz < kDepth / 2 ? 0 : 2);
    case Zone::Breakroom:   return 10;
    case Zone::Conference:  return 11;
    default:                return 100 + gz * kWidth + gx;
    }
}

const glm::ivec2 kSideStep[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};          ///< West, east, south, north.
const glm::vec3  kInward[4]   = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}; ///< Into the cell from that side.

/// The edge on a side of a cell (0 = west, 1 = east, 2 = south, 3 = north).
EdgeType sideEdge(int gx, int gz, int side) {
    switch (side) {
    case 0:  return edge(kLevel, gx, gz, EdgeAxis::West);
    case 1:  return edge(kLevel, gx + 1, gz, EdgeAxis::West);
    case 2:  return edge(kLevel, gx, gz, EdgeAxis::South);
    default: return edge(kLevel, gx, gz + 1, EdgeAxis::South);
    }
}

/// One wall of a cell as seen from inside it: the face's middle at floor
/// level, the normal into the room, and "right" for someone facing the wall.
struct Wall {
    glm::vec3 face;
    glm::vec3 n;
    glm::vec3 right;
};
Wall wallOf(const glm::vec3& centre, int side) {
    Wall w;
    w.n = kInward[side];
    w.face = centre - w.n * (S * 0.5f - T * 0.5f);
    w.right = glm::cross(-w.n, kUp);
    return w;
}

float yawFacing(const glm::vec3& dir) { return std::atan2(dir.x, dir.z); }

template <size_t N>
const char* pick(rnd::Rng& rng, const char* const (&options)[N]) {
    return options[rng.next() % N];
}

// ---- What the walls say --------------------------------------------------------------------
struct Poster {
    const char* title;
    const char* line;
};
const Poster kPosters[] = {
    {"TEAMWORK", "TOGETHER EVERYONE ACHIEVES MORE"},
    {"SYNERGY", "ONE TEAM. ONE FLOOR. FOREVER."},
    {"COMMITMENT", "WE ARE IN THIS FOR THE LONG TERM"},
    {"OPPORTUNITY", "EVERY DOOR IS A NEW BEGINNING"},
    {"PERSEVERANCE", "THE ONLY WAY OUT IS THROUGH"},
    {"PRODUCTIVITY", "THE CLOCK NEVER STOPS. NEITHER DO WE."},
    {"HANG IN THERE", "IT'S ALMOST FRIDAY"},
    {"EXCELLENCE", "GOOD ENOUGH IS NOT AN EXIT STRATEGY"},
};
const char* const kNotices[] = {
    "REMINDER:\nTPS REPORTS ARE\nDUE FRIDAY",
    "PLEASE DO NOT\nFEED THE PRINTER",
    "WHOEVER KEEPS\nTAKING MY STAPLER:\nSTOP",
    "MANDATORY FUN\nFRIDAY 4PM\nATTENDANCE TRACKED",
    "LOST: ONE INTERN\nLAST SEEN NEAR\nTHE COPIER",
    "THE 4TH FLOOR\nDOES NOT EXIST.\nPLEASE STOP ASKING.",
    "PARKING LOT\nCLOSED UNTIL\nFURTHER NOTICE",
};
struct Safety {
    const char* header;
    const char* body;
};
const Safety kSafety[] = {
    {"CAUTION", "IN CASE OF FIRE\nDO NOT USE\nTHE ELEVATORS"},
    {"NOTICE", "DAYS WITHOUT\nAN INCIDENT:\n0"},
    {"NOTICE", "EMPLOYEES MUST\nWASH HANDS BEFORE\nRETURNING TO WORK"},
    {"CAUTION", "BADGES MUST BE\nVISIBLE AT\nALL TIMES"},
    {"NOTICE", "REPORT ANY\nFLICKERING LIGHTS\nTO FACILITIES"},
};
const char* const kNames[] = {"D. KESSLER", "M. OKAFOR", "R. LINDQVIST", "P. DUBOIS", "T. ASHWORTH", "S. NAKAMURA",
                              "L. FERREIRA", "B. MOREAU", "J. HALVORSEN", "A. PETRAKIS", "C. WEXLER", "N. ABARA"};
const char* const kTitles[] = {"REGIONAL MANAGER", "VP OF SYNERGY", "HEAD OF EXIT STRATEGY", "CHIEF WELLNESS OFFICER",
                               "DIRECTOR, LEVEL 0", "SENIOR VP, REORGANIZATION", "HUMAN RESOURCES", "ASST. TO THE MANAGER",
                               "HEAD OF FACILITIES", "VP OF FORWARD MOTION"};
const char* const kWhiteboards[] = {
    "Q3 OBJECTIVES:\n1. SYNERGY\n2. LEVERAGE THE PARADIGM\n3. FIND THE EXIT\n4. ???\n5. PROFIT",
    "BRAINSTORM\n- MORE MEETINGS\n- FEWER WINDOWS\n- WHY IS THE CARPET WET?",
    "ACTION ITEMS\n* CIRCLE BACK\n* TOUCH BASE\n* MOVE THE NEEDLE\n* WHO IS ON THE PHONES??",
    "DO NOT ERASE\nDO NOT ERASE\nDO NOT ERASE",
    "ROADMAP\nQ1  ONBOARD\nQ2  ONBOARD\nQ3  ONBOARD\nQ4  NEVER LEAVE",
};

/// Builds the contents of one cell.
class Furnisher {
public:
    Furnisher(ChunkBlueprint& bp, uint64_t seed, int gx, int gz)
        : m_bp(bp), m_seed(seed), m_gx(gx), m_gz(gz),
          m_origin(bp.coord.originX(), bp.coord.originY(), bp.coord.originZ()),
          m_rng(rnd::hashCoords(seed, gx, gz, kSaltCell)) {
        m_min = glm::vec3(static_cast<float>(gx) * S, m_origin.y, static_cast<float>(gz) * S);
        m_centre = m_min + glm::vec3(S * 0.5f, 0.0f, S * 0.5f);
    }

    void build() {
        switch (zone(m_gx, m_gz)) {
        case Zone::Cubicles:    cubiclePod(); break;
        case Zone::Aisle:       aisle(); break;
        case Zone::Corridor:    corridor(); break;
        case Zone::Office:      executiveOffice(doorSide(), true); break;
        case Zone::CornerSuite: cornerSuite(); break;
        case Zone::Breakroom:   breakroom(); break;
        case Zone::Conference:  conference(); break;
        case Zone::ExitLobby:   exitLobby(); break;
        case Zone::Outside:
        default:                break;
        }
    }

private:
    // ---- Primitives ---------------------------------------------------------------------
    FurnitureInstance& add(FurnitureType type, const glm::vec3& p, float yaw) {
        m_bp.furniture.push_back(Furniture::makeInstance(type, p, yaw));
        return m_bp.furniture.back();
    }
    uint64_t nextId(uint64_t salt) { return rnd::hashCoords(m_seed, m_gx, m_gz, salt + static_cast<uint64_t>(m_counter++)); }

    void solid(const glm::vec3& mn, const glm::vec3& mx, MaterialId mat, uint8_t faces = mesh::FaceAll, bool collide = true) {
        mesh::BoxDesc d;
        d.min = mn;
        d.max = mx;
        d.material = mat;
        d.faces = faces;
        d.uvOrigin = m_origin;
        mesh::addBox(m_bp.staticMesh, d);
        if (collide) m_bp.colliders.push_back(AABB(mn, mx));
    }

    /// A straight run of cubicle partition between two floor points on a line of constant x or z.
    void partition(const glm::vec2& a, const glm::vec2& b) {
        const bool alongX = std::fabs(b.x - a.x) > std::fabs(b.y - a.y);
        const float y = m_origin.y;
        const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
        const glm::vec3 mn = alongX ? glm::vec3(lo.x, y, lo.y - kPanelHalf) : glm::vec3(lo.x - kPanelHalf, y, lo.y);
        const glm::vec3 mx = alongX ? glm::vec3(hi.x, y + kPanelHeight, hi.y + kPanelHalf) : glm::vec3(hi.x + kPanelHalf, y + kPanelHeight, hi.y);
        const uint8_t longFaces = alongX ? (mesh::FacePosZ | mesh::FaceNegZ) : (mesh::FacePosX | mesh::FaceNegX);
        const uint8_t ends = alongX ? (mesh::FacePosX | mesh::FaceNegX) : (mesh::FacePosZ | mesh::FaceNegZ);
        solid(mn + glm::vec3(0.0f, 0.03f, 0.0f), mx - glm::vec3(0.0f, 0.03f, 0.0f), MaterialId::OfficeFabric, longFaces, false);
        solid(mn, mx, MaterialId::GrayMetal, ends, true); // end posts (and the collider)
        const glm::vec3 grow = alongX ? glm::vec3(0.0f, 0.0f, 0.006f) : glm::vec3(0.006f, 0.0f, 0.0f);
        solid(glm::vec3(mn.x, mx.y - 0.03f, mn.z) - grow, mx + grow, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegY, false);
        solid(mn, glm::vec3(mx.x, mn.y + 0.03f, mx.z), MaterialId::DarkPlastic, longFaces, false); // kick plate
    }

    /// A terminal, a phone and some clutter on a desk top (desk space: +Z towards whoever sits there).
    void deskKit(const glm::mat4& desk, float top, const glm::vec3& terminalAt, float terminalChance, float phoneChance,
                 float phoneX0, float phoneX1, bool force) {
        const bool terminal = force || m_rng.chance(terminalChance);
        if (terminal) {
            m_bp.terminals.push_back({nextId(kSaltTerminal), glm::translate(desk, terminalAt), force || m_rng.chance(0.85f)});
        }
        float phoneX = 1e3f;
        if (force || m_rng.chance(phoneChance)) {
            phoneX = terminal ? m_rng.range(phoneX0, phoneX1) : m_rng.range(-0.4f, phoneX1);
            const float z = m_rng.range(-0.14f, 0.02f);
            const float yaw = terminal ? m_rng.range(-0.45f, -0.12f) : -0.4f * phoneX + m_rng.range(-0.15f, 0.15f);
            m_bp.phones.push_back({nextId(kSaltPhone), glm::rotate(glm::translate(desk, glm::vec3(phoneX, top, z)), yaw, kUp)});
        }
        // Clutter where the computer and the phone leave room.
        const float spots[3] = {-0.55f, 0.28f, 0.58f};
        for (float x : spots) {
            if (terminal && x < terminalAt.x + 0.34f) continue; // left of the computer is its keyboard and mouse
            if (std::fabs(x - phoneX) < 0.27f) continue;
            if (!m_rng.chance(0.5f)) continue;
            const float r = m_rng.nextFloat();
            const FurnitureType t = r < 0.45f ? FurnitureType::PaperStack : r < 0.75f ? FurnitureType::Mug : FurnitureType::DocTray;
            const glm::mat4 model = glm::rotate(glm::translate(desk, glm::vec3(x, top, m_rng.range(-0.15f, 0.12f))),
                                                m_rng.range(-0.4f, 0.4f), kUp);
            FurnitureInstance inst;
            inst.type = t;
            inst.position = glm::vec3(model[3]);
            inst.model = model;
            m_bp.furniture.push_back(inst);
        }
    }

    // ---- Things on walls --------------------------------------------------------------------
    glm::vec3 onWall(const Wall& w, float along, float y, float out) const {
        return w.face + kUp * y + w.right * along + w.n * out;
    }
    void text(const Wall& w, float along, float baseline, float out, const std::string& s, decals::Ink ink, float charHeight,
              decals::Align align = decals::Align::Center, float wobble = 0.0f, float spacing = 1.7f) {
        decals::TextLayout layout;
        layout.charHeight = charHeight;
        layout.align = align;
        layout.wobble = wobble;
        layout.lineSpacing = spacing;
        decals::addText(m_bp.staticMesh, decals::wrap(s, 64), onWall(w, along, baseline, out + world::kDecalOffset), w.right, kUp, ink,
                        layout, nextId(kSaltText));
    }
    /// Text fitted into a box (centred, `top` = its upper edge).
    void fittedText(const Wall& w, float along, float top, float out, const std::string& s, decals::Ink ink, glm::vec2 box,
                    float maxHeight, float wobble = 0.0f) {
        const std::vector<std::string> lines = decals::wrap(s, 64);
        const float h = decals::fitHeight(lines, box.x, box.y, maxHeight);
        decals::TextLayout layout;
        layout.charHeight = h;
        layout.align = decals::Align::Center;
        layout.wobble = wobble;
        decals::addText(m_bp.staticMesh, lines, onWall(w, along, top - h, out + world::kDecalOffset), w.right, kUp, ink, layout,
                        nextId(kSaltText));
    }
    void panel(const Wall& w, float along, float yCentre, const glm::vec2& size, atlas::Sign cell, float depth,
               MaterialId rim = MaterialId::GrayMetal) {
        decals::addPanel(m_bp.staticMesh, onWall(w, along, yCentre, 0.0f), w.right, kUp, size, depth, cell, rim);
    }

    void poster(const Wall& w, float along, float yCentre) {
        const Poster& p = kPosters[m_rng.next() % (sizeof(kPosters) / sizeof(kPosters[0]))];
        const float r = m_rng.nextFloat();
        const atlas::Sign cell = r < 0.4f ? atlas::Sign::PosterMountain : r < 0.75f ? atlas::Sign::PosterSea : atlas::Sign::PosterBlue;
        const glm::vec2 size(0.62f, 0.86f);
        const float depth = 0.006f;
        panel(w, along, yCentre, size, cell, depth, MaterialId::DarkPlastic);
        const float bottom = yCentre - size.y * 0.5f;
        if (cell == atlas::Sign::PosterBlue) { // no caption band: the words go across the middle
            fittedText(w, along, yCentre + 0.12f, depth, p.title, decals::Ink::PrintWhite, {0.5f, 0.1f}, 0.06f);
            fittedText(w, along, yCentre - 0.04f, depth, p.line, decals::Ink::PrintWhite, {0.5f, 0.05f}, 0.022f);
            return;
        }
        fittedText(w, along, bottom + 0.215f, depth, p.title, decals::Ink::PrintWhite, {0.52f, 0.07f}, 0.055f);
        fittedText(w, along, bottom + 0.105f, depth, p.line, decals::Ink::PrintWhite, {0.52f, 0.05f}, 0.02f);
    }
    void notice(const Wall& w, float along, float yCentre) {
        const glm::vec2 size(0.30f, 0.40f);
        const float depth = 0.002f;
        if (m_rng.chance(0.5f)) {
            const Safety& s = kSafety[m_rng.next() % (sizeof(kSafety) / sizeof(kSafety[0]))];
            panel(w, along, yCentre, size, atlas::Sign::SafetyYellow, depth, MaterialId::OfficePaper);
            fittedText(w, along, yCentre + 0.175f, depth, s.header, decals::Ink::PrintWhite, {0.24f, 0.06f}, 0.05f);
            fittedText(w, along, yCentre + 0.06f, depth, s.body, decals::Ink::Print, {0.25f, 0.2f}, 0.03f);
        } else {
            panel(w, along, yCentre, size, atlas::Sign::Paper, depth, MaterialId::OfficePaper);
            fittedText(w, along, yCentre + 0.13f, depth, pick(m_rng, kNotices), decals::Ink::Print, {0.25f, 0.24f}, 0.03f);
        }
    }
    void placard(const Wall& w, float along, float yCentre, const std::string& line1, const std::string& line2, atlas::Sign cell) {
        const glm::vec2 size(0.44f, 0.15f);
        const float depth = 0.012f;
        panel(w, along, yCentre, size, cell, depth);
        const decals::Ink ink = cell == atlas::Sign::Brass ? decals::Ink::Print : decals::Ink::PrintWhite;
        if (line2.empty()) {
            fittedText(w, along, yCentre + 0.03f, depth, line1, ink, {0.38f, 0.06f}, 0.05f);
        } else {
            fittedText(w, along, yCentre + 0.045f, depth, line1, ink, {0.38f, 0.04f}, 0.035f);
            fittedText(w, along, yCentre - 0.012f, depth, line2, ink, {0.38f, 0.03f}, 0.022f);
        }
    }
    void exitSign(const Wall& w, float along, float yCentre) {
        const glm::vec2 size(0.40f, 0.18f);
        const float depth = 0.05f;
        panel(w, along, yCentre, size, atlas::Sign::ExitFace, depth, MaterialId::BeigePlastic);
        fittedText(w, along, yCentre + 0.05f, depth, "EXIT", decals::Ink::PrintGlowRed, {0.3f, 0.1f}, 0.1f);
    }
    void whiteboard(const Wall& w, float along, float yCentre) {
        const glm::vec2 size(1.5f, 0.95f);
        const float depth = 0.018f;
        panel(w, along, yCentre, size, atlas::Sign::Whiteboard, depth, MaterialId::Aluminum);
        // The marker tray, with a marker nobody has capped.
        const glm::vec3 tray = onWall(w, along, yCentre - size.y * 0.5f - 0.01f, 0.0f);
        const glm::vec3 half = glm::abs(w.right) * 0.5f + glm::abs(w.n) * 0.035f + kUp * 0.012f;
        solid(tray - half + w.n * 0.035f, tray + half + w.n * 0.035f, MaterialId::Aluminum, mesh::FaceAll, false);
        const decals::Ink ink = m_rng.chance(0.6f) ? decals::Ink::DryEraseBlue : decals::Ink::DryEraseRed;
        text(w, along - size.x * 0.5f + 0.08f, yCentre + size.y * 0.5f - 0.13f, depth, pick(m_rng, kWhiteboards), ink, 0.068f,
             decals::Align::Left, 0.7f, 1.75f);
    }

    // ---- Rooms --------------------------------------------------------------------------------
    /// The side of a room cell with the door (or opening) to the corridor, or -1.
    int doorSide() const {
        for (int s = 0; s < 4; ++s) {
            const glm::ivec2 nb = glm::ivec2(m_gx, m_gz) + kSideStep[s];
            if (zone(nb.x, nb.y) == Zone::Corridor && sideEdge(m_gx, m_gz, s) != EdgeType::Wall) return s;
        }
        return -1;
    }

    /// A pod of four cubicles: a cross of partitions through the middle, a
    /// solid back on the north and south, the ends open to the aisles; a desk
    /// against each back panel, its occupant facing the pod's spine.
    void cubiclePod() {
        const float cx = m_centre.x, cz = m_centre.z, e = kPod;
        partition({cx - e, cz}, {cx + e, cz});
        partition({cx, cz - e}, {cx, cz + e});
        partition({cx - e, cz + e}, {cx + e, cz + e});
        partition({cx - e, cz - e}, {cx + e, cz - e});
        for (float sx : {-1.0f, 1.0f}) {
            partition({cx + sx * e, cz - e}, {cx + sx * e, cz - 1.0f});
            partition({cx + sx * e, cz + 1.0f}, {cx + sx * e, cz + e});
        }
        const bool spawnPod = m_gx == kSpawnX && m_gz == kSpawnZ;
        for (int q = 0; q < 4; ++q) {
            const float sx = (q & 1) ? 1.0f : -1.0f, sz = (q & 2) ? 1.0f : -1.0f;
            const glm::vec3 deskPos(cx + sx * e * 0.5f, m_origin.y, cz + sz * (e - kPanelHalf - 0.375f - 0.01f));
            const glm::vec3 user(0.0f, 0.0f, -sz); // the occupant faces the back panel
            const FurnitureInstance& desk = add(FurnitureType::Desk, deskPos, yawFacing(user));
            const glm::mat4 deskModel = desk.model;
            const bool home = spawnPod && sx == static_cast<float>(kSpawnQx) && sz == static_cast<float>(kSpawnQz);
            deskKit(deskModel, 0.75f, glm::vec3(-0.2f, 0.75f, -0.04f), 0.7f, 0.6f, 0.40f, 0.52f, home);
            if (home) {
                // The player's own chair, swivelled aside: they are standing where it was.
                add(FurnitureType::Chair, glm::vec3(cx + sx * 0.33f, m_origin.y, cz + sz * 0.45f), sx * kPi * 0.5f);
            } else if (m_rng.chance(0.85f)) {
                const glm::vec3 p = deskPos + user * (0.375f + m_rng.range(0.35f, 0.6f)) + glm::vec3(m_rng.range(-0.15f, 0.15f), 0.0f, 0.0f);
                add(FurnitureType::Chair, p, yawFacing(-user) + m_rng.range(-0.5f, 0.5f));
            }
        }
    }

    /// Walkways through the cubicle field: a plant, a bin, now and then a water cooler.
    void aisle() {
        const glm::vec2 corners[4] = {{0.75f, 0.75f}, {4.25f, 0.75f}, {0.75f, 4.25f}, {4.25f, 4.25f}};
        int order[4] = {0, 1, 2, 3};
        for (int i = 3; i > 0; --i) std::swap(order[i], order[m_rng.rangeInt(0, i)]);
        auto at = [&](int k) { return m_min + glm::vec3(corners[order[k]].x, 0.0f, corners[order[k]].y); };
        if (m_rng.chance(0.4f)) add(m_rng.chance(0.6f) ? FurnitureType::Ficus : FurnitureType::Fern, at(0), m_rng.range(0.0f, 6.28f));
        if (m_rng.chance(0.35f)) add(FurnitureType::TrashCan, at(1), m_rng.range(0.0f, 6.28f));
        if (m_rng.chance(0.2f)) {
            const glm::vec3 p = at(2);
            add(FurnitureType::WaterCooler, p, yawFacing(m_centre - p));
        }
    }

    /// The ring walkway: posters between the doors, name plates beside them,
    /// signs over the openings, plants and bins against the walls.
    void corridor() {
        for (int s = 0; s < 4; ++s) {
            const glm::ivec2 nb = glm::ivec2(m_gx, m_gz) + kSideStep[s];
            const Zone room = zone(nb.x, nb.y);
            if (!isRoom(room)) continue;
            const Wall w = wallOf(m_centre, s);
            const EdgeType e = sideEdge(m_gx, m_gz, s);
            if (e == EdgeType::Door) {
                if (room == Zone::Conference) {
                    placard(w, 0.85f, 1.55f, "CONFERENCE", "ROOM B", atlas::Sign::PlacardBlue);
                } else {
                    rnd::Rng names(rnd::hashCoords(m_seed, nb.x, nb.y, 0x4A3Eull));
                    placard(w, 0.85f, 1.55f, pick(names, kNames), pick(names, kTitles),
                            names.chance(0.3f) ? atlas::Sign::Brass : atlas::Sign::PlacardDark);
                }
            } else if (e == EdgeType::Archway) {
                if (room == Zone::Breakroom) {
                    panel(w, 0.0f, 2.62f, {0.9f, 0.2f}, atlas::Sign::PlacardBlue, 0.012f);
                    fittedText(w, 0.0f, 2.67f, 0.012f, "BREAKROOM", decals::Ink::PrintWhite, {0.8f, 0.1f}, 0.09f);
                } else if (room == Zone::ExitLobby) {
                    exitSign(w, 0.0f, 2.62f);
                }
            }
            if (e == EdgeType::Door || e == EdgeType::Wall) {
                // The stretches of wall either side of the door.
                if (m_rng.chance(0.45f)) {
                    if (m_rng.chance(0.6f)) poster(w, -1.5f, 1.5f);
                    else notice(w, -1.5f, 1.45f);
                }
                if (m_rng.chance(0.3f)) {
                    const glm::vec3 p = w.face + w.n * 0.45f + w.right * 1.8f;
                    add(m_rng.chance(0.5f) ? FurnitureType::Ficus : FurnitureType::TrashCan, p, m_rng.range(0.0f, 6.28f));
                } else if (m_rng.chance(0.15f)) {
                    const glm::vec3 p = w.face + w.n * 0.4f + w.right * 1.8f;
                    add(FurnitureType::WaterCooler, p, yawFacing(w.n));
                }
            }
        }
    }

    /// An executive office: the desk facing the door, a big chair behind it,
    /// two for visitors, a plant, a filing cabinet, something on the walls.
    void executiveOffice(int door, bool visitors) {
        if (door < 0) return;
        const Wall back = wallOf(m_centre, door ^ 1);
        const glm::vec3 in = back.n; // from the back wall towards the door
        const glm::vec3 deskPos = back.face + in * 1.45f;
        const FurnitureInstance& desk = add(FurnitureType::ExecDesk, deskPos, yawFacing(-in));
        const glm::mat4 deskModel = desk.model;
        deskKit(deskModel, 0.76f, glm::vec3(-0.45f, 0.76f, -0.12f), 0.75f, 0.95f, 0.45f, 0.7f, false);
        add(FurnitureType::ExecChair, deskPos - in * 0.95f, yawFacing(in) + m_rng.range(-0.3f, 0.3f));
        if (visitors) {
            for (float s : {-1.0f, 1.0f}) {
                add(FurnitureType::Chair, deskPos + in * 1.05f + back.right * (s * 0.6f), yawFacing(-in) + m_rng.range(-0.3f, 0.3f));
            }
        }
        const float side = m_rng.chance(0.5f) ? 1.0f : -1.0f;
        add(FurnitureType::Ficus, back.face + in * 0.45f + back.right * (side * 1.9f), m_rng.range(0.0f, 6.28f));
        if (m_rng.chance(0.6f)) {
            // A filing cabinet against the other side wall, near the back.
            const glm::vec3 n = back.right * side; // into the room from the wall on the -side
            const glm::mat4 model = Furniture::makeInstance(FurnitureType::FileCabinet,
                                                            back.face + in * 0.6f - back.right * (side * (S * 0.5f - T * 0.5f - 0.345f)),
                                                            yawFacing(n)).model;
            m_bp.cabinets.push_back({nextId(kSaltCabinet), model});
        }
        // The side walls.
        const int sideWalls[2] = {(door < 2) ? 2 : 0, (door < 2) ? 3 : 1};
        for (int sw : sideWalls) {
            if (sideEdge(m_gx, m_gz, sw) != EdgeType::Wall) continue;
            const Wall w = wallOf(m_centre, sw);
            // "along" on this wall towards the door side.
            const float toDoor = glm::dot(-back.n, w.right) < 0.0f ? 1.0f : -1.0f;
            const float r = m_rng.nextFloat();
            if (r < 0.3f) whiteboard(w, toDoor * 0.9f, 1.45f);
            else if (r < 0.65f) poster(w, toDoor * 0.9f, 1.5f);
            else if (r < 0.8f) notice(w, toDoor * 1.2f, 1.45f);
        }
    }

    /// A corner suite: the office proper in the corner cell, a seating area next to it.
    void cornerSuite() {
        const bool corner = (m_gx == 0 || m_gx == kWidth - 1) && (m_gz == 0 || m_gz == kDepth - 1);
        if (corner) {
            // "Door" side: towards the other half of the suite.
            const int open = m_gx == 0 ? 1 : 0;
            executiveOffice(open, false);
            return;
        }
        add(FurnitureType::BreakTable, m_centre, 0.0f);
        for (int i = 0; i < 3; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / 3.0f + m_rng.range(-0.2f, 0.2f);
            const glm::vec3 d(std::cos(a), 0.0f, std::sin(a));
            add(FurnitureType::Chair, m_centre + d * 0.85f, yawFacing(-d) + m_rng.range(-0.3f, 0.3f));
        }
        const int outer = m_gz == 0 ? 2 : 3; // the exterior wall
        const Wall w = wallOf(m_centre, outer);
        add(FurnitureType::Fern, w.face + w.n * 0.5f + w.right * 1.9f, 0.0f);
        add(FurnitureType::Ficus, w.face + w.n * 0.5f - w.right * 1.9f, 1.0f);
        poster(w, 0.0f, 1.55f);
    }

    /// The breakroom: a counter along the outside wall (coffee maker, microwave,
    /// a fridge), round tables, the water cooler, notices about the fridge.
    void breakroom() {
        const bool west = m_gx == 7;
        const Wall w = wallOf(m_centre, 2); // the exterior (south) wall
        const float y = m_origin.y;
        const float counterEnd = west ? 2.4f : 1.45f;
        const glm::vec3 a = w.face + w.right * -2.4f, b = w.face + w.right * counterEnd;
        const glm::vec3 lo = glm::min(a, b), hi = glm::max(a, b) + glm::vec3(0.0f, 0.0f, 0.62f);
        solid(glm::vec3(lo.x, y, lo.z), glm::vec3(hi.x, y + 0.86f, hi.z - 0.02f), MaterialId::BeigePlastic);
        solid(glm::vec3(lo.x, y + 0.86f, lo.z), glm::vec3(hi.x, y + 0.9f, hi.z), MaterialId::WoodLaminate, mesh::FaceAll, false);
        const float top = y + 0.9f;
        if (west) {
            // Coffee maker, its carafe, two mugs.
            const glm::vec3 c = w.face + w.right * -1.0f + w.n * 0.3f;
            solid(glm::vec3(c.x - 0.15f, top, c.z - 0.15f), glm::vec3(c.x + 0.15f, top + 0.40f, c.z + 0.12f), MaterialId::DarkPlastic);
            mesh::addCylinder(m_bp.staticMesh, glm::translate(glm::mat4(1.0f), glm::vec3(c.x, top, c.z + 0.05f)), 0.07f, 0.0f, 0.16f, 14,
                              MaterialId::WaterBottle);
            for (float x : {0.0f, 0.25f}) {
                add(FurnitureType::Mug, w.face + w.right * x + w.n * 0.3f + glm::vec3(0.0f, 0.9f, 0.0f), m_rng.range(0.0f, 6.28f));
            }
            notice(w, 1.4f, 1.55f);
        } else {
            // Microwave and fridge.
            const glm::vec3 m = w.face + w.right * 0.6f + w.n * 0.3f;
            solid(glm::vec3(m.x - 0.26f, top, m.z - 0.18f), glm::vec3(m.x + 0.26f, top + 0.30f, m.z + 0.18f), MaterialId::GrayMetal);
            solid(glm::vec3(m.x - 0.24f, top + 0.03f, m.z + 0.18f), glm::vec3(m.x + 0.1f, top + 0.27f, m.z + 0.19f), MaterialId::DarkPlastic,
                  mesh::FaceAll & ~mesh::FaceNegZ, false);
            const glm::vec3 f = w.face + w.right * 1.95f + w.n * 0.36f;
            solid(glm::vec3(f.x - 0.36f, y, f.z - 0.36f), glm::vec3(f.x + 0.36f, y + 1.8f, f.z + 0.34f), MaterialId::GrayMetal);
            solid(glm::vec3(f.x + 0.28f, y + 0.9f, f.z + 0.34f), glm::vec3(f.x + 0.31f, y + 1.5f, f.z + 0.38f), MaterialId::DarkPlastic,
                  mesh::FaceAll, false); // its handle
            // The note on the fridge.
            const Wall door{f + w.n * 0.34f, w.n, w.right};
            panel(door, -0.1f, 1.35f, {0.2f, 0.26f}, atlas::Sign::Paper, 0.002f, MaterialId::OfficePaper);
            fittedText(door, -0.1f, 1.45f, 0.002f, "PLEASE LABEL\nYOUR FOOD.\nUNLABELED FOOD\nWILL BE THROWN\nOUT FRIDAY", decals::Ink::Marker,
                       {0.17f, 0.2f}, 0.025f, 0.6f);
        }
        // A table and three chairs.
        const glm::vec3 t = m_centre + glm::vec3(0.0f, 0.0f, 0.4f);
        add(FurnitureType::BreakTable, t, 0.0f);
        for (int i = 0; i < 3; ++i) {
            const float a = 2.0f * kPi * (static_cast<float>(i) + 0.25f) / 3.0f + m_rng.range(-0.3f, 0.3f);
            const glm::vec3 d(std::cos(a), 0.0f, std::sin(a));
            add(FurnitureType::Chair, t + d * 0.85f, yawFacing(-d) + m_rng.range(-0.3f, 0.3f));
        }
        const Wall side = wallOf(m_centre, west ? 0 : 1);
        if (west) {
            add(FurnitureType::WaterCooler, m_min + glm::vec3(0.45f, 0.0f, 4.4f), yawFacing(glm::vec3(1, 0, -1)));
            panel(side, 0.3f, 1.5f, {0.42f, 0.3f}, atlas::Sign::Paper, 0.002f, MaterialId::OfficePaper);
            fittedText(side, 0.3f, 1.62f, 0.002f, "CLEAN UP AFTER YOURSELF.\nYOUR MOTHER DOES NOT\nWORK HERE.", decals::Ink::Print,
                       {0.36f, 0.22f}, 0.03f);
        } else {
            add(FurnitureType::TrashCan, m_min + glm::vec3(4.5f, 0.0f, 4.4f), 0.0f);
            poster(side, -0.6f, 1.5f);
        }
    }

    /// Conference room B: a long table across both cells, chairs, a phone in
    /// the middle, a whiteboard of objectives and a chart that only goes up.
    void conference() {
        const bool west = m_gx == kConferenceDoor.x;
        const Wall outer = wallOf(m_centre, 3);
        add(FurnitureType::Fern, outer.face + outer.n * 0.5f + outer.right * (west ? 1.9f : -1.9f), 0.0f);
        if (!west) {
            const Wall side = wallOf(m_centre, 1);
            panel(side, 0.0f, 1.5f, {0.9f, 0.7f}, atlas::Sign::Chart, 0.004f, MaterialId::GrayMetal);
            fittedText(side, 0.0f, 1.83f, 0.004f, "Q3 PROFIT", decals::Ink::Print, {0.6f, 0.05f}, 0.045f);
            return;
        }
        const glm::vec3 centre(static_cast<float>(m_gx + 1) * S, m_origin.y, m_centre.z);
        const FurnitureInstance& table = add(FurnitureType::ConferenceTable, centre, 0.0f);
        const glm::mat4 tableModel = table.model;
        m_bp.phones.push_back({nextId(kSaltPhone), glm::rotate(glm::translate(tableModel, glm::vec3(0.0f, 0.76f, 0.1f)), kPi, kUp)});
        for (float x : {-1.2f, 0.0f, 1.2f}) {
            for (float s : {-1.0f, 1.0f}) {
                if (!m_rng.chance(0.9f)) continue;
                add(FurnitureType::Chair, centre + glm::vec3(x + m_rng.range(-0.15f, 0.15f), 0.0f, s * 0.95f),
                    yawFacing(glm::vec3(0.0f, 0.0f, -s)) + m_rng.range(-0.4f, 0.4f));
            }
        }
        add(FurnitureType::ExecChair, centre + glm::vec3(-2.3f, 0.0f, 0.0f), yawFacing(glm::vec3(1, 0, 0)));
        add(FurnitureType::PaperStack, centre + glm::vec3(0.8f, 0.76f, -0.2f), 0.3f);
        whiteboard(wallOf(m_centre, 0), 0.0f, 1.45f);
    }

    /// An exit lobby: an emergency exit in the outside wall - a door with a
    /// push bar, the sign glowing over it, a notice, and a note - that is
    /// only drawn on the wall.
    void exitLobby() {
        const int outerSide = m_gx == 0 ? 0 : 1;
        const Wall w = wallOf(m_centre, outerSide);
        const float y = m_origin.y;
        const glm::vec3 c = w.face;
        const glm::vec3 n = w.n;
        // Frame and door leaf, flat against the wall.
        auto slab = [&](float a0, float a1, float y0, float y1, float d0, float d1, MaterialId mat) {
            const glm::vec3 p0 = c + w.right * a0 + n * d0 + kUp * y0, p1 = c + w.right * a1 + n * d1 + kUp * y1;
            solid(glm::min(p0, p1), glm::max(p0, p1), mat, mesh::FaceAll, false);
        };
        slab(-0.52f, -0.47f, y, y + 2.15f, 0.0f, 0.03f, MaterialId::GrayMetal);
        slab(0.47f, 0.52f, y, y + 2.15f, 0.0f, 0.03f, MaterialId::GrayMetal);
        slab(-0.52f, 0.52f, y + 2.1f, y + 2.15f, 0.0f, 0.03f, MaterialId::GrayMetal);
        slab(-0.46f, 0.46f, y + 0.01f, y + 2.09f, 0.0f, 0.02f, MaterialId::GrayMetal);
        slab(-0.4f, 0.4f, y + 0.98f, y + 1.04f, 0.02f, 0.07f, MaterialId::Aluminum); // the push bar
        m_bp.colliders.push_back(AABB(glm::min(c - w.right * 0.52f, c + w.right * 0.52f + n * 0.07f + kUp * 2.15f),
                                      glm::max(c - w.right * 0.52f, c + w.right * 0.52f + n * 0.07f + kUp * 2.15f)));
        exitSign(w, 0.0f, 2.4f);
        const Wall leaf{c + n * 0.02f, n, w.right};
        panel(leaf, 0.0f, 1.5f, {0.34f, 0.2f}, atlas::Sign::Paper, 0.002f, MaterialId::OfficePaper);
        fittedText(leaf, 0.0f, 1.57f, 0.002f, "EMERGENCY EXIT ONLY", decals::Ink::PrintRed, {0.3f, 0.04f}, 0.03f);
        fittedText(leaf, 0.0f, 1.49f, 0.002f, "ALARM WILL SOUND", decals::Ink::Print, {0.26f, 0.03f}, 0.022f);
        // Somebody already tried.
        panel(leaf, 0.22f, 1.25f, {0.08f, 0.08f}, atlas::Sign::StickyNote, 0.001f, MaterialId::OfficePaper);
        text(leaf, 0.22f, 1.25f, 0.001f, "nice try", decals::Ink::Pen, 0.011f, decals::Align::Center, 1.0f);
        // The side walls.
        for (int s : {2, 3}) {
            const Wall side = wallOf(m_centre, s);
            if (sideEdge(m_gx, m_gz, s) != EdgeType::Wall) continue;
            if (s == 2) notice(side, 0.0f, 1.45f);
            else add(FurnitureType::Ficus, side.face + side.n * 0.45f + side.right * (outerSide == 0 ? 1.9f : -1.9f), 0.5f);
        }
    }

    ChunkBlueprint& m_bp;
    uint64_t  m_seed;
    int       m_gx, m_gz;
    glm::vec3 m_origin;
    glm::vec3 m_min{0.0f};
    glm::vec3 m_centre{0.0f};
    rnd::Rng  m_rng;
    int       m_counter = 0;
};

} // namespace

Zone zone(int gx, int gz) {
    if (!inside(gx, gz)) return Zone::Outside;
    const bool south = gz == 0, north = gz == kDepth - 1, west = gx == 0, east = gx == kWidth - 1;
    if ((south || north) && (gx <= 1 || gx >= kWidth - 2)) return Zone::CornerSuite;
    if (south) return (gx == 7 || gx == 8) ? Zone::Breakroom : Zone::Office;
    if (north) return (gx == 11 || gx == 12) ? Zone::Conference : Zone::Office;
    if (west) return gz == 5 ? Zone::ExitLobby : Zone::Office;
    if (east) return gz == 6 ? Zone::ExitLobby : Zone::Office;
    if (gx == 1 || gx == kWidth - 2 || gz == 1 || gz == kDepth - 2) return Zone::Corridor;
    if ((gx - 2) % 5 == 4 || (gz - 2) % 4 == 3) return Zone::Aisle;
    return Zone::Cubicles;
}

EdgeType edge(int level, int gx, int gz, EdgeAxis axis) {
    if (level != kLevel) return EdgeType::Open;
    const glm::ivec2 a = axis == EdgeAxis::West ? glm::ivec2(gx - 1, gz) : glm::ivec2(gx, gz - 1);
    const glm::ivec2 b(gx, gz);
    const Zone za = zone(a.x, a.y), zb = zone(b.x, b.y);
    const bool ia = za != Zone::Outside, ib = zb != Zone::Outside;
    if (!ia && !ib) return EdgeType::Open;
    if (ia != ib) return EdgeType::Wall; // the building's outside wall
    const bool ra = isRoom(za), rb = isRoom(zb);
    if (ra && rb) return roomId(a.x, a.y) == roomId(b.x, b.y) ? EdgeType::Open : EdgeType::Wall;
    if (!ra && !rb) return EdgeType::Open; // the open-plan floor
    // A room meets the corridor.
    const glm::ivec2 room = ra ? a : b;
    switch (ra ? za : zb) {
    case Zone::Breakroom:
    case Zone::ExitLobby:  return EdgeType::Archway;
    case Zone::Conference: return room == kConferenceDoor ? EdgeType::Door : EdgeType::Wall;
    default:               return EdgeType::Door;
    }
}

std::vector<glm::vec2> fixtures(int gx, int gz) {
    const float tile = world::kCeilingTileSize;
    switch (zone(gx, gz)) {
    case Zone::Outside:   return {};
    case Zone::Office:
    case Zone::ExitLobby: return {{3.5f * tile, 4.0f * tile}};
    default:              return {{3.5f * tile, 2.0f * tile}, {3.5f * tile, 6.0f * tile}};
    }
}

void furnish(ChunkBlueprint& bp, uint64_t seed) {
    if (bp.coord.level != kLevel) return;
    const int n = world::kChunkCells;
    for (int lz = 0; lz < n; ++lz) {
        for (int lx = 0; lx < n; ++lx) {
            Furnisher(bp, seed, bp.coord.x * n + lx, bp.coord.z * n + lz).build();
        }
    }
}

void spawn(glm::vec3& feet, float& yaw) {
    const glm::vec3 centre((static_cast<float>(kSpawnX) + 0.5f) * S, world::levelFloorY(kLevel), (static_cast<float>(kSpawnZ) + 0.5f) * S);
    const float sx = static_cast<float>(kSpawnQx), sz = static_cast<float>(kSpawnQz);
    const float deskZ = centre.z + sz * (kPod - kPanelHalf - 0.375f - 0.01f);
    feet = glm::vec3(centre.x + sx * kPod * 0.5f, centre.y, deskZ - sz * (0.375f + 0.55f));
    // Looking at the desk: along +sz (Camera yaw 0 looks down -z).
    yaw = sz < 0.0f ? 0.0f : kPi;
}

} // namespace office
