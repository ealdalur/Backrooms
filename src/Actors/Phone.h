#pragma once
// ---------------------------------------------------------------------------
// Phone.h
// A 1980s office desk telephone: a wedge-shaped base with a sloped keypad
// deck, the handset lying in its cradle across the back, a coiled cord, a
// "DIAL 9" number card and a red message lamp that doubles as the message
// key. Procedurally modelled like the terminals; rendered with one shared
// instanced mesh per look, one instanced mesh per key (so a single key can be
// seen going down) and one per lamp state. Some phones have a message waiting
// (the lamp blinks) until it has been listened to. The call itself (tones,
// voices, voicemail) lives in Gameplay/PhoneCall.
//
// Local space: origin on the desk top at the centre of the base, +Z is the
// front (the side the keypad faces), +Y up.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

/// How a phone is drawn: body colour, and whether the handset is in the cradle.
enum class PhoneLook : uint8_t {
    Beige = 0,       ///< Yellowed beige, handset on the hook.
    BeigeOffHook,    ///< ...lifted: empty cradle, raised hook switches, cord stretched away.
    Charcoal,        ///< Charcoal plastic, handset on the hook.
    CharcoalOffHook,
    Count
};

inline constexpr int kPhoneLookCount = static_cast<int>(PhoneLook::Count);

/// The message lamp.
enum class PhoneLamp : uint8_t {
    Dark = 0,  ///< No messages.
    Blinking,  ///< A message is waiting.
    Lit,       ///< The message is being played back.
    Ringing,   ///< The phone is ringing: it flashes with every ring.
    Count
};

inline constexpr int kPhoneLampCount = static_cast<int>(PhoneLamp::Count);

class Phone {
public:
    static constexpr int  kKeyCount = 12;
    /// Key characters in keypad order (left to right, top row first).
    static constexpr char kKeyChars[kKeyCount + 1] = "123456789*0#";
    /// The message lamp is a button too: its index for press() and hit tests...
    static constexpr int  kLampButton = kKeyCount;
    /// ...and the character standing for it (the M key).
    static constexpr char kMessageChar = 'M';

    /// @param id    Deterministic global id (hash of its cell and desk).
    /// @param model Rigid local -> world transform.
    Phone(uint64_t id, const glm::mat4& model);

    /// Shared geometry of a look (everything except the keys).
    static MeshData buildMesh(PhoneLook look);
    /// Shared geometry of one key (key space: centred on the deck, +Y out of it).
    static MeshData buildKeyMesh(int key);
    /// Shared geometry of the message lamp in one state (lamp space: centred on the hump).
    static MeshData buildLampMesh(PhoneLamp lamp);
    /// Local-space collision boxes.
    static const std::vector<AABB>& localColliders();
    /// Index of a keypad character, or -1.
    static int keyIndex(char c);

    uint64_t id() const { return m_id; }
    const glm::mat4& modelMatrix() const { return m_model; }

    /// Body colour, fixed per phone (derived from its id).
    bool charcoal() const { return (m_id >> 23) % 3u == 0u; }
    bool offHook() const { return m_offHook; }
    void setOffHook(bool off) { m_offHook = off; }
    PhoneLook look() const;

    /// Whether the phone ever had a message (fixed per phone, from its id)...
    bool hasMessage() const { return (m_id >> 29) % 10u < 3u; }
    /// ...and whether it is still waiting to be heard.
    bool messageWaiting() const { return m_messageWaiting; }
    void setMessageWaiting(bool waiting) { m_messageWaiting = waiting && hasMessage(); }
    /// Lit steadily while the message plays back.
    void setLampLit(bool lit) { m_lampLit = lit; }
    /// Ringing: the lamp flashes with the bell, so the phone can be told apart from across a room.
    void setRinging(bool ringing) { m_ringing = ringing; }
    bool ringing() const { return m_ringing; }
    PhoneLamp lamp() const {
        return m_ringing ? PhoneLamp::Ringing : m_lampLit ? PhoneLamp::Lit : m_messageWaiting ? PhoneLamp::Blinking : PhoneLamp::Dark;
    }

    /// Pushes a key (or the lamp button) down for a moment (it springs back in update()).
    void press(int key);
    void update(float dt);

    /// Key space -> world transform of a key (lowered while it is held down).
    glm::mat4 keyMatrix(int key) const;
    /// World-space corners of a key's clickable area (its share of the keypad),
    /// counter-clockwise seen from above the deck.
    void keyHitCorners(int key, glm::vec3 out[4]) const;
    /// Lamp space -> world transform of the lamp (lowered while pressed), and its clickable area.
    glm::mat4 lampMatrix() const;
    void lampHitCorners(glm::vec3 out[4]) const;

    /// World-space centre of the unit (used to rank interaction targets).
    glm::vec3 center() const;
    /// Where the camera sits while the player uses the phone, and what it looks at.
    glm::vec3 viewPoint() const;
    glm::vec3 viewTarget() const;

private:
    uint64_t  m_id;
    glm::mat4 m_model;
    bool      m_offHook = false;
    bool      m_messageWaiting;
    bool      m_lampLit = false;
    bool      m_ringing = false;
    int       m_pressedKey = -1;
    float     m_pressTimer = 0.0f;
};
