// ---------------------------------------------------------------------------
// EngineTesla.cpp
// The Engine's side of the Tesla coil gun: searching filing cabinets,
// taking and swapping parts, putting the gun together, holding and firing
// it, and the HUD that goes with it. (The simulation of the discharge
// itself is Gameplay/TeslaGun; item persistence lives in the ChunkManager.)
// ---------------------------------------------------------------------------
#include "Core/Engine.h"

#include "AI/EntityDirector.h"
#include "Actors/FileCabinet.h"
#include "Actors/Player.h"
#include "Audio/Soundscape.h"
#include "Gameplay/TeslaGun.h"
#include "Physics/Physics.h"
#include "Render/Renderer.h"
#include "World/ChunkManager.h"
#include "World/WorldGenerator.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265f;

inline float yawToward(const glm::vec3& d) { return std::atan2(-d.x, -d.z); }
inline float pitchToward(const glm::vec3& d) { return std::asin(std::clamp(d.y / std::max(glm::length(d), 1e-4f), -1.0f, 1.0f)); }
inline float wrapAngle(float a) { return std::remainder(a, 2.0f * kPi); }
inline float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/// The order the parts go together in, when each one seats (seconds into
/// the assembly), and how long each takes to slide home.
constexpr PartType kAssemblyOrder[kPartTypeCount] = {PartType::Driver, PartType::Battery, PartType::Coil, PartType::TopLoad};
constexpr float kSeatTime[kPartTypeCount] = {0.35f, 0.7f, 1.05f, 1.4f};
constexpr float kSeatTravel = 0.35f;
constexpr float kPowerUpAt = 1.5f;

/// Where (gun space) each part hovers before it slides into place.
glm::vec3 explodedOffset(PartType type) {
    switch (type) {
    case PartType::Driver:        return {0.0f, -0.06f, -0.12f};
    case PartType::Battery:       return {0.0f, -0.16f, 0.02f};
    case PartType::Coil:          return {0.0f, 0.03f, 0.22f};
    default:                      return {0.0f, 0.05f, 0.36f};
    }
}

std::string percent(float charge) {
    char text[16];
    std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::round(std::clamp(charge, 0.0f, 1.0f) * 100.0f)));
    return text;
}

} // namespace

// ---- Filing cabinets ------------------------------------------------------------------------------

FileCabinet* Engine::activeCabinet() const { return m_cabinetId ? m_chunks->cabinetById(m_cabinetId) : nullptr; }

ItemSite* Engine::cabinetSite() const { return m_cabinetId ? m_chunks->drawerSite(m_cabinetId, m_cabinetDrawer) : nullptr; }

void Engine::enterCabinet(FileCabinet& cabinet, int drawer) {
    m_cabinetId = cabinet.id();
    m_cabinetDrawer = std::clamp(drawer, 0, FileCabinet::kDrawers - 1);
    cabinet.openDrawer(m_cabinetDrawer);
    // Lean over the drawer that rolls out.
    m_cabinetEye = cabinet.viewPoint(m_cabinetDrawer);
    const glm::vec3 look = cabinet.viewTarget(m_cabinetDrawer) - m_cabinetEye;
    m_cabinetYaw = yawToward(look);
    m_cabinetPitch = pitchToward(look);
    m_cabinetMove = 0;
    m_cabinetUse = false;
    m_cabinetLeave = false;
    setState(GameState::Cabinet);
}

void Engine::leaveCabinet() {
    if (FileCabinet* c = activeCabinet()) c->openDrawer(-1); // pushed shut
    m_cabinetMove = 0;
    m_cabinetUse = false;
    m_cabinetLeave = false;
    if (m_state == GameState::Cabinet) setState(GameState::Running);
    // m_cabinetId stays set while the view blends back.
}

void Engine::handleCabinetKey(const SDL_KeyboardEvent& key) {
    switch (key.scancode) {
    case SDL_SCANCODE_ESCAPE:
        if (!key.repeat) m_cabinetLeave = true;
        break;
    case SDL_SCANCODE_E:
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_SPACE:
        if (!key.repeat) m_cabinetUse = true;
        break;
    case SDL_SCANCODE_W:
    case SDL_SCANCODE_UP:
        if (!key.repeat) ++m_cabinetMove;
        break;
    case SDL_SCANCODE_S:
    case SDL_SCANCODE_DOWN:
        if (!key.repeat) --m_cabinetMove;
        break;
    case SDL_SCANCODE_F11:
        if (!key.repeat) {
            m_fullscreen = !m_fullscreen;
            SDL_SetWindowFullscreen(m_window, m_fullscreen);
        }
        break;
    case SDL_SCANCODE_F12:
        m_screenshotRequested = true;
        break;
    default:
        break;
    }
}

void Engine::updateCabinet(float dt) {
    const bool searching = m_state == GameState::Cabinet;
    const float step = dt / cfg::kCabinetOpenTime;
    m_cabinetBlend = std::clamp(m_cabinetBlend + (searching ? step : -step), 0.0f, 1.0f);
    if (!searching) {
        if (m_cabinetBlend <= 0.0f) m_cabinetId = 0;
        return;
    }
    FileCabinet* cabinet = activeCabinet();
    if (!cabinet) { // its chunk went away (should not happen while searching)
        leaveCabinet();
        return;
    }
    // One drawer out at a time: the one below / above rolls out as this one rolls shut.
    if (m_cabinetMove != 0) {
        const int d = std::clamp(m_cabinetDrawer + m_cabinetMove, 0, FileCabinet::kDrawers - 1);
        m_cabinetMove = 0;
        if (d != m_cabinetDrawer) {
            m_cabinetDrawer = d;
            cabinet->openDrawer(d);
        }
    }
    // The view glides to the drawer that is out.
    const glm::vec3 eye = cabinet->viewPoint(m_cabinetDrawer);
    const glm::vec3 look = cabinet->viewTarget(m_cabinetDrawer) - eye;
    const float k = 1.0f - std::exp(-7.0f * dt);
    m_cabinetEye += (eye - m_cabinetEye) * k;
    m_cabinetYaw += wrapAngle(yawToward(look) - m_cabinetYaw) * k;
    m_cabinetPitch += (pitchToward(look) - m_cabinetPitch) * k;

    if (m_cabinetUse) {
        m_cabinetUse = false;
        ItemSite* site = cabinetSite();
        if (site && site->item && cabinet->drawerOpen(m_cabinetDrawer) > 0.7f) takeItem(*site);
    }
    if (m_cabinetLeave) leaveCabinet();
}

// ---- Parts ------------------------------------------------------------------------------------------

std::string Engine::itemPrompt(const ItemSite& site) const {
    if (!site.item) return {};
    const Item& item = *site.item;
    const std::optional<Item>& held = m_inventory.slot(item.type);
    std::string text = std::string(held ? "SWAP FOR " : "TAKE ") + partName(item.type);
    if (item.type == PartType::Battery) {
        text += " " + percent(item.charge);
        if (held) text += "  (HOLDING " + percent(held->charge) + ")";
    }
    return text;
}

void Engine::takeItem(ItemSite& site) {
    if (!site.item) return;
    const Item found = *site.item;
    const glm::vec3 at = glm::vec3(site.world * glm::vec4(tesla::centerLocal(found.type), 1.0f));
    const Inventory::Exchange exchange = m_inventory.exchange(site.item);
    if (exchange == Inventory::Exchange::None) return;
    site.modified = true; // the chunk manager remembers what is here now
    const bool swapped = exchange == Inventory::Exchange::Swapped;
    m_sound->playEffect(swapped ? SoundId::PartSwap : SoundId::PartPickup, at, 0.6f, *m_world);
    m_noises.push_back({at, cfg::kNoiseTyping, NoiseKind::Machine});

    std::string message = std::string(swapped ? "SWAPPED FOR " : "PICKED UP ") + partName(found.type);
    if (found.type == PartType::Battery) {
        message += " " + percent(found.charge);
        if (swapped) message += "  (LEFT ONE AT " + percent(site.item->charge) + ")";
    }
    if (m_inventory.assembled()) {
        if (swapped) m_swapDip = cfg::kSwapDipTime; // hot-swapped into the gun
    } else if (m_inventory.complete()) {
        message = "ALL FOUR PARTS  -  PRESS R TO ASSEMBLE THE TESLA GUN";
    }
    showMessage(message, 4.0f);
    std::printf("[Tesla] %s %s%s (site %016llx)\n", swapped ? "Swapped for" : "Picked up", partName(found.type),
                found.type == PartType::Battery ? (" at " + percent(found.charge)).c_str() : "",
                static_cast<unsigned long long>(site.id));
}

bool Engine::beginAssembly() {
    if (!m_inventory.complete() || m_inventory.assembled() || m_assembleTimer >= 0.0f) return false;
    m_assembleTimer = 0.0f;
    m_assembleStep = 0;
    m_sound->playHeld(SoundId::PartPickup, 0.5f);
    return true;
}

// ---- The gun --------------------------------------------------------------------------------------

glm::mat4 Engine::gunTransform(const Camera& cam) const {
    const glm::mat4 toWorld = glm::inverse(cam.viewMatrix());
    const float raise = smooth01(m_gunRaise);
    const float dip = m_swapDip > 0.0f ? std::sin(kPi * (1.0f - m_swapDip / cfg::kSwapDipTime)) : 0.0f;
    const float t = static_cast<float>(m_simTime);
    // It hums in the hands while it fires; otherwise it just breathes a little.
    const float shake = m_gun ? m_gun->vibration() : 0.0f;
    const glm::vec3 jitter = shake * 0.0035f * glm::vec3(std::sin(t * 97.0f), std::sin(t * 83.0f + 1.7f), std::sin(t * 71.0f + 0.4f));
    const glm::vec3 sway(0.004f * std::sin(t * 1.1f), 0.003f * std::sin(t * 1.7f), 0.0f);
    const glm::vec3 offset = glm::vec3(0.17f, -0.215f, -0.42f) + sway + jitter +
                             glm::vec3(0.0f, -0.4f * (1.0f - raise) - 0.25f * dip, 0.0f);
    glm::mat4 m = glm::translate(toWorld, offset);
    m = glm::rotate(m, kPi + 0.04f, glm::vec3(0.0f, 1.0f, 0.0f));                       // muzzle ahead, turned in a touch
    m = glm::rotate(m, 0.9f * dip + 0.35f * (1.0f - raise) - 0.02f, glm::vec3(1.0f, 0.0f, 0.0f)); // tipped down while lowered
    return m;
}

void Engine::updateGun(float dt) {
    const bool running = m_state == GameState::Running;

    // ---- Putting it together: the parts seat one by one, then it powers up.
    if (running && m_input.keyPressed(SDL_SCANCODE_R)) beginAssembly();
    if (m_assembleTimer >= 0.0f) {
        m_assembleTimer += dt;
        while (m_assembleStep < kPartTypeCount && m_assembleTimer >= kSeatTime[m_assembleStep]) {
            m_sound->playHeld(SoundId::AssembleSnap, 1.4f);
            ++m_assembleStep;
        }
        if (m_assembleStep == kPartTypeCount && m_assembleTimer >= kPowerUpAt) {
            m_sound->playHeld(SoundId::WeaponPowerUp, 0.55f);
            ++m_assembleStep;
        }
        if (m_assembleTimer >= cfg::kAssembleTime) {
            m_inventory.assemble();
            m_assembleTimer = -1.0f;
            showMessage("TESLA GUN READY  -  HOLD LEFT MOUSE OR F TO FIRE", 5.0f);
            std::printf("[Tesla] Gun assembled, battery %s\n", percent(m_inventory.charge()).c_str());
        }
    }

    // ---- In the hands while walking about; lowered for everything else.
    const bool assembling = m_assembleTimer >= 0.0f;
    const bool hands = (m_inventory.assembled() || assembling) && running;
    m_gunRaise = std::clamp(m_gunRaise + (hands ? dt : -dt) / 0.35f, 0.0f, 1.0f);
    if (m_swapDip > 0.0f) {
        const float before = m_swapDip;
        m_swapDip = std::max(0.0f, m_swapDip - dt);
        if (before > 0.5f * cfg::kSwapDipTime && m_swapDip <= 0.5f * cfg::kSwapDipTime) {
            m_sound->playHeld(SoundId::AssembleSnap, 1.2f); // the new part clicks home
        }
    }

    // ---- The trigger.
    const Camera cam = viewCamera();
    GunContext ctx;
    ctx.muzzle = glm::vec3(gunTransform(cam) * glm::vec4(tesla::spikeTipGun(), 1.0f));
    ctx.eye = cam.position;
    ctx.aim = cam.forward();
    ctx.trigger = running && (m_input.mouseDown(SDL_BUTTON_LEFT) || m_input.keyDown(SDL_SCANCODE_F) || m_demoTrigger);
    ctx.triggerPressed = running && (m_input.mousePressed(SDL_BUTTON_LEFT) || m_input.keyPressed(SDL_SCANCODE_F));
    ctx.ready = m_inventory.assembled() && running && !assembling && m_gunRaise >= 1.0f && m_swapDip <= 0.0f;
    ctx.world = m_chunks.get();
    ctx.physics = m_physics.get();
    ctx.targets = m_entities.get();
    m_gun->update(dt, ctx, m_inventory);

    for (const GunSoundEvent& e : m_gun->takeSounds()) {
        switch (e.type) {
        case GunSoundEvent::Type::Zap:
            m_sound->playHeld(SoundId::TeslaZap, 0.5f * e.gain);
            break;
        case GunSoundEvent::Type::Hit: // the crack of the arc landing, out where it lands
            m_sound->playEffect(SoundId::TeslaZap, e.position, 0.45f * e.gain, *m_world);
            break;
        case GunSoundEvent::Type::DryClick:
            m_sound->playHeld(SoundId::TeslaDryClick, 0.78f);
            break;
        case GunSoundEvent::Type::LowBattery:
            m_sound->playHeld(SoundId::BatteryLow, 0.2f);
            break;
        case GunSoundEvent::Type::Depleted:
            m_sound->playHeld(SoundId::TeslaDryClick, 0.65f);
            showMessage("BATTERY FLAT  -  FIND ANOTHER BATTERY PACK", 4.0f);
            break;
        }
    }
    const float power = m_gun->power();
    m_sound->setArcLoop(m_gun->discharging(), 0.45f + 0.55f * power, 0.8f + 0.3f * power);
    // The roar carries: everything in the Backrooms hears the gun.
    if (m_gun->noise() > 0.0f) m_noises.push_back({cam.position, m_gun->noise(), NoiseKind::Machine});
}

void Engine::buildViewModel(const Camera& cam) {
    m_viewModel.clear();
    if (m_gunRaise <= 0.0f) return;
    const glm::mat4 gun = gunTransform(cam);
    const bool assembling = m_assembleTimer >= 0.0f;
    for (int k = 0; k < kPartTypeCount; ++k) {
        const PartType type = kAssemblyOrder[k];
        const std::optional<Item>& item = m_inventory.slot(type);
        if (!item) continue;
        glm::vec3 offset(0.0f);
        if (assembling) { // hovering apart, then sliding home one after another
            const float seat = smooth01((m_assembleTimer - (kSeatTime[k] - kSeatTravel)) / kSeatTravel);
            offset = explodedOffset(type) * (1.0f - seat);
        }
        const glm::mat4 part = glm::translate(gun, offset);
        m_viewModel.push_back({tesla::meshFor(*item), part * tesla::mountTransform(type)});
        if (type == PartType::Driver) m_viewModel.push_back({PartMesh::Frame, part}); // the grip is bolted to the box
    }
}

void Engine::drawInventory() {
    TextOverlay& hud = m_renderer->hud();
    const float s = hud.pixelScale();
    const float w = static_cast<float>(m_pixelWidth), h = static_cast<float>(m_pixelHeight);
    const float fade = 1.0f - m_fade;
    const glm::vec4 ink(1.0f, 1.0f, 0.92f, 0.9f * fade);
    const glm::vec4 dim(1.0f, 1.0f, 0.92f, 0.35f * fade);
    const glm::vec4 red(1.0f, 0.35f, 0.25f, 0.95f * fade);

    int held = 0;
    for (int t = 0; t < kPartTypeCount; ++t) held += m_inventory.has(static_cast<PartType>(t)) ? 1 : 0;
    if (held == 0) return;

    if (m_inventory.assembled()) {
        // The gun's charge gauge.
        const float charge = m_inventory.charge();
        const int bars = static_cast<int>(std::ceil(charge * 20.0f - 1e-4f));
        std::string gauge = "TESLA GUN [";
        for (int i = 0; i < 20; ++i) gauge += i < bars ? '#' : '-';
        gauge += "] " + percent(charge);
        const bool low = charge < cfg::kLowBattery;
        const bool blink = std::fmod(m_simTime, 0.8) < 0.4;
        const glm::vec4 color = charge <= 0.0f ? red * glm::vec4(1, 1, 1, 0.6f) : low ? (blink ? red : red * glm::vec4(1, 1, 1, 0.45f)) : ink;
        hud.text(gauge, w - 10.0f * s, h - 24.0f * s, TextOverlay::Align::Right, 1.0f, color, true);
        if (charge <= 0.0f) {
            hud.text("BATTERY FLAT", w - 10.0f * s, h - 38.0f * s, TextOverlay::Align::Right, 1.0f, red, true);
        } else if (low) {
            hud.text("LOW BATTERY", w - 10.0f * s, h - 38.0f * s, TextOverlay::Align::Right, 1.0f, blink ? red : dim, true);
        }
        return;
    }

    // The parts gathered so far.
    const float line = 12.0f * s;
    float y = h - 20.0f * s - line * static_cast<float>(kPartTypeCount + 1);
    char title[40];
    std::snprintf(title, sizeof(title), "TESLA GUN PARTS %d/%d", held, kPartTypeCount);
    hud.text(title, 10.0f * s, y, TextOverlay::Align::Left, 1.0f, ink, true);
    for (int t = 0; t < kPartTypeCount; ++t) {
        y += line;
        const std::optional<Item>& item = m_inventory.slot(static_cast<PartType>(t));
        std::string text = std::string(item ? "[X] " : "[ ] ") + partName(static_cast<PartType>(t));
        if (item && item->type == PartType::Battery) text += " " + percent(item->charge);
        hud.text(text, 10.0f * s, y, TextOverlay::Align::Left, 1.0f, item ? ink : dim, true);
    }
    if (m_assembleTimer >= 0.0f) {
        hud.text("ASSEMBLING...", w * 0.5f, h - 40.0f * s, TextOverlay::Align::Center, 2.0f, ink, true);
    } else if (m_inventory.complete() && m_state == GameState::Running) {
        hud.text("<R> ASSEMBLE THE TESLA GUN", w * 0.5f, h - 40.0f * s, TextOverlay::Align::Center, 2.0f,
                 glm::vec4(1.0f, 1.0f, 0.92f, (0.55f + 0.3f * std::sin(static_cast<float>(m_simTime) * 4.0f)) * fade), true);
    }
}
