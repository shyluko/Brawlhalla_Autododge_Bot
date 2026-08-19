#pragma once

#include "GameDataManager.h"
#include "InputController.h"
#include "attack_table.h"
#include "frame_hitbox_table.h"

#include <cmath>
#include <string>
#include <unordered_set>

struct VelocityTracker {
    double lastX = 0, lastY = 0;
    long long lastTimeMs = 0;
    bool hasLast = false;
    double velX = 0, velY = 0;

    void Update(double x, double y, long long nowMs) {
        if (hasLast) {
            const double dt = (nowMs - lastTimeMs) / 1000.0;
            if (dt > 0.001) {
                velX = (x - lastX) / dt;
                velY = (y - lastY) / dt;
            }
        }
        lastX = x;
        lastY = y;
        lastTimeMs = nowMs;
        hasLast = true;
    }

    void Reset() {
        lastX = lastY = velX = velY = 0;
        lastTimeMs = 0;
        hasLast = false;
    }
};

struct AttackChoice {
    const AttackData* move = nullptr;
    int id = 0;
    int hitFrame = 0;
    float score = 0;
    float aimDx = 0;
    float aimDy = 0;
};

class OffenseController {
public:
    static const char* WeaponPrefix(int weaponTypeId) {
        switch (weaponTypeId) {
            case 1: return "Base";
            case 3: return "Sword";
            case 4: return "Hammer";
            case 5: return "Lance";
            case 6: return "Pistol";
            case 7: return "Spear";
            case 8: return "Katar";
            case 9: return "Axe";
            case 10: return "Bow";
            case 11: return "Fists";
            case 12: return "Scythe";
            case 13: return "Cannon";
            case 14: return "Orb";
            case 16: return "Greatsword";
            case 18: return "Boots";
            case 19: return "Chakram";
            default: return nullptr;
        }
    }

    static bool MoveMatchesWeapon(const AttackData& move, int weaponTypeId) {
        const char* prefix = WeaponPrefix(weaponTypeId);
        if (!prefix || !move.name) return false;
        const std::string name = move.name;
        return name.rfind(prefix, 0) == 0;
    }

    static bool IsInternalAttackPhase(int powerId) {
        static const std::unordered_set<int> internalIds = [] {
            std::unordered_set<int> ids;
            for (const auto& [id, move] : get_attack_table()) {
                (void)id;
                if (move.combo_next != -1) ids.insert(move.combo_next);
            }
            return ids;
        }();
        return internalIds.count(powerId) != 0;
    }

    static AttackChoice PickAttack(const Actor& you, const Actor& eny,
                                   const VelocityTracker& youVel, const VelocityTracker& enyVel,
                                   int forcedMoveId = -1) {
        AttackChoice best;
        if (forcedMoveId != -1) {
            const auto& table = get_attack_table();
            auto it = table.find(forcedMoveId);
            if (it != table.end() && MoveMatchesWeapon(it->second, you.weapon_type_id)) {
                const auto sim = Simulate(forcedMoveId, you, eny, youVel, enyVel, it->second);
                if (sim.hits) {
                    best.move = &it->second;
                    best.id = forcedMoveId;
                    best.hitFrame = sim.frame;
                    best.aimDx = sim.dx;
                    best.aimDy = sim.dy;
                    return best;
                }
            }
        }

        float bestScore = 1e9f;
        for (auto& [id, move] : get_attack_table()) {
            if (!MoveMatchesWeapon(move, you.weapon_type_id)) continue;
            if (IsInternalAttackPhase(id)) continue;
            const auto sim = Simulate(id, you, eny, youVel, enyVel, move);
            if (!sim.hits) continue;
            float score = static_cast<float>(sim.frame) * 6.0f +
                          static_cast<float>(move.recovery_frames) * 0.8f +
                          sim.centerDist * 0.12f;
            if (move.combo_next != -1) score -= 8.0f;
            if (score < bestScore) {
                bestScore = score;
                best.move = &move;
                best.id = id;
                best.hitFrame = sim.frame;
                best.score = score;
                best.aimDx = sim.dx;
                best.aimDy = sim.dy;
            }
        }
        return best;
    }

    static void Execute(InputController& input, const AttackData& move, bool towardRight) {
        const std::string name = move.name ? move.name : "";
        const bool isGroundPound = name.find("GroundPound") != std::string::npos;
        const bool isHeavy = name.find("Heavy") != std::string::npos || isGroundPound;
        const bool isSide = name.find("Side") != std::string::npos;
        const bool isDown = name.find("Down") != std::string::npos || isGroundPound;

        if (isSide) input.Down(towardRight ? VK_RIGHT : VK_LEFT);
        else if (isDown) input.Down(VK_DOWN);

        input.Press(isHeavy ? 'K' : 'J');

        if (isSide) input.Release(towardRight ? VK_RIGHT : VK_LEFT);
        else if (isDown) input.Release(VK_DOWN);
    }

private:
    struct Sim {
        bool hits = false;
        int frame = 0;
        float centerDist = 0;
        float dx = 0;
        float dy = 0;
    };

    static Sim Simulate(int powerId, const Actor& you, const Actor& eny,
                        const VelocityTracker& youVel, const VelocityTracker& enyVel,
                        const AttackData& move) {
        Sim result;
        if (move.is_air != (you.airborne != 0)) return result;

        const bool youFacingRight = (you.facing == 0);
        const double facingSign = youFacingRight ? 1.0 : -1.0;
        const double baseDx = (eny.pos_x - you.pos_x) * facingSign;
        const double baseDy = eny.pos_y - you.pos_y;
        const double relVelX = (enyVel.velX - youVel.velX) * facingSign;
        const double relVelY = enyVel.velY - youVel.velY;

        const auto& frames = get_frame_hitbox_table();
        auto it = frames.find(powerId);
        if (it == frames.end()) {
            if (move.x1 == move.x2 && move.y1 == move.y2) return result;
            const float dx = static_cast<float>(baseDx);
            const float dy = static_cast<float>(baseDy);
            if (dx < move.x1 || dx > move.x2 || dy < move.y1 || dy > move.y2) return result;
            result.hits = true;
            result.frame = move.startup_frames;
            result.dx = dx;
            result.dy = dy;
            result.centerDist = std::hypot(dx - (move.x1 + move.x2) * 0.5f, dy - (move.y1 + move.y2) * 0.5f);
            return result;
        }

        int bestFrame = 9999;
        float bestCenterDist = 1e9f;
        float bestDx = 0;
        float bestDy = 0;
        for (const FrameBox& box : it->second.boxes) {
            if (box.frame > 24) continue;
            const double t = box.frame / 60.0;
            const float dx = static_cast<float>(baseDx + relVelX * t);
            const float dy = static_cast<float>(baseDy + relVelY * t);
            if (dx < box.x1 || dx > box.x2 || dy < box.y1 || dy > box.y2) continue;
            const float centerDist = std::hypot(dx - (box.x1 + box.x2) * 0.5f, dy - (box.y1 + box.y2) * 0.5f);
            if (box.frame < bestFrame || (box.frame == bestFrame && centerDist < bestCenterDist)) {
                bestFrame = box.frame;
                bestCenterDist = centerDist;
                bestDx = dx;
                bestDy = dy;
            }
        }
        if (bestFrame != 9999) {
            result.hits = true;
            result.frame = bestFrame;
            result.centerDist = bestCenterDist;
            result.dx = bestDx;
            result.dy = bestDy;
        }
        return result;
    }
};
