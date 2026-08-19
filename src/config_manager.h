#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "config.h"

struct BotConfig {
    WORD dodgeKey = 'Z';
    int pollingRateMs = 8;
    int idlePollingRateMs = 40;
    int activePollingRateMs = 8;
    bool adaptivePolling = true;
    bool frameSkipEnabled = false;
    int targetFps = 60;
    double minimumDodgeDistance = 50.0;
    double maximumDodgeDistance = 600.0;
    bool dryRunEnabled = false;
    bool calibrationMode = false;
    bool useLegacyOffsets = false;
    bool weaponSpecificDistances = true;
    std::string logLevel = "INFO";
    std::string csvOutputPath = "data/calibration.csv";
};

class ConfigManager {
public:
    static BotConfig Load(const std::string& preferredPath = "config/config.json") {
        Config& config = Config::instance();
        config.loadFromFile(preferredPath);

        BotConfig bot;
        bot.dodgeKey = static_cast<WORD>(std::toupper(static_cast<unsigned char>(config.dodge_key[0])));
        bot.pollingRateMs = config.polling_rate_ms;
        bot.idlePollingRateMs = config.idle_polling_rate_ms;
        bot.activePollingRateMs = config.active_polling_rate_ms;
        bot.adaptivePolling = config.adaptive_polling;
        bot.frameSkipEnabled = config.frame_skip_enabled;
        bot.targetFps = config.target_fps;
        bot.minimumDodgeDistance = config.min_dodge_distance;
        bot.maximumDodgeDistance = config.max_dodge_distance;
        bot.dryRunEnabled = config.dry_run_enabled;
        bot.calibrationMode = config.calibration_mode;
        bot.useLegacyOffsets = config.use_legacy_offsets;
        bot.weaponSpecificDistances = config.weapon_specific_distances;
        bot.logLevel = config.log_level;
        bot.csvOutputPath = config.csv_output_path;
        return bot;
    }

    static bool Save(const BotConfig& config, const std::string& path = "config/config.json") {
        Config& manager = Config::instance();
        manager.dodge_key = std::string(1, static_cast<char>(config.dodgeKey));
        manager.polling_rate_ms = config.pollingRateMs;
        manager.idle_polling_rate_ms = config.idlePollingRateMs;
        manager.active_polling_rate_ms = config.activePollingRateMs;
        manager.adaptive_polling = config.adaptivePolling;
        manager.frame_skip_enabled = config.frameSkipEnabled;
        manager.target_fps = config.targetFps;
        manager.min_dodge_distance = config.minimumDodgeDistance;
        manager.max_dodge_distance = config.maximumDodgeDistance;
        manager.dry_run_enabled = config.dryRunEnabled;
        manager.calibration_mode = config.calibrationMode;
        manager.use_legacy_offsets = config.useLegacyOffsets;
        manager.weapon_specific_distances = config.weaponSpecificDistances;
        manager.log_level = config.logLevel;
        manager.csv_output_path = config.csvOutputPath;
        return manager.saveToFile(path);
    }
};
