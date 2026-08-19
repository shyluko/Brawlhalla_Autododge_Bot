#pragma once

#include <cstdint>

// Confirmed / candidate / unconfirmed values from docs/OFFSETS.md.
// Runtime must not silently promote candidates. F3 only switches the two
// documented post-patch fields (airborne, CanDodge).

namespace Offsets {
    constexpr uintptr_t POS_X = 0x140;
    constexpr uintptr_t POS_Y = 0x138;
    constexpr uintptr_t LIGHT_ATTACK = 0x048;
    constexpr uintptr_t ATTACK_FAMILY = 0x050;
    constexpr uintptr_t HEAVY_ATTACK = 0x054;
    constexpr uintptr_t ATTACK_ID = 0x06C;
    constexpr uintptr_t ENTITY_PTR = 0x0C0;
    constexpr uintptr_t WEAPON_PTR = 0x0C8;
    constexpr uintptr_t WEAPON_TYPE_PTR = 0x48;
    constexpr uintptr_t WEAPON_TYPE_ID = 0xB4;

    constexpr uintptr_t FACING = 0x110;
    constexpr uintptr_t DAMAGE = 0x5D8;
    constexpr uintptr_t LEGEND_PTR = 0x3E8;
    constexpr uintptr_t LEGEND_ID = 0x48;
    constexpr uintptr_t LOCAL_MARKER = 0x250;

    constexpr uintptr_t AIRBORNE_CURRENT = 0x12C;
    constexpr uintptr_t AIRBORNE_LEGACY = 0x124;
    constexpr uintptr_t CAN_DODGE_CURRENT = 0x1A4;
    constexpr uintptr_t CAN_DODGE_LEGACY = 0x190;

    constexpr uintptr_t IS_STUNNED = 0x144;
    constexpr uintptr_t IS_DEAD = 0x25C;
    constexpr uintptr_t IS_EDGING = 0x154;
    constexpr uintptr_t REMAINING_OPTIONS = 0x220;
    constexpr uintptr_t STOCKS = 0x2A4;
    constexpr uintptr_t KNOCKED_BACK = 0x084;

    constexpr const char* CAPTURE_PATTERN = "\xF2\x0F\x10\x8B\x40\x01\x00\x00";
    constexpr const char* CAPTURE_MASK = "xxxxxxxx";
}

enum class OffsetConfidence {
    Confirmed,
    Conditional,
    Candidate,
    Unconfirmed
};

inline const char* OffsetConfidenceName(OffsetConfidence level) {
    switch (level) {
        case OffsetConfidence::Confirmed: return "confirmed";
        case OffsetConfidence::Conditional: return "conditional";
        case OffsetConfidence::Candidate: return "candidate";
        default: return "unconfirmed";
    }
}
