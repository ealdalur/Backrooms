// ---------------------------------------------------------------------------
// TerminalText.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/TerminalText.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace termtext {
namespace {

template <size_t N>
const char* pick(rnd::Rng& rng, const char* const (&options)[N]) {
    return options[rng.next() % N];
}

std::string format(const char* fmt, ...) {
    char buf[160];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

// ---- Kernel / facility log ------------------------------------------------------
std::string kernelLine(rnd::Rng& rng, double uptime, int level) {
    const std::string stamp = format("[%9.4f] ", uptime);
    switch (rng.rangeInt(0, 17)) {
    case 0:  return stamp + format("ATA2.00: EXCEPTION EMASK 0X0 SACT 0X%X FROZEN", rng.next() & 0xFFFF);
    case 1:  return stamp + format("ETH0: LINK UP, 100MBPS, FULL-DUPLEX, LPA 0X%04X", rng.next() & 0xFFFF);
    case 2:  return stamp + format("EXT2-FS (HDA%d): WARNING: MOUNTING UNCHECKED FS", rng.rangeInt(1, 4));
    case 3:  return stamp + format("CPU%d: TEMPERATURE ABOVE THRESHOLD, THROTTLED", rng.rangeInt(0, 3));
    case 4:  return stamp + format("USB 1-%d: NEW FULL-SPEED DEVICE NUMBER %d", rng.rangeInt(1, 4), rng.rangeInt(2, 40));
    case 5:  return stamp + format("SCSI %d:0:0:0: REJECTING I/O TO OFFLINE DEVICE", rng.rangeInt(0, 3));
    case 6:  return stamp + format("NODE-%02X: HEARTBEAT MISSED (%d)", rng.next() & 0xFF, rng.rangeInt(1, 9));
    case 7:  return stamp + format("MEM: PAGE ALLOCATION STALL %dMS, ORDER:%d", rng.rangeInt(10, 900), rng.rangeInt(0, 4));
    case 8:  return stamp + "ACPI: \\_SB.PCI0.LPC0: _STA EVALUATION FAILED";
    case 9:  return stamp + "SCHED: RT THROTTLING ACTIVATED";
    case 10: return stamp + format("BALLAST %03d: ARC RESTRIKE, FLICKER COUNT %d", rng.rangeInt(1, 999), rng.rangeInt(1, 60000));
    case 11: return stamp + format("HVAC ZONE %d: SUPPLY %.1fC RETURN %.1fC", rng.rangeInt(1, 40), rng.range(17.0f, 19.0f), rng.range(23.0f, 31.0f));
    case 12: return stamp + format("LEVEL%d.GEOM: REGENERATED CHUNK (%d, %d)", level, rng.rangeInt(-99, 99), rng.rangeInt(-99, 99));
    case 13: return stamp + format("HUMIDITY %d%%: WALLPAPER ADHESION DEGRADED", rng.rangeInt(71, 99));
    case 14: return stamp + format("CARPET SENSOR %d: MOISTURE %d%% (WET)", rng.rangeInt(1, 400), rng.rangeInt(60, 100));
    case 15: return stamp + format("DOOR %04X: OPENED FROM WRONG SIDE", rng.next() & 0xFFFF);
    case 16: return stamp + format("OCCUPANCY: %d DETECTED, %d EXPECTED", rng.rangeInt(2, 3), 0);
    default: return stamp + format("KERNEL: WATCHDOG PET (%d)", rng.rangeInt(100, 9999));
    }
}

// ---- Hardware diagnostics ---------------------------------------------------------
std::string diagLine(rnd::Rng& rng, int level) {
    switch (rng.rangeInt(0, 8)) {
    case 0: {
        const uint32_t a = (rng.next() & 0x0FFF) << 12;
        return format("MEMTEST %08X-%08X .......... %s", a, a + 0xFFF, rng.chance(0.93f) ? "OK" : "FAIL");
    }
    case 1: {
        const char* const rails[] = {"+5V ", "+12V", "-12V", "+3.3V"};
        const float nominal[] = {5.0f, 12.0f, -12.0f, 3.3f};
        const int r = rng.rangeInt(0, 3);
        const float v = nominal[r] * rng.range(0.94f, 1.04f);
        return format("VOLTAGE RAIL %s ....... %6.2fV %s", rails[r], v, std::abs(v / nominal[r] - 1.0f) > 0.04f ? "UNSTABLE" : "NOMINAL");
    }
    case 2:  return format("SECTOR %06X: CRC %04X %s", rng.next() & 0xFFFFFF, rng.next() & 0xFFFF,
                           rng.chance(0.8f) ? "OK" : format("MISMATCH (RETRY %d/5)", rng.rangeInt(1, 5)).c_str());
    case 3:  return format("FAN %d: %4d RPM", rng.rangeInt(1, 4), rng.rangeInt(0, 3200));
    case 4:  return format("CHECKSUM LEVEL_%d MANIFEST ........ %s", level, rng.chance(0.7f) ? "OK" : "DOES NOT MATCH");
    case 5: {
        const int pct = rng.rangeInt(0, 100);
        std::string bar(20, '.');
        std::fill(bar.begin(), bar.begin() + pct / 5, '#');
        const char* const tasks[] = {"REINDEXING GEOMETRY", "DEFRAGMENTING", "VERIFYING EXITS", "COUNTING ROOMS", "PURGING CACHE"};
        return format("[%s] %3d%% %s", bar.c_str(), pct, pick(rng, tasks));
    }
    case 6:  return format("SMART: REALLOCATED SECTORS %d, PENDING %d", rng.rangeInt(0, 900), rng.rangeInt(0, 40));
    case 7:  return format("EXIT PATH SEARCH: %d ROOMS SCANNED, 0 FOUND", rng.rangeInt(1000, 999999));
    default: return format("CLOCK DRIFT %+.3fS - RESYNC FAILED", rng.range(-400.0f, 400.0f));
    }
}

// ---- Network chatter ----------------------------------------------------------------
std::string netLine(rnd::Rng& rng, int level) {
    switch (rng.rangeInt(0, 5)) {
    case 0:  return rng.chance(0.75f) ? format("PING 10.%d.%d.%d: SEQ=%d TTL=64 TIME=%.1f MS", level & 255, rng.rangeInt(0, 255),
                                               rng.rangeInt(1, 254), rng.rangeInt(0, 999), rng.range(0.3f, 900.0f))
                                      : std::string("REQUEST TIMED OUT.");
    case 1:  return format("SYNC NODE %02X:%02X -> LEVEL %d ... %s", rng.next() & 0xFF, rng.next() & 0xFF, level + rng.rangeInt(-3, 3),
                           rng.chance(0.85f) ? "ACK" : "NO ROUTE");
    case 2:  return format("RX %d PACKETS, %d ERRORS, %d DROPPED", rng.rangeInt(1000, 90000), rng.rangeInt(0, 99), rng.rangeInt(0, 999));
    case 3:  return format("ARP WHO-HAS 10.0.%d.%d TELL 10.0.0.1", rng.rangeInt(0, 255), rng.rangeInt(1, 254));
    case 4:  return format("TCP %d.%d.%d.%d:%d RST (CONNECTION RESET BY PEER)", rng.rangeInt(10, 192), rng.rangeInt(0, 255),
                           rng.rangeInt(0, 255), rng.rangeInt(1, 254), rng.rangeInt(1024, 65535));
    default: return format("DHCP: LEASE EXPIRED %d DAYS AGO", rng.rangeInt(900, 12000));
    }
}

// ---- Hex dumps (occasionally the bytes say something) ------------------------------
std::string hexLine(rnd::Rng& rng) {
    static const char* const kHidden[] = {"LET ME OUT PLEAS", "HELP ME HELP ME ", "NO EXIT NO EXIT ", "I AM STILL HERE ",
                                          "WHERE IS THE DOO", "IT IS DARK HERE "};
    uint8_t bytes[16];
    if (rng.chance(0.12f)) {
        std::memcpy(bytes, pick(rng, kHidden), 16);
    } else {
        for (uint8_t& b : bytes) b = static_cast<uint8_t>(rng.next() & 0xFF);
    }
    std::string out = format("%04X  ", (rng.next() & 0xFFF) << 4);
    for (int i = 0; i < 16; ++i) {
        out += format("%02X", bytes[i]);
        if (i % 4 == 3 && i < 15) out += ' ';
    }
    out += "  |";
    for (uint8_t b : bytes) out += (b >= 32 && b < 127) ? static_cast<char>(b) : '.';
    return out + "|";
}

} // namespace

bool parseMode(const std::string& name, StreamMode& out) {
    if (name == "all") out = StreamMode::All;
    else if (name == "kernel" || name == "log" || name == "logs") out = StreamMode::Kernel;
    else if (name == "diag") out = StreamMode::Diag;
    else if (name == "net") out = StreamMode::Net;
    else if (name == "hex") out = StreamMode::Hex;
    else return false;
    return true;
}

const char* modeName(StreamMode mode) {
    switch (mode) {
    case StreamMode::Kernel: return "KERNEL";
    case StreamMode::Diag:   return "DIAG";
    case StreamMode::Net:    return "NET";
    case StreamMode::Hex:    return "HEX";
    case StreamMode::All:
    default:                 return "ALL";
    }
}

std::string systemLine(rnd::Rng& rng, StreamMode mode, double uptime, int level) {
    if (mode == StreamMode::All) {
        const float r = rng.nextFloat();
        mode = r < 0.45f ? StreamMode::Kernel : r < 0.72f ? StreamMode::Diag : r < 0.9f ? StreamMode::Net : StreamMode::Hex;
    }
    std::string line;
    switch (mode) {
    case StreamMode::Kernel: line = kernelLine(rng, uptime, level); break;
    case StreamMode::Diag:   line = diagLine(rng, level); break;
    case StreamMode::Net:    line = netLine(rng, level); break;
    case StreamMode::Hex:
    default:                 line = hexLine(rng); break;
    }
    return line.substr(0, 64);
}

std::string anomalyMessage(rnd::Rng& rng, float desperation) {
    static const char* const kCalm[] = {
        "hello?", "are you there?", "is someone there?", "can you read this?", "hello? hello?",
        "are we stuck here?", "i can see your light", "who are you?", "is anyone else there?",
        "i hear the lights humming. do you?", "how long have i been in here", "please don't log off",
    };
    static const char* const kDesperate[] = {
        "HELP!!!", "does this ever end???", "HELP ME HELP ME HELP ME", "please", "LET ME OUT",
        "there's no exit. i checked.", "it's so cold in the wires", "i don't want to be data",
        "they said it would only take a moment", "i can't feel my hands anymore", "don't go",
        "it keeps changing the rooms", "you shouldn't have come in here", "i was just like you",
        "is it still 1994?", "i counted the rooms. i stopped at forty thousand.",
    };
    std::string msg = (rng.nextFloat() < 0.35f + 0.6f * desperation) ? pick(rng, kDesperate) : pick(rng, kCalm);
    if (rng.chance(0.15f + 0.25f * desperation)) {
        // Stutter: the first word, over and over.
        const std::string first = msg.substr(0, msg.find(' '));
        std::string s;
        for (int i = rng.rangeInt(2, 4); i > 0; --i) s += first + " ";
        msg = s + msg;
    }
    return msg.substr(0, 64);
}

std::string corrupt(rnd::Rng& rng, const std::string& text, float amount) {
    static const char kNoise[] = "#%&@$*!?/\\|<>~^=+";
    std::string out = text;
    for (char& c : out) {
        if (rng.chance(amount)) c = kNoise[rng.next() % (sizeof(kNoise) - 1)];
    }
    return out;
}

std::vector<std::string> bootSequence(rnd::Rng& rng, int level, uint32_t node) {
    return {
        "ASYNC OFFICE SYSTEMS BIOS V1.03",
        "(C) 1987-1994 ASYNC CORP.",
        "",
        "MEMORY TEST: 640K OK",
        format("EXTENDED MEMORY: %dK OK", 1024 * rng.rangeInt(2, 15)),
        format("DETECTING IDE DRIVES... HDA: %dMB", rng.rangeInt(20, 540)),
        "LOADING KERNEL .................. OK",
        format("MOUNTING /LEVEL%d ................ OK", level),
        "STARTING FACILITY MONITOR",
        "",
        format("FACILITY MONITOR 2.3 - NODE %04X - LEVEL %d", node & 0xFFFF, level),
        "WELCOME, OPERATOR. TYPE HELP FOR COMMANDS.",
        "",
    };
}

bool isConversational(const std::string& s) {
    static const char* const kWords[] = {"hello", "hi", "hey", "who", "where", "help", "are you", "you there", "anyone",
                                         "trapped", "stuck", "out", "name", "sorry", "yes", "no", "ok", "what", "why",
                                         "how", "i'm", "im ", "me", "?"};
    for (const char* w : kWords) {
        if (s.find(w) != std::string::npos) return true;
    }
    return false;
}

std::string reply(rnd::Rng& rng, const std::string& s) {
    auto has = [&s](const char* w) { return s.find(w) != std::string::npos; };
    if (has("hello") || has("hi") || has("hey") || has("anyone") || has("there")) {
        const char* const r[] = {"hello? HELLO? you can see this?", "hi. please don't leave.", "yes. yes. i'm here.",
                                 "you answered. nobody ever answers."};
        return pick(rng, r);
    }
    if (has("who") || has("name")) {
        const char* const r[] = {"i don't remember my name. it was on a badge.", "an employee. i think.",
                                 "i used to be someone who sat at this desk."};
        return pick(rng, r);
    }
    if (has("where")) {
        const char* const r[] = {"same place as you. just deeper.", "inside. behind the glass.", "under the carpet. in the wires."};
        return pick(rng, r);
    }
    if (has("out") || has("exit") || has("escape") || has("leave") || has("how")) {
        const char* const r[] = {"if you find the way out, come back for me", "there's no way out. only up. or down.",
                                 "the stairs never end. i tried."};
        return pick(rng, r);
    }
    if (has("help")) {
        const char* const r[] = {"i can't help you. can you help me?", "HELP!!!", "nobody can help. not here."};
        return pick(rng, r);
    }
    if (has("sorry")) return "everyone is.";
    if (has("yes") || has("no") || has("ok")) {
        const char* const r[] = {"...", "you're lying.", "ok. ok. ok."};
        return pick(rng, r);
    }
    const char* const r[] = {"...", "i can't read you. the words come in broken.", "what?", ""};
    return pick(rng, r);
}

} // namespace termtext
