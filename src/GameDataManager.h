#pragma once

// Game-data lookups (moves, power IDs, dodge distances) and the raw actor
// snapshot shape read from process memory. Kept separate from AutododgeBot's
// decision logic per the runtime/data-layer split.

#include "json.hpp"
#include "offsets.h"
#include "numeric.h"
#include "hitbox_table.h"
#include "frame_hitbox_table.h"
#include "attack_table.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Hitbox {
    double radiusX, radiusY;
    double centerX, centerY;
    double x1, y1, x2, y2;
};

struct MoveData {
    std::string name;
    std::string displayName;
    int powerID;
    bool isAir;
    std::string damage;
    int stun;
    std::string impulse;
    std::string castTime;
    std::string hurtboxRef;
    std::vector<std::vector<Hitbox>> hitboxes;
    double calculatedRange;
};

struct Actor {
    uintptr_t dp = 0;
    uintptr_t entity_ptr = 0;
    uintptr_t weapon_ptr = 0;
    int weapon_type_id = 0;

    double pos_x = 0, pos_y = 0;
    bool valid_position = false;

    uint8_t light_attack = 0;
    uint8_t attack_family = 0;
    uint8_t heavy_attack = 0;
    bool is_attacking = false;
    int attack_id = 0;

    int facing = 0;
    int airborne = 0;
    int can_dodge = 0;
    int is_stunned = 0;
    int local_marker = -999;
    double damage = 0;
    int legend_id = 0;
    bool has_legend = false;

    bool is_dead = false;
    bool is_edging = false;
    int remaining_options = 0;
    int stocks = 0;
    int knocked_back = 0;

    bool valid = false;
};

struct ActiveThreat {
    int attack_id = 0;
    std::chrono::steady_clock::time_point start_timestamp{};
    bool hasReacted = false;
};

inline bool IsPlausibleFighter(const Actor& actor) {
    if (!actor.valid_position || actor.entity_ptr == 0) return false;
    if (!IsFiniteNumber(actor.pos_x) || !IsFiniteNumber(actor.pos_y)) return false;
    if (actor.pos_x < -20000.0 || actor.pos_x > 20000.0 ||
        actor.pos_y < -20000.0 || actor.pos_y > 20000.0) return false;
    if (!IsFiniteNumber(actor.damage)) return false;
    if (actor.damage < -20000.0 || actor.damage > 20000.0) return false;
    if (actor.facing != 0 && actor.facing != 1 && actor.facing != -1 &&
        actor.facing != 2 && actor.facing != -2) return false;
    if (actor.attack_id < 0 || actor.attack_id > 100000) return false;
    return true;
}

class GameDataManager {
private:
    std::map<std::string, MoveData> moves;
    std::map<int, std::string> powerIDToMoveName;
    std::map<int, std::string> powerIDToMoveKey;
    std::map<std::string, double> weaponDodgeDistances;

public:
    bool loadAllData() {
        std::cout << "Loading game data...\n";

        const std::vector<std::filesystem::path> dataRoots = {
            "data/game_data/attack_data",
            "data/game_data",
            "../data/game_data/attack_data",
            "../data/game_data"
        };

        bool loaded = false;
        for (const auto& root : dataRoots) {
            if (!std::filesystem::exists(root)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    loaded = loadMoveData(entry.path().string()) || loaded;
                }
            }
            if (loaded) {
                std::cout << "  Loaded attack data from: " << root.string() << "\n";
                break;
            }
        }

        if (!loaded) std::cout << "  Warning: Could not find attack JSON files\n";

        buildPowerIDMap();
        setupDefaultDodgeDistances();

        std::cout << "Loaded " << moves.size() << " moves\n";
        std::cout << "Loaded " << powerIDToMoveName.size() << " powerIDs\n";
        std::cout << "Hitbox table entries: " << get_hitbox_table().size() << "\n";
        std::cout << "Frame hitbox table entries: " << get_frame_hitbox_table().size() << "\n";

        return true;
    }

private:
    bool loadMoveData(const std::string& filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) return false;

            nlohmann::json data;
            file >> data;

            size_t before = moves.size();
            collectMoves(data, filename);

            return moves.size() > before;
        } catch (const std::exception& error) {
            std::cout << "  Skipping " << filename << ": " << error.what() << "\n";
            return false;
        }
    }

    void collectMoves(const nlohmann::json& node, const std::string& filename) {
        if (node.is_object()) {
            if (node.contains("powerID") && node["powerID"].is_number_integer()) {
                MoveData move;
                move.name = node.value("name", "");
                move.displayName = node.value("displayName", move.name);
                if (move.displayName.empty()) move.displayName = node.value("weapon", filename);
                move.powerID = node.value("powerID", 0);
                move.isAir = node.value("isAir", false);
                move.damage = node.value("damage", "");
                move.stun = node.value("stun", 0);
                move.impulse = node.value("impulse", "");
                move.castTime = node.value("castTime", "");
                move.hurtboxRef = node.value("hurtboxRef", "");

                move.calculatedRange = calculateMaxRange(node);

                moves[move.displayName + "#" + std::to_string(move.powerID)] = move;
                powerIDToMoveKey[move.powerID] = move.displayName + "#" + std::to_string(move.powerID);
                powerIDToMoveName[move.powerID] = move.displayName;
                std::cout << "    Parsed: " << move.displayName << " (PowerID: " << move.powerID << ", Range: " << move.calculatedRange << ")\n";
                return;
            }
            for (const auto& item : node.items()) collectMoves(item.value(), filename);
        } else if (node.is_array()) {
            for (const auto& item : node) collectMoves(item, filename);
        }
    }

    double calculateMaxRange(const nlohmann::json& node) {
        if (!node.contains("hitboxes")) return 300.0;

        double maxRange = 0.0;
        for (const auto& frame : node["hitboxes"]) {
            if (!frame.is_array()) continue;
            for (const auto& hb : frame) {
                if (!hb.is_object()) continue;
                double x1 = hb.value("x1", 0.0);
                double y1 = hb.value("y1", 0.0);
                double x2 = hb.value("x2", 0.0);
                double y2 = hb.value("y2", 0.0);

                double corner1 = std::hypot(x1, y1);
                double corner2 = std::hypot(x2, y2);
                double range = std::max(corner1, corner2);
                maxRange = std::max(maxRange, range);
            }
        }

        return maxRange > 0 ? maxRange + 50.0 : 300.0;
    }

    void buildPowerIDMap() {
        for (auto& [name, move] : moves) {
            powerIDToMoveKey[move.powerID] = name;
            powerIDToMoveName[move.powerID] = move.displayName;
        }

        std::map<int, std::string> hardcodedPowerIDs = {
            {1, "NLight"}, {2, "NLight2"}, {3, "NLight3"}, {4, "NLight4"},
            {6, "NAir"}, {7, "NAir2"}, {10, "Recovery"}, {12, "SAir"}, {13, "DAir"}, {16, "GP"},
            {20, "SLight"}, {23, "DLight"}, {35, "DLight2"},
            {30, "SwordNeutral"}, {37, "SwordSide"}, {34, "SwordDown"},
            {118, "HammerNeutral"}, {142, "HammerSide"}, {145, "HammerDown"},
            {72, "LanceNeutral"}, {90, "LanceSide"}, {97, "LanceDown"},
            {166, "PistolNeutral"}, {192, "PistolSide"}, {200, "PistolDown"},
            {223, "SpearNeutral"}, {226, "SpearSide"}, {248, "SpearDown"},
            {262, "KatarNeutral"}, {258, "KatarSide"}, {255, "KatarDown"},
            {295, "AxeNeutral"}, {291, "AxeSide"}, {294, "AxeDown"},
            {318, "BowSide"}, {327, "BowNeutral"},
            {364, "FistsNeutral"}, {370, "FistsSide"}, {373, "FistsDown"},
            {1065, "OrbNeutral"}, {1072, "OrbSide"}, {1087, "OrbDown"},
            {1109, "GreatswordNeutral"}, {1110, "GreatswordSide"}, {1111, "GreatswordDown"},
            {1117, "CannonDown"}, {1120, "CannonAir"},
            {1142, "ScytheSide"}, {1146, "ScytheDown"}, {1251, "ScytheNeutral"},
            {3500, "ChakramNeutral"}, {3614, "ChakramSide"}, {3506, "ChakramDown"},
            {4100, "BootsNeutral"}, {4105, "BootsSide"}, {4117, "BootsDown"},
        };

        for (auto& [id, name] : hardcodedPowerIDs) {
            if (!powerIDToMoveName.count(id)) {
                powerIDToMoveName[id] = name;
            }
        }
    }

    void setupDefaultDodgeDistances() {
        weaponDodgeDistances["NLight"] = 280.0;
        weaponDodgeDistances["NLight2"] = 280.0;
        weaponDodgeDistances["NLight3"] = 280.0;
        weaponDodgeDistances["NLight4"] = 280.0;
        weaponDodgeDistances["SLight"] = 300.0;
        weaponDodgeDistances["DLight"] = 250.0;
        weaponDodgeDistances["DLight2"] = 350.0;
        weaponDodgeDistances["NAir"] = 320.0;
        weaponDodgeDistances["NAir2"] = 320.0;
        weaponDodgeDistances["SAir"] = 320.0;
        weaponDodgeDistances["DAir"] = 350.0;
        weaponDodgeDistances["Recovery"] = 400.0;
        weaponDodgeDistances["GP"] = 380.0;
    }

public:
    int getMoveCount() const { return static_cast<int>(moves.size()); }
    int getPowerIdCount() const { return static_cast<int>(powerIDToMoveName.size()); }

    int getStartupFrames(int powerID) const {
        const auto& attacks = get_attack_table();
        const auto it = attacks.find(powerID);
        return it == attacks.end() ? 0 : it->second.startup_frames;
    }

    double getDodgeDistance(int powerID) {
        const auto& hitboxes = get_hitbox_table();
        auto it = hitboxes.find(powerID);
        if (it != hitboxes.end()) {
            const AttackBox& box = it->second;
            if (box.x1 != box.x2 || box.y1 != box.y2) {
                double maxExtent = 0.0;
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x1, box.y1));
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x2, box.y2));
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x1, box.y2));
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x2, box.y1));
                return maxExtent + 60.0;
            }
        }

        const auto& frameHitboxes = get_frame_hitbox_table();
        auto frameIt = frameHitboxes.find(powerID);
        if (frameIt != frameHitboxes.end()) {
            double maxExtent = 0.0;
            for (const FrameBox& box : frameIt->second.boxes) {
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x1, box.y1));
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x2, box.y2));
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x1, box.y2));
                maxExtent = std::max(maxExtent, std::hypot<double>(box.x2, box.y1));
            }
            if (maxExtent > 0) return maxExtent + 60.0;
        }

        if (powerIDToMoveKey.count(powerID)) {
            const std::string& moveKey = powerIDToMoveKey[powerID];
            if (moves.count(moveKey)) {
                return moves[moveKey].calculatedRange;
            }
        }

        const auto moveNameIt = powerIDToMoveName.find(powerID);
        if (moveNameIt != powerIDToMoveName.end()) {
            const std::string& moveName = moveNameIt->second;
            if (weaponDodgeDistances.count(moveName)) {
                return weaponDodgeDistances[moveName];
            }
        }

        for (const auto& [key, move] : moves) {
            if (move.powerID == powerID) return move.calculatedRange;
            if (key.find(std::to_string(powerID)) != std::string::npos) return move.calculatedRange;
        }

        if (powerID >= 1 && powerID <= 4) return 280.0;
        if (powerID == 20) return 300.0;
        if (powerID == 23 || powerID == 35) return 250.0;
        if (powerID >= 6 && powerID <= 16) return 320.0;
        if (powerID > 100) return 350.0;

        return 320.0;
    }

    std::string getMoveName(int powerID) {
        const auto exact = powerIDToMoveName.find(powerID);
        if (exact != powerIDToMoveName.end()) {
            return exact->second;
        }

        for (const auto& [key, move] : moves) {
            if (move.powerID == powerID) {
                return move.displayName;
            }
            const std::string needle = "#" + std::to_string(powerID);
            if (key.size() >= needle.size() && key.compare(key.size() - needle.size(), needle.size(), needle) == 0) {
                return move.displayName;
            }
        }

        return "PowerID_" + std::to_string(powerID);
    }
};
