// ---------------------------------------------------------------------------
// WorldGenerator.cpp
// ---------------------------------------------------------------------------
#include "World/WorldGenerator.h"

#include "Math/Random.h"
#include "Render/MeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using world::EdgeAxis;
using world::EdgeType;

namespace {

// Salts separating independent random streams derived from the same coords.
constexpr uint64_t kSaltChunk         = 0xC4A7'0001ull;
constexpr uint64_t kSaltLayout        = 0x1A70'0002ull;
constexpr uint64_t kSaltWestBoundary  = 0xB0DE'0003ull;
constexpr uint64_t kSaltSouthBoundary = 0xB0DE'0004ull;
constexpr uint64_t kSaltPillar        = 0x9111'0005ull;
constexpr uint64_t kSaltDoor          = 0xD00B'0006ull;
constexpr uint64_t kSaltLights        = 0x7167'0007ull;
constexpr uint64_t kSaltFurniture     = 0xF0E1'0008ull;

constexpr float S  = world::kCellSize;
constexpr float H  = world::kCeilingHeight;
constexpr float HT = world::kWallHalf;

constexpr size_t kMaxCachedLayouts = 4096;

/// Minimal union-find for randomised Kruskal.
class DisjointSet {
public:
    explicit DisjointSet(int n) : m_parent(static_cast<size_t>(n)) { std::iota(m_parent.begin(), m_parent.end(), 0); }
    int find(int x) {
        while (m_parent[static_cast<size_t>(x)] != x) {
            m_parent[static_cast<size_t>(x)] = m_parent[static_cast<size_t>(m_parent[static_cast<size_t>(x)])];
            x = m_parent[static_cast<size_t>(x)];
        }
        return x;
    }
    /// Returns true if the sets were distinct (i.e. the edge joins a tree).
    bool unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return false;
        m_parent[static_cast<size_t>(a)] = b;
        return true;
    }

private:
    std::vector<int> m_parent;
};

/// Picks a passable edge type with the given door / archway probabilities.
EdgeType pickPassable(rnd::Rng& rng, float doorBias, float archBias) {
    const float p = rng.nextFloat();
    if (p < doorBias) return EdgeType::Door;
    if (p < doorBias + archBias) return EdgeType::Archway;
    return EdgeType::Open;
}

/// Adds a (world-space) box to the static mesh with chunk-local UVs and,
/// optionally, a matching collider.
void addSolid(ChunkBlueprint& bp, const glm::vec3& origin, const AABB& box, MaterialId mat, uint8_t faces,
              bool collide = true) {
    mesh::BoxDesc d;
    d.min = box.min;
    d.max = box.max;
    d.material = mat;
    d.faces = faces;
    d.uvOrigin = origin;
    mesh::addBox(bp.staticMesh, d);
    if (collide) bp.colliders.push_back(box);
}

/// Geometry helper describing one edge in world space.
struct EdgeFrame {
    EdgeAxis  axis;
    glm::vec2 start; ///< World x/z of the edge's first vertex.

    /// Box spanning [s0, s1] along the edge, [y0, y1] vertically and
    /// +-halfDepth across it.
    AABB box(float s0, float s1, float y0, float y1, float halfDepth) const {
        if (axis == EdgeAxis::South) { // runs along +X at z = start.y
            return {glm::vec3(start.x + s0, y0, start.y - halfDepth), glm::vec3(start.x + s1, y1, start.y + halfDepth)};
        }
        // West: runs along +Z at x = start.x
        return {glm::vec3(start.x - halfDepth, y0, start.y + s0), glm::vec3(start.x + halfDepth, y1, start.y + s1)};
    }
    uint8_t longFaces() const { return axis == EdgeAxis::South ? (mesh::FacePosZ | mesh::FaceNegZ) : (mesh::FacePosX | mesh::FaceNegX); }
    uint8_t startCap() const { return axis == EdgeAxis::South ? mesh::FaceNegX : mesh::FaceNegZ; }
    uint8_t endCap() const { return axis == EdgeAxis::South ? mesh::FacePosX : mesh::FacePosZ; }
    glm::vec3 alongDir() const { return axis == EdgeAxis::South ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1); }
    glm::vec3 point(float s) const { return glm::vec3(start.x, 0.0f, start.y) + alongDir() * s; }
};

/// Whether light can pass an edge at position `along` (0..cell size).
bool edgeBlocksLight(EdgeType type, float along) {
    switch (type) {
    case EdgeType::Open:    return false;
    case EdgeType::Archway: return std::fabs(along - S * 0.5f) > world::kArchWidth * 0.5f - 0.05f;
    case EdgeType::Wall:
    case EdgeType::Door:
    default:                return true; // doors are treated as closed for lighting
    }
}

} // namespace

WorldGenerator::WorldGenerator(uint64_t worldSeed) : m_seed(worldSeed) {}

uint64_t WorldGenerator::chunkSeed(const ChunkCoord& c) const {
    return rnd::hashCoords(m_seed, c.x, c.z, kSaltChunk);
}

// ----- Layout -----------------------------------------------------------------

void WorldGenerator::trimCache() const {
    if (m_layoutCache.size() > kMaxCachedLayouts) m_layoutCache.clear();
}

const WorldGenerator::ChunkLayout& WorldGenerator::layout(const ChunkCoord& c) const {
    auto it = m_layoutCache.find(c);
    if (it != m_layoutCache.end()) return it->second;
    return m_layoutCache.emplace(c, buildLayout(c)).first->second;
}

WorldGenerator::ChunkLayout WorldGenerator::buildLayout(const ChunkCoord& c) const {
    ChunkLayout L;
    L.west.fill(EdgeType::Wall);
    L.south.fill(EdgeType::Wall);

    rnd::Rng rng(rnd::hashCoords(m_seed, c.x, c.z, kSaltLayout));
    // Per-chunk character: from maze-like corridors to cavernous open halls.
    const float openness = rng.range(0.20f, 0.85f);
    const float archBias = rng.range(0.15f, 0.40f);
    const float doorBias = rng.range(0.10f, 0.30f);

    auto idx = [](int lx, int lz) { return lz * kN + lx; };

    // Interior edges: randomised Kruskal guarantees a spanning tree of
    // passable edges; the remaining edges open up with probability `openness`.
    struct Candidate {
        bool west;
        int  lx, lz;
        int  a, b;
    };
    std::vector<Candidate> edges;
    edges.reserve(2 * kN * kN);
    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            if (lx > 0) edges.push_back({true, lx, lz, idx(lx - 1, lz), idx(lx, lz)});
            if (lz > 0) edges.push_back({false, lx, lz, idx(lx, lz - 1), idx(lx, lz)});
        }
    }
    for (size_t i = edges.size(); i > 1; --i) { // Fisher-Yates with our deterministic RNG
        const size_t j = static_cast<size_t>(rng.next() % static_cast<uint32_t>(i));
        std::swap(edges[i - 1], edges[j]);
    }
    DisjointSet sets(kN * kN);
    for (const Candidate& e : edges) {
        const bool treeEdge = sets.unite(e.a, e.b);
        const EdgeType t = (treeEdge || rng.chance(openness)) ? pickPassable(rng, doorBias, archBias) : EdgeType::Wall;
        (e.west ? L.west : L.south)[static_cast<size_t>(idx(e.lx, e.lz))] = t;
    }

    // Boundary edges (owned by this chunk: its west and south borders). Each
    // border has one guaranteed passable edge so neighbouring chunks connect.
    {
        rnd::Rng b(rnd::hashCoords(m_seed, c.x, c.z, kSaltWestBoundary));
        const int guaranteed = b.rangeInt(0, kN - 1);
        for (int lz = 0; lz < kN; ++lz) {
            const bool open = lz == guaranteed || b.chance(0.5f);
            L.west[static_cast<size_t>(idx(0, lz))] = open ? pickPassable(b, 0.2f, 0.3f) : EdgeType::Wall;
        }
    }
    {
        rnd::Rng b(rnd::hashCoords(m_seed, c.x, c.z, kSaltSouthBoundary));
        const int guaranteed = b.rangeInt(0, kN - 1);
        for (int lx = 0; lx < kN; ++lx) {
            const bool open = lx == guaranteed || b.chance(0.5f);
            L.south[static_cast<size_t>(idx(lx, 0))] = open ? pickPassable(b, 0.2f, 0.3f) : EdgeType::Wall;
        }
    }
    return L;
}

EdgeType WorldGenerator::edge(int gx, int gz, EdgeAxis axis) const {
    const ChunkCoord c{world::floorDiv(gx, kN), world::floorDiv(gz, kN)};
    const int lx = world::floorMod(gx, kN);
    const int lz = world::floorMod(gz, kN);
    const ChunkLayout& L = layout(c);
    const size_t i = static_cast<size_t>(lz * kN + lx);
    return axis == EdgeAxis::West ? L.west[i] : L.south[i];
}

bool WorldGenerator::vertexHasWall(int gx, int gz) const {
    return edge(gx, gz, EdgeAxis::South) != EdgeType::Open ||     // +X
           edge(gx - 1, gz, EdgeAxis::South) != EdgeType::Open || // -X
           edge(gx, gz, EdgeAxis::West) != EdgeType::Open ||      // +Z
           edge(gx, gz - 1, EdgeAxis::West) != EdgeType::Open;    // -Z
}

bool WorldGenerator::vertexHasPillar(int gx, int gz) const {
    if (vertexHasWall(gx, gz)) return false;
    return rnd::toUnit(rnd::hashCoords(m_seed, gx, gz, kSaltPillar)) < world::kPillarChance;
}

bool WorldGenerator::isLightBlocked(const glm::vec2& a, const glm::vec2& b) const {
    // Crossings of vertical grid lines x = k*S (west edges).
    if (a.x != b.x) {
        const float lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
        for (int k = static_cast<int>(std::ceil(lo / S)); k <= static_cast<int>(std::floor(hi / S)); ++k) {
            const float x = static_cast<float>(k) * S;
            if (x <= lo || x >= hi) continue;
            const float t = (x - a.x) / (b.x - a.x);
            const float z = a.y + t * (b.y - a.y);
            const int gz = static_cast<int>(std::floor(z / S));
            if (edgeBlocksLight(edge(k, gz, EdgeAxis::West), z - static_cast<float>(gz) * S)) return true;
        }
    }
    // Crossings of horizontal grid lines z = k*S (south edges).
    if (a.y != b.y) {
        const float lo = std::min(a.y, b.y), hi = std::max(a.y, b.y);
        for (int k = static_cast<int>(std::ceil(lo / S)); k <= static_cast<int>(std::floor(hi / S)); ++k) {
            const float z = static_cast<float>(k) * S;
            if (z <= lo || z >= hi) continue;
            const float t = (z - a.y) / (b.y - a.y);
            const float x = a.x + t * (b.x - a.x);
            const int gx = static_cast<int>(std::floor(x / S));
            if (edgeBlocksLight(edge(gx, k, EdgeAxis::South), x - static_cast<float>(gx) * S)) return true;
        }
    }
    return false;
}

// ----- Chunk content ----------------------------------------------------------

ChunkBlueprint WorldGenerator::generate(const ChunkCoord& c) const {
    trimCache(); // safe here: no layout references are alive between calls

    ChunkBlueprint bp;
    bp.coord = c;
    bp.seed = chunkSeed(c);
    const glm::vec3 origin(c.originX(), 0.0f, c.originZ());

    buildShell(bp, origin);
    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            const int gx = c.x * kN + lx;
            const int gz = c.z * kN + lz;
            buildEdge(bp, origin, gx, gz, EdgeAxis::West);
            buildEdge(bp, origin, gx, gz, EdgeAxis::South);
            buildVertex(bp, origin, gx, gz);
        }
    }
    placeLights(bp, origin);
    placeFurniture(bp, origin);

    // Bounds include a margin for doors swinging into neighbouring chunks.
    bp.bounds = AABB(origin + glm::vec3(-1.0f, 0.0f, -1.0f),
                     origin + glm::vec3(world::kChunkSize + 1.0f, H, world::kChunkSize + 1.0f));
    return bp;
}

void WorldGenerator::buildShell(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const glm::vec3 far = origin + glm::vec3(world::kChunkSize, 0.0f, world::kChunkSize);
    // Carpet floor: the top face of a slab below y = 0 (collision is analytic).
    addSolid(bp, origin, AABB(glm::vec3(origin.x, -0.1f, origin.z), glm::vec3(far.x, 0.0f, far.z)),
             MaterialId::Carpet, mesh::FacePosY, false);
    // Acoustic tile ceiling: the bottom face of a slab above the ceiling height.
    addSolid(bp, origin, AABB(glm::vec3(origin.x, H, origin.z), glm::vec3(far.x, H + 0.1f, far.z)),
             MaterialId::CeilingTile, mesh::FaceNegY, false);
}

void WorldGenerator::buildEdge(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz, EdgeAxis axis) const {
    const EdgeType type = edge(gx, gz, axis);
    if (type == EdgeType::Open) return;

    const EdgeFrame f{axis, glm::vec2(static_cast<float>(gx) * S, static_cast<float>(gz) * S)};
    const float s0 = HT;          // wall runs between the corner posts
    const float s1 = S - HT;
    const float mid = S * 0.5f;

    if (type == EdgeType::Wall) {
        addSolid(bp, origin, f.box(s0, s1, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces());
        return;
    }

    if (type == EdgeType::Archway) {
        const float hw = world::kArchWidth * 0.5f;
        addSolid(bp, origin, f.box(s0, mid - hw, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces() | f.endCap());
        addSolid(bp, origin, f.box(mid + hw, s1, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces() | f.startCap());
        addSolid(bp, origin, f.box(mid - hw, mid + hw, world::kArchHeight, H, HT), MaterialId::Wallpaper,
                 f.longFaces() | mesh::FaceNegY);
        return;
    }

    // ---- Door frame -------------------------------------------------------------
    const float hw = world::kDoorOpeningWidth * 0.5f;
    const float jw = world::kDoorFrameWidth;
    const float hd = world::kDoorOpeningHeight;
    const float frameDepth = HT + world::kDoorFrameProtrude;

    // Wall segments either side and the header above (reveals hidden by the frame).
    addSolid(bp, origin, f.box(s0, mid - hw, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces());
    addSolid(bp, origin, f.box(mid + hw, s1, 0.0f, H, HT), MaterialId::Wallpaper, f.longFaces());
    addSolid(bp, origin, f.box(mid - hw, mid + hw, hd, H, HT), MaterialId::Wallpaper, f.longFaces());

    // Painted steel frame: two jambs and a head jamb.
    addSolid(bp, origin, f.box(mid - hw, mid - hw + jw, 0.0f, hd, frameDepth), MaterialId::GrayMetal,
             mesh::FaceSides);
    addSolid(bp, origin, f.box(mid + hw - jw, mid + hw, 0.0f, hd, frameDepth), MaterialId::GrayMetal,
             mesh::FaceSides);
    addSolid(bp, origin, f.box(mid - hw + jw, mid + hw - jw, hd - jw, hd, frameDepth), MaterialId::GrayMetal,
             f.longFaces() | mesh::FaceNegY);

    // Door leaf: hinge side chosen deterministically from the edge hash.
    const uint64_t id = rnd::hashCoords(m_seed, gx, gz, kSaltDoor + static_cast<uint64_t>(axis));
    const bool hingeAtStart = (id & 1u) == 0u;
    DoorPlacement door;
    door.id = id;
    if (hingeAtStart) {
        door.hinge = f.point(mid - hw + jw);
        door.closedDir = f.alongDir();
    } else {
        door.hinge = f.point(mid + hw - jw);
        door.closedDir = -f.alongDir();
    }
    bp.doors.push_back(door);
}

void WorldGenerator::buildVertex(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz) const {
    const glm::vec3 p(static_cast<float>(gx) * S, 0.0f, static_cast<float>(gz) * S);

    const bool px = edge(gx, gz, EdgeAxis::South) != EdgeType::Open;
    const bool nx = edge(gx - 1, gz, EdgeAxis::South) != EdgeType::Open;
    const bool pz = edge(gx, gz, EdgeAxis::West) != EdgeType::Open;
    const bool nz = edge(gx, gz - 1, EdgeAxis::West) != EdgeType::Open;

    if (px || nx || pz || nz) {
        // Corner post joining the walls; faces hidden against walls are skipped.
        uint8_t faces = 0;
        if (!px) faces |= mesh::FacePosX;
        if (!nx) faces |= mesh::FaceNegX;
        if (!pz) faces |= mesh::FacePosZ;
        if (!nz) faces |= mesh::FaceNegZ;
        const AABB post(p + glm::vec3(-HT, 0.0f, -HT), p + glm::vec3(HT, H, HT));
        mesh::BoxDesc d;
        d.min = post.min;
        d.max = post.max;
        d.material = MaterialId::Wallpaper;
        d.faces = faces;
        d.uvOrigin = origin;
        if (faces) mesh::addBox(bp.staticMesh, d);
        bp.colliders.push_back(post);
        return;
    }

    if (vertexHasPillar(gx, gz)) {
        const float h = world::kPillarSize * 0.5f;
        addSolid(bp, origin, AABB(p + glm::vec3(-h, 0.0f, -h), p + glm::vec3(h, H, h)), MaterialId::Wallpaper,
                 mesh::FaceSides);
    }
}

void WorldGenerator::placeLights(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const float tile = world::kCeilingTileSize;
    const float depth = world::kFixtureDepth;
    // Chunk-wide electrical "unrest" (squared -> most chunks are calm).
    rnd::Rng chunkRng(rnd::hashCombine(bp.seed, kSaltLights));
    const float unrest = chunkRng.nextFloat() * chunkRng.nextFloat();

    int lightIndex = 0;
    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            rnd::Rng rng(rnd::hashCoords(bp.seed, lx, lz, kSaltLights));
            const float roll = rng.nextFloat();
            if (roll < 0.07f) continue; // occasional cell with no fixture at all

            // Fixture centres in cell-local coordinates, snapped to the ceiling
            // tile grid, expressed for a fixture whose long axis runs along Z.
            std::vector<glm::vec2> centres;
            const float x0 = (rng.chance(0.5f) ? 3.5f : 4.5f) * tile; // one tile wide
            if (roll < 0.60f) {
                centres.push_back({x0, 4.0f * tile});                  // single, centred
            } else if (roll < 0.82f) {
                centres.push_back({x0, 2.0f * tile});                  // two in line
                centres.push_back({x0, 6.0f * tile});
            } else {
                centres.push_back({2.5f * tile, 4.0f * tile});         // two side by side
                centres.push_back({5.5f * tile, 4.0f * tile});
            }
            const bool alongX = rng.chance(0.5f);

            const glm::vec3 cellMin = origin + glm::vec3(static_cast<float>(lx) * S, 0.0f, static_cast<float>(lz) * S);
            for (const glm::vec2& lc : centres) {
                const glm::vec2 local = alongX ? glm::vec2(lc.y, lc.x) : lc;
                const glm::vec2 half = alongX ? glm::vec2(world::kFixtureLength, world::kFixtureWidth) * 0.5f
                                              : glm::vec2(world::kFixtureWidth, world::kFixtureLength) * 0.5f;
                const glm::vec3 c = cellMin + glm::vec3(local.x, 0.0f, local.y);
                const float x0w = c.x - half.x, x1w = c.x + half.x;
                const float z0w = c.z - half.y, z1w = c.z + half.y;
                const float yb = H - depth;

                // Housing sides (metal).
                mesh::BoxDesc housing;
                housing.min = glm::vec3(x0w, yb, z0w);
                housing.max = glm::vec3(x1w, H, z1w);
                housing.material = MaterialId::GrayMetal;
                housing.faces = mesh::FaceSides;
                housing.uvOrigin = origin;
                mesh::addBox(bp.staticMesh, housing);

                // Emissive diffuser facing down, UV (0..1) across the fixture:
                // u across the short side, v along the long side (tube direction).
                const glm::vec3 corners[4] = {{x0w, yb, z0w}, {x1w, yb, z0w}, {x1w, yb, z1w}, {x0w, yb, z1w}};
                const glm::vec2 uvZ[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                const glm::vec2 uvX[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
                mesh::addQuad(bp.staticMesh, corners, glm::vec3(0, -1, 0), alongX ? uvX : uvZ,
                              MaterialId::LightPanel, static_cast<float>(lightIndex));

                LightFixture light(glm::vec3(c.x, yb - 0.005f, c.z), half,
                                   rnd::hashCombine(bp.seed, static_cast<uint64_t>(lightIndex)), unrest);
                computeLightVisibility(light);
                bp.lights.push_back(std::move(light));
                ++lightIndex;
            }
        }
    }
}

void WorldGenerator::computeLightVisibility(LightFixture& light) const {
    const float gs = world::kLightGridCell;
    const float R = world::kLightRange;
    const glm::vec2 c(light.center().x, light.center().z);
    const int gx0 = static_cast<int>(std::floor((c.x - R) / gs));
    const int gx1 = static_cast<int>(std::floor((c.x + R) / gs));
    const int gz0 = static_cast<int>(std::floor((c.y - R) / gs));
    const int gz1 = static_cast<int>(std::floor((c.y + R) / gs));
    const float inset = 0.25f;

    std::vector<glm::ivec2>& cells = light.visibleCells();
    cells.clear();
    for (int gz = gz0; gz <= gz1; ++gz) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            const glm::vec2 mn(static_cast<float>(gx) * gs, static_cast<float>(gz) * gs);
            const glm::vec2 mx = mn + glm::vec2(gs);
            const glm::vec2 nearest = glm::clamp(c, mn, mx);
            if (glm::length(nearest - c) > R) continue; // outside the light's range

            // The cell is lit if any of five sample points has line of sight.
            const bool containsLight = c.x >= mn.x && c.x < mx.x && c.y >= mn.y && c.y < mx.y;
            bool visible = containsLight;
            const glm::vec2 samples[5] = {(mn + mx) * 0.5f,
                                          {mn.x + inset, mn.y + inset}, {mx.x - inset, mn.y + inset},
                                          {mx.x - inset, mx.y - inset}, {mn.x + inset, mx.y - inset}};
            for (int i = 0; i < 5 && !visible; ++i) {
                visible = !isLightBlocked(c, samples[i]);
            }
            if (visible) cells.emplace_back(gx, gz);
        }
    }
}

void WorldGenerator::placeFurniture(ChunkBlueprint& bp, const glm::vec3& origin) const {
    const float T = world::kWallThickness;

    for (int lz = 0; lz < kN; ++lz) {
        for (int lx = 0; lx < kN; ++lx) {
            const int gx = bp.coord.x * kN + lx;
            const int gz = bp.coord.z * kN + lz;
            rnd::Rng rng(rnd::hashCoords(bp.seed, lx, lz, kSaltFurniture));

            const glm::vec3 cellMin = origin + glm::vec3(static_cast<float>(lx) * S, 0.0f, static_cast<float>(lz) * S);
            const glm::vec3 cellCenter = cellMin + glm::vec3(S * 0.5f, 0.0f, S * 0.5f);

            // Solid walls of this cell: 0 = west, 1 = east, 2 = south, 3 = north.
            const EdgeType sides[4] = {edge(gx, gz, EdgeAxis::West), edge(gx + 1, gz, EdgeAxis::West),
                                       edge(gx, gz, EdgeAxis::South), edge(gx, gz + 1, EdgeAxis::South)};
            const glm::vec3 inward[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
            std::vector<int> walls;
            for (int i = 0; i < 4; ++i) {
                if (sides[i] == EdgeType::Wall) walls.push_back(i);
            }

            // Orientation helpers: yaw mapping local +Z / +X onto a direction.
            auto yawFacing = [](const glm::vec3& dir) { return std::atan2(dir.x, dir.z); };
            auto yawAlongX = [](const glm::vec3& dir) { return std::atan2(-dir.z, dir.x); };
            auto add = [&bp](FurnitureType t, const glm::vec3& p, float yaw) {
                bp.furniture.push_back(Furniture::makeInstance(t, p, yaw));
            };

            // Frame of a wall side: inner face midpoint, inward normal, along-wall axis.
            auto wallFrame = [&](int side, glm::vec3& face, glm::vec3& n, glm::vec3& r) {
                n = inward[side];
                r = glm::vec3(-n.z, 0.0f, n.x);
                face = cellCenter - n * (S * 0.5f - T * 0.5f);
            };

            // A desk against the wall, a chair pulled up to it, optional
            // partitions and a filing cabinet: a classic cubicle.
            auto cubicle = [&](int side, bool partitions) {
                glm::vec3 face, n, r;
                wallFrame(side, face, n, r);
                const glm::vec3 base = face + r * rng.range(-0.5f, 0.5f);
                add(FurnitureType::Desk, base + n * (0.375f + 0.03f), yawFacing(n));
                add(FurnitureType::Chair,
                    base + n * (0.78f + rng.range(0.25f, 0.55f)) + r * rng.range(-0.3f, 0.3f),
                    yawFacing(-n) + rng.range(-0.6f, 0.6f));
                if (partitions) {
                    for (float s : {-1.0f, 1.0f}) {
                        add(FurnitureType::Partition, base + r * (s * 0.80f) + n * 0.76f, yawAlongX(n));
                    }
                }
                if (rng.chance(0.5f)) {
                    const float s = rng.chance(0.5f) ? 1.0f : -1.0f;
                    add(FurnitureType::FileCabinet, base + r * (s * 1.09f) + n * 0.345f, yawFacing(n));
                }
            };

            const float roll = rng.nextFloat();
            if (!walls.empty() && roll < 0.24f) {
                const int side = walls[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(walls.size()) - 1))];
                cubicle(side, true);
                // Occasionally a second cubicle on the opposite wall.
                const int opposite = side ^ 1;
                if (sides[opposite] == EdgeType::Wall && rng.chance(0.35f)) cubicle(opposite, true);
            } else if (!walls.empty() && roll < 0.40f) {
                // A row of filing cabinets pushed against a wall, some askew.
                const int side = walls[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(walls.size()) - 1))];
                glm::vec3 face, n, r;
                wallFrame(side, face, n, r);
                const int count = rng.rangeInt(1, 3);
                const float start = rng.range(-1.2f, 1.2f - 0.47f * static_cast<float>(count - 1));
                for (int i = 0; i < count; ++i) {
                    const float gap = rng.range(0.0f, 0.06f);
                    add(FurnitureType::FileCabinet,
                        face + r * (start + 0.47f * static_cast<float>(i)) + n * (0.345f + gap),
                        yawFacing(n) + rng.range(-0.06f, 0.06f));
                }
            } else if (roll < 0.47f) {
                // An abandoned chair drifting somewhere in the room.
                const glm::vec3 p = cellMin + glm::vec3(rng.range(1.4f, 3.6f), 0.0f, rng.range(1.4f, 3.6f));
                add(FurnitureType::Chair, p, rng.range(0.0f, 6.2831853f));
            } else if (!walls.empty() && roll < 0.53f) {
                const int side = walls[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(walls.size()) - 1))];
                cubicle(side, false);
            }
        }
    }
}
