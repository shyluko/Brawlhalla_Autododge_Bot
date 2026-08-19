#pragma once
#include "attack_table.h"
#include <random>

// High-level phase label shown on the dashboard. This mirrors the branches
// already present in the decision loop; it does not change control flow.
enum class BotState {
    Disabled, NoTracking, LedgeHang, Recover, Dodge,
    ComboExtend, EdgeGuard, EdgeHold, Attack, Approach, Waiting
};

inline const char* BotStateName(BotState s) {
    switch (s) {
        case BotState::Disabled:    return "DISABLED";
        case BotState::NoTracking:  return "NO TRACKING";
        case BotState::LedgeHang:   return "LEDGE";
        case BotState::Recover:     return "RECOVER";
        case BotState::Dodge:       return "DODGE";
        case BotState::ComboExtend: return "COMBO";
        case BotState::EdgeGuard:   return "EDGE GUARD";
        case BotState::EdgeHold:    return "EDGE HOLD";
        case BotState::Attack:      return "ATTACK";
        case BotState::Approach:    return "APPROACH";
        default:                    return "WAITING";
    }
}

// Adds +/-varianceMs of jitter around a base delay so automated input timing
// is not perfectly uniform (raw SendInput cadence is a detection signal).
inline int JitteredMs(int baseMs, int varianceMs) {
    static std::mt19937 rng{ std::random_device{}() };
    if (varianceMs <= 0) return baseMs;
    std::uniform_int_distribution<int> dist(-varianceMs, varianceMs);
    return std::max(1, baseMs + dist(rng));
}

// Deterministic follow-up selection built on the attack table's combo_next
// chain. PickAttack() already re-searches every move each frame, which works
// but treats every hit as a fresh decision; ComboEngine instead remembers the
// move that just landed and, while its chain window is open, forces the
// attack table's designated follow-up so real combo strings (e.g. SwordNeutral
// -> SwordNeutralCombo -> SwordNeutralHit) play out deterministically instead
// of being re-derived by the generic scorer every frame.
class ComboEngine {
public:
    void OnHitConfirmed(int landedMoveId, long long nowMs) {
        const auto& table = get_attack_table();
        auto it = table.find(landedMoveId);
        nextMoveId = (it != table.end()) ? it->second.combo_next : -1;
        windowExpiresMs = nextMoveId != -1 ? nowMs + kComboWindowMs : 0;
    }

    void Clear() { nextMoveId = -1; windowExpiresMs = 0; }

    bool IsActive(long long nowMs) const {
        return nextMoveId != -1 && nowMs < windowExpiresMs;
    }

    // Returns the forced combo_next move id, or -1 if no combo is pending.
    int PendingMoveId(long long nowMs) const {
        return IsActive(nowMs) ? nextMoveId : -1;
    }

private:
    int nextMoveId = -1;
    long long windowExpiresMs = 0;
    static constexpr long long kComboWindowMs = 500;
};
