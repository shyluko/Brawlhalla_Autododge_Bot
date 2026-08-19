#pragma once

// Hitbox-rectangle threat detection and dodge-direction selection, ported
// from autoplay_main.cpp's EnemyAttackThreatensYou/ChooseDodgeDirOutOfThreat
// so AutododgeBot's dodge decision uses the same frame-data-backed logic
// instead of a plain distance threshold.

#include "GameDataManager.h"
#include "hitbox_table.h"

#include <algorithm>
#include <cmath>

enum class DodgeDir { None, Left, Right, Up, Down };

class DefenseController {
public:
    static void RelativeToAttackerFacingRight(const Actor& attacker, double targetX, double targetY, float& dx, float& dy) {
        const bool attackerFacingRight = (attacker.facing == 0);
        dx = static_cast<float>((targetX - attacker.pos_x) * (attackerFacingRight ? 1.0 : -1.0));
        dy = static_cast<float>(targetY - attacker.pos_y);
    }

    // Returns true if the enemy's current attack hitbox reaches (youX, youY).
    // Falls back to a plain radius check when no hitbox rectangle is known
    // for the attack id.
    static bool ThreatensPosition(const Actor& enemy, double youX, double youY, double dist,
                                   double fallbackRadius, double paddingX = 35.0, double paddingY = 30.0) {
        if (!enemy.is_attacking) return false;

        const auto& hitboxes = get_hitbox_table();
        const auto it = hitboxes.find(enemy.attack_id);
        if (it == hitboxes.end()) return dist < fallbackRadius;

        const AttackBox& box = it->second;
        if (box.x1 == box.x2 && box.y1 == box.y2) return dist < fallbackRadius;

        float dx, dy;
        RelativeToAttackerFacingRight(enemy, youX, youY, dx, dy);
        return dx >= box.x1 - paddingX && dx <= box.x2 + paddingX &&
               dy >= box.y1 - paddingY && dy <= box.y2 + paddingY;
    }

    // Picks the shortest way out of the enemy's current hitbox rectangle.
    static DodgeDir ChooseDodgeDir(const Actor& enemy, double youX, double youY) {
        const auto& hitboxes = get_hitbox_table();
        const auto it = hitboxes.find(enemy.attack_id);
        if (it == hitboxes.end() || (it->second.x1 == it->second.x2 && it->second.y1 == it->second.y2)) {
            return (youX > enemy.pos_x) ? DodgeDir::Right : DodgeDir::Left;
        }

        const AttackBox& box = it->second;
        float dx, dy;
        RelativeToAttackerFacingRight(enemy, youX, youY, dx, dy);

        const float exitLeft = std::abs(dx - box.x1);
        const float exitRight = std::abs(box.x2 - dx);
        const float exitUp = std::abs(dy - box.y1);
        const float exitDown = std::abs(box.y2 - dy);

        float best = exitLeft;
        DodgeDir dir = (enemy.facing == 0) ? DodgeDir::Left : DodgeDir::Right;
        if (exitRight < best) {
            best = exitRight;
            dir = (enemy.facing == 0) ? DodgeDir::Right : DodgeDir::Left;
        }
        if (exitUp < best) {
            best = exitUp;
            dir = DodgeDir::Up;
        }
        if (exitDown < best) {
            dir = DodgeDir::Down;
        }
        return dir;
    }
};
