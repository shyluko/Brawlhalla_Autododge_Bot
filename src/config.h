#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>

#include "json.hpp"
#include "numeric.h"

struct HotkeyConfig {
    std::string toggle_bot = "F1";
    std::string dry_run = "F2";
    std::string offset_toggle = "F3";
    std::string calibration = "F4";
    std::string safe_mode = "F5";
    std::string reconnect = "F6";
    std::string test_dodge = "F7";
    std::string export_diagnostics = "F9";
    std::string shutdown = "F10";
    std::string dodge = "Z";
};

struct SavedProfile {
    std::string name = "default";
    std::string character = "any";
    std::string weapon = "any";

    std::string dodge_key = "Z";
    int polling_rate_ms = 8;
    int idle_polling_rate_ms = 40;
    int active_polling_rate_ms = 8;
    bool adaptive_polling = true;
    bool frame_skip_enabled = false;
    int target_fps = 60;
    double min_dodge_distance = 50.0;
    double max_dodge_distance = 600.0;
    bool dry_run_enabled = false;
    bool calibration_mode = false;
    bool weapon_specific_distances = true;
    bool use_legacy_offsets = false;
    bool sound_notifications = true;
    bool minimize_to_tray = true;
    bool auto_recover_hook = true;
    bool safe_mode_enabled = true;
    bool warn_on_game_update = true;
    bool process_watchdog_enabled = true;
    bool visual_hitboxes_enabled = false;
    bool combo_recognition_enabled = true;
    bool prediction_tuning_enabled = true;
    float prediction_aggression = 0.5f;
    double dodge_padding_x = 35.0;
    double dodge_padding_y = 30.0;
    float react_chance = 1.0f;
    bool offense_enabled = false;
    float ui_scale = 1.0f;
    bool analysis_mode = false;
    bool weapon_swap_detection = true;
    bool discord_rpc_enabled = false;
    std::string discord_webhook_url = "";
    std::string github_repo = "";
    std::string export_import_path = "config/exports/shared_profile.json";
    std::string log_level = "INFO";
    std::string csv_output_path = "data/calibration.csv";
    bool sound_event_enabled = true;
    bool sound_event_disabled = true;
    bool sound_event_dodge = true;
    bool sound_event_hook_lost = true;
    bool sound_event_hook_reconnected = true;

    HotkeyConfig hotkeys;

    int window_x = 100;
    int window_y = 100;
    int window_w = 1120;
    int window_h = 720;
};

class Config {
public:
    static Config& instance() {
        static Config cfg;
        return cfg;
    }

    bool loadFromFile(const std::filesystem::path& overridePath = {}) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::filesystem::path path = overridePath;
        if (path.empty()) {
            const std::filesystem::path exeDir = getExecutableDirectory();
            const std::vector<std::filesystem::path> candidates = {
                exeDir / "config.json",
                exeDir / "config" / "config.json",
                std::filesystem::current_path() / "config.json",
                std::filesystem::current_path() / "config" / "config.json"
            };

            for (const auto& candidate : candidates) {
                if (std::filesystem::exists(candidate)) {
                    path = candidate;
                    break;
                }
            }
        }

        resetDefaults();
        if (path.empty() || !std::filesystem::exists(path)) {
            config_path_ = path;
            return false;
        }

        config_path_ = path;

        try {
            std::ifstream input(path);
            if (!input.is_open()) {
                return false;
            }

            nlohmann::json root;
            input >> root;

            if (root.contains("active_profile") && root["active_profile"].is_string()) {
                active_profile = root["active_profile"].get<std::string>();
            }

            const nlohmann::json mainSettings = root.contains("settings") ? root["settings"] : root;
            applyProfileJson(mainSettings, current_profile_);
            if (!mainSettings.contains("name") && current_profile_.name.empty()) {
                current_profile_.name = active_profile.empty() ? "default" : active_profile;
            }

            if (root.contains("profiles") && root["profiles"].is_object()) {
                for (auto it = root["profiles"].begin(); it != root["profiles"].end(); ++it) {
                    const std::string profileName = it.key();
                    SavedProfile profile = current_profile_;
                    profile.name = profileName;
                    applyProfileJson(it.value(), profile);
                    profiles_[profileName] = profile;
                }
            }

            if (!profiles_.count(active_profile) && !active_profile.empty()) {
                profiles_[active_profile] = current_profile_;
            }

            if (!profiles_.empty()) {
                const std::string selected = !active_profile.empty() && profiles_.count(active_profile) ? active_profile : std::string("default");
                if (profiles_.count(selected)) {
                    current_profile_ = profiles_[selected];
                    if (current_profile_.name.empty()) current_profile_.name = selected;
                }
            }

            applyProfileToRuntime(current_profile_);
        } catch (const std::exception&) {
            resetDefaults();
            return false;
        }

        return true;
    }

    bool saveToFile(const std::filesystem::path& overridePath = {}) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::filesystem::path path = overridePath.empty() ? config_path_ : overridePath;
        if (path.empty()) {
            const std::filesystem::path exeDir = getExecutableDirectory();
            path = exeDir / "config" / "config.json";
        }

        try {
            std::filesystem::create_directories(path.parent_path());

            nlohmann::json root;
            root["active_profile"] = active_profile.empty() ? current_profile_.name : active_profile;
            root["settings"] = profileToJson(current_profile_);
            root["profiles"] = nlohmann::json::object();

            for (const auto& [name, profile] : profiles_) {
                root["profiles"][name] = profileToJson(profile);
            }

            if (root["profiles"].empty()) {
                root["profiles"][current_profile_.name.empty() ? "default" : current_profile_.name] = profileToJson(current_profile_);
            }

            std::ofstream output(path);
            if (!output.is_open()) {
                return false;
            }

            output << root.dump(4) << '\n';
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void syncCurrentProfileFromRuntime() {
        current_profile_.name = active_profile.empty() ? current_profile_.name : active_profile;
        current_profile_.dodge_key = dodge_key;
        current_profile_.polling_rate_ms = polling_rate_ms;
        current_profile_.idle_polling_rate_ms = idle_polling_rate_ms;
        current_profile_.active_polling_rate_ms = active_polling_rate_ms;
        current_profile_.adaptive_polling = adaptive_polling;
        current_profile_.frame_skip_enabled = frame_skip_enabled;
        current_profile_.target_fps = target_fps;
        current_profile_.min_dodge_distance = min_dodge_distance;
        current_profile_.max_dodge_distance = max_dodge_distance;
        current_profile_.dry_run_enabled = dry_run_enabled;
        current_profile_.calibration_mode = calibration_mode;
        current_profile_.weapon_specific_distances = weapon_specific_distances;
        current_profile_.use_legacy_offsets = use_legacy_offsets;
        current_profile_.sound_notifications = sound_notifications;
        current_profile_.sound_event_enabled = sound_event_enabled;
        current_profile_.sound_event_disabled = sound_event_disabled;
        current_profile_.sound_event_dodge = sound_event_dodge;
        current_profile_.sound_event_hook_lost = sound_event_hook_lost;
        current_profile_.sound_event_hook_reconnected = sound_event_hook_reconnected;
        current_profile_.minimize_to_tray = minimize_to_tray;
        current_profile_.auto_recover_hook = auto_recover_hook;
        current_profile_.safe_mode_enabled = safe_mode_enabled;
        current_profile_.warn_on_game_update = warn_on_game_update;
        current_profile_.process_watchdog_enabled = process_watchdog_enabled;
        current_profile_.visual_hitboxes_enabled = visual_hitboxes_enabled;
        current_profile_.combo_recognition_enabled = combo_recognition_enabled;
        current_profile_.prediction_tuning_enabled = prediction_tuning_enabled;
        current_profile_.prediction_aggression = prediction_aggression;
        current_profile_.dodge_padding_x = dodge_padding_x;
        current_profile_.dodge_padding_y = dodge_padding_y;
        current_profile_.react_chance = react_chance;
        current_profile_.offense_enabled = offense_enabled;
        current_profile_.ui_scale = ui_scale;
        current_profile_.analysis_mode = analysis_mode;
        current_profile_.weapon_swap_detection = weapon_swap_detection;
        current_profile_.discord_rpc_enabled = discord_rpc_enabled;
        current_profile_.discord_webhook_url = discord_webhook_url;
        current_profile_.github_repo = github_repo;
        current_profile_.export_import_path = export_import_path;
        current_profile_.log_level = log_level;
        current_profile_.csv_output_path = csv_output_path;
        current_profile_.hotkeys = hotkeys;
        current_profile_.window_x = window_x;
        current_profile_.window_y = window_y;
        current_profile_.window_w = window_w;
        current_profile_.window_h = window_h;
        if (!current_profile_.name.empty()) {
            profiles_[current_profile_.name] = current_profile_;
        }
    }

    bool saveProfile(const std::string& name, const SavedProfile& profile = SavedProfile{}) {
        SavedProfile target = profile;
        if (name.empty()) {
            return false;
        }
        if (target.name.empty()) {
            target.name = name;
        }
        target.name = name;
        profiles_[name] = target;
        current_profile_ = target;
        active_profile = name;
        return saveToFile();
    }

    bool saveCurrentProfile(const std::string& name = {}) {
        syncCurrentProfileFromRuntime();
        const std::string profileName = name.empty() ? active_profile.empty() ? current_profile_.name : active_profile : name;
        if (profileName.empty()) {
            return false;
        }
        current_profile_.name = profileName;
        active_profile = profileName;
        profiles_[profileName] = current_profile_;
        return saveToFile();
    }

    bool exportProfileToFile(const std::filesystem::path& path) const {
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path);
            if (!output.is_open()) {
                return false;
            }
            const nlohmann::json root = profileToJson(current_profile_);
            output << root.dump(4) << '\n';
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool importProfileFromFile(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return false;
        }

        try {
            std::ifstream input(path);
            if (!input.is_open()) {
                return false;
            }
            nlohmann::json root;
            input >> root;
            SavedProfile imported = current_profile_;
            applyProfileJson(root.contains("settings") ? root["settings"] : root, imported);
            current_profile_ = imported;
            active_profile = imported.name.empty() ? active_profile.empty() ? "default" : active_profile : imported.name;
            applyProfileToRuntime(current_profile_);
            profiles_[active_profile] = current_profile_;
            return saveToFile();
        } catch (const std::exception&) {
            return false;
        }
    }

    bool loadProfile(const std::string& name) {
        if (name.empty() || !profiles_.count(name)) {
            return false;
        }
        current_profile_ = profiles_[name];
        if (current_profile_.name.empty()) current_profile_.name = name;
        active_profile = name;
        applyProfileToRuntime(current_profile_);
        return true;
    }

    std::vector<std::string> listProfiles() const {
        std::vector<std::string> profiles;
        for (const auto& [name, _] : profiles_) {
            profiles.push_back(name);
        }
        return profiles;
    }

    const std::string& activeProfileName() const { return active_profile; }

    bool deleteProfile(const std::string& name) {
        if (name.empty() || name == "default" || !profiles_.count(name)) {
            return false;
        }
        profiles_.erase(name);
        if (active_profile == name) {
            active_profile = "default";
            if (profiles_.count("default")) {
                current_profile_ = profiles_["default"];
                applyProfileToRuntime(current_profile_);
            }
        }
        return saveToFile();
    }

    void resetDefaults() {
        active_profile = "default";
        current_profile_ = SavedProfile{};
        current_profile_.name = "default";
        profiles_.clear();

        dodge_key = "Z";
        polling_rate_ms = 8;
        idle_polling_rate_ms = 40;
        active_polling_rate_ms = 8;
        adaptive_polling = true;
        frame_skip_enabled = false;
        target_fps = 60;
        min_dodge_distance = 50.0;
        max_dodge_distance = 600.0;
        dry_run_enabled = false;
        calibration_mode = false;
        weapon_specific_distances = true;
        use_legacy_offsets = false;
        sound_notifications = true;
        sound_event_enabled = true;
        sound_event_disabled = true;
        sound_event_dodge = true;
        sound_event_hook_lost = true;
        sound_event_hook_reconnected = true;
        minimize_to_tray = true;
        auto_recover_hook = true;
        safe_mode_enabled = true;
        warn_on_game_update = true;
        process_watchdog_enabled = true;
        visual_hitboxes_enabled = false;
        combo_recognition_enabled = true;
        prediction_tuning_enabled = true;
        prediction_aggression = 0.5f;
        dodge_padding_x = 35.0;
        dodge_padding_y = 30.0;
        react_chance = 1.0f;
        offense_enabled = false;
        ui_scale = 1.0f;
        analysis_mode = false;
        weapon_swap_detection = true;
        discord_rpc_enabled = false;
        discord_webhook_url = "";
        github_repo = "";
        export_import_path = "config/exports/shared_profile.json";
        log_level = "INFO";
        csv_output_path = "data/calibration.csv";
        hotkeys = HotkeyConfig{};
        window_x = 100;
        window_y = 100;
        window_w = 1120;
        window_h = 720;

        profiles_["default"] = current_profile_;
        applyProfileToRuntime(current_profile_);
    }

    void applyProfileToRuntime(const SavedProfile& profile) {
        dodge_key = profile.dodge_key.empty() ? "Z" : profile.dodge_key;
        polling_rate_ms = profile.polling_rate_ms;
        idle_polling_rate_ms = profile.idle_polling_rate_ms;
        active_polling_rate_ms = profile.active_polling_rate_ms;
        adaptive_polling = profile.adaptive_polling;
        frame_skip_enabled = profile.frame_skip_enabled;
        target_fps = profile.target_fps;
        min_dodge_distance = profile.min_dodge_distance;
        max_dodge_distance = std::max(profile.min_dodge_distance, profile.max_dodge_distance);
        dry_run_enabled = profile.dry_run_enabled;
        calibration_mode = profile.calibration_mode;
        weapon_specific_distances = profile.weapon_specific_distances;
        use_legacy_offsets = profile.use_legacy_offsets;
        sound_notifications = profile.sound_notifications;
        sound_event_enabled = profile.sound_event_enabled;
        sound_event_disabled = profile.sound_event_disabled;
        sound_event_dodge = profile.sound_event_dodge;
        sound_event_hook_lost = profile.sound_event_hook_lost;
        sound_event_hook_reconnected = profile.sound_event_hook_reconnected;
        minimize_to_tray = profile.minimize_to_tray;
        auto_recover_hook = profile.auto_recover_hook;
        safe_mode_enabled = profile.safe_mode_enabled;
        warn_on_game_update = profile.warn_on_game_update;
        process_watchdog_enabled = profile.process_watchdog_enabled;
        visual_hitboxes_enabled = profile.visual_hitboxes_enabled;
        combo_recognition_enabled = profile.combo_recognition_enabled;
        prediction_tuning_enabled = profile.prediction_tuning_enabled;
        prediction_aggression = profile.prediction_aggression;
        dodge_padding_x = profile.dodge_padding_x;
        dodge_padding_y = profile.dodge_padding_y;
        react_chance = profile.react_chance;
        offense_enabled = profile.offense_enabled;
        ui_scale = profile.ui_scale;
        analysis_mode = profile.analysis_mode;
        weapon_swap_detection = profile.weapon_swap_detection;
        discord_rpc_enabled = profile.discord_rpc_enabled;
        discord_webhook_url = profile.discord_webhook_url;
        github_repo = profile.github_repo;
        export_import_path = profile.export_import_path;
        log_level = profile.log_level;
        csv_output_path = profile.csv_output_path;
        hotkeys = profile.hotkeys;
        window_x = profile.window_x;
        window_y = profile.window_y;
        window_w = profile.window_w;
        window_h = profile.window_h;
        if (!profile.name.empty()) {
            active_profile = profile.name;
            current_profile_ = profile;
        }
        sanitizeRuntime();
    }

    void sanitizeRuntime() {
        polling_rate_ms = SanitizeInt(polling_rate_ms, 8, 1, 1000);
        idle_polling_rate_ms = SanitizeInt(idle_polling_rate_ms, 40, 10, 500);
        active_polling_rate_ms = SanitizeInt(active_polling_rate_ms, 8, 1, 100);
        target_fps = SanitizeInt(target_fps, 60, 30, 240);
        min_dodge_distance = SanitizeDouble(min_dodge_distance, 50.0, 0.0, 2000.0);
        max_dodge_distance = SanitizeDouble(max_dodge_distance, 600.0, min_dodge_distance, 4000.0);
        prediction_aggression = SanitizeFloat(prediction_aggression, 0.5f, 0.0f, 1.0f);
        dodge_padding_x = SanitizeDouble(dodge_padding_x, 35.0, 0.0, 200.0);
        dodge_padding_y = SanitizeDouble(dodge_padding_y, 30.0, 0.0, 200.0);
        react_chance = SanitizeFloat(react_chance, 1.0f, 0.0f, 1.0f);
        ui_scale = SanitizeFloat(ui_scale, 1.0f, 0.75f, 1.5f);
        log_level = SanitizeLogLevel(log_level);
        window_w = SanitizeInt(window_w, 1120, 640, 3840);
        window_h = SanitizeInt(window_h, 720, 480, 2160);
    }

    void applyProfileJson(const nlohmann::json& root, SavedProfile& profile) {
        if (!root.is_object()) {
            return;
        }

        if (root.contains("name") && root["name"].is_string()) {
            profile.name = root["name"].get<std::string>();
        }
        if (root.contains("character") && root["character"].is_string()) {
            profile.character = root["character"].get<std::string>();
        }
        if (root.contains("weapon") && root["weapon"].is_string()) {
            profile.weapon = root["weapon"].get<std::string>();
        }
        if (root.contains("dodge_key") && root["dodge_key"].is_string()) {
            profile.dodge_key = root["dodge_key"].get<std::string>();
        }
        if (root.contains("polling_rate_ms") && root["polling_rate_ms"].is_number_integer()) {
            profile.polling_rate_ms = std::max(1, std::min(1000, root["polling_rate_ms"].get<int>()));
        }
        if (root.contains("idle_polling_rate_ms") && root["idle_polling_rate_ms"].is_number_integer()) {
            profile.idle_polling_rate_ms = std::max(10, std::min(500, root["idle_polling_rate_ms"].get<int>()));
        }
        if (root.contains("active_polling_rate_ms") && root["active_polling_rate_ms"].is_number_integer()) {
            profile.active_polling_rate_ms = std::max(1, std::min(100, root["active_polling_rate_ms"].get<int>()));
        }
        if (root.contains("adaptive_polling") && root["adaptive_polling"].is_boolean()) {
            profile.adaptive_polling = root["adaptive_polling"].get<bool>();
        }
        if (root.contains("frame_skip_enabled") && root["frame_skip_enabled"].is_boolean()) {
            profile.frame_skip_enabled = root["frame_skip_enabled"].get<bool>();
        }
        if (root.contains("target_fps") && root["target_fps"].is_number_integer()) {
            profile.target_fps = std::max(30, std::min(120, root["target_fps"].get<int>()));
        }
        if (root.contains("min_dodge_distance") && root["min_dodge_distance"].is_number()) {
            profile.min_dodge_distance = std::max(0.0, root["min_dodge_distance"].get<double>());
        }
        if (root.contains("max_dodge_distance") && root["max_dodge_distance"].is_number()) {
            profile.max_dodge_distance = std::max(profile.min_dodge_distance, root["max_dodge_distance"].get<double>());
        }
        if (root.contains("dry_run_enabled") && root["dry_run_enabled"].is_boolean()) {
            profile.dry_run_enabled = root["dry_run_enabled"].get<bool>();
        }
        if (root.contains("calibration_mode") && root["calibration_mode"].is_boolean()) {
            profile.calibration_mode = root["calibration_mode"].get<bool>();
        }
        if (root.contains("weapon_specific_distances") && root["weapon_specific_distances"].is_boolean()) {
            profile.weapon_specific_distances = root["weapon_specific_distances"].get<bool>();
        }
        if (root.contains("use_legacy_offsets") && root["use_legacy_offsets"].is_boolean()) {
            profile.use_legacy_offsets = root["use_legacy_offsets"].get<bool>();
        }
        if (root.contains("sound_notifications") && root["sound_notifications"].is_boolean()) {
            profile.sound_notifications = root["sound_notifications"].get<bool>();
        }
        if (root.contains("sound_event_enabled") && root["sound_event_enabled"].is_boolean()) {
            profile.sound_event_enabled = root["sound_event_enabled"].get<bool>();
        }
        if (root.contains("sound_event_disabled") && root["sound_event_disabled"].is_boolean()) {
            profile.sound_event_disabled = root["sound_event_disabled"].get<bool>();
        }
        if (root.contains("sound_event_dodge") && root["sound_event_dodge"].is_boolean()) {
            profile.sound_event_dodge = root["sound_event_dodge"].get<bool>();
        }
        if (root.contains("sound_event_hook_lost") && root["sound_event_hook_lost"].is_boolean()) {
            profile.sound_event_hook_lost = root["sound_event_hook_lost"].get<bool>();
        }
        if (root.contains("sound_event_hook_reconnected") && root["sound_event_hook_reconnected"].is_boolean()) {
            profile.sound_event_hook_reconnected = root["sound_event_hook_reconnected"].get<bool>();
        }
        if (root.contains("minimize_to_tray") && root["minimize_to_tray"].is_boolean()) {
            profile.minimize_to_tray = root["minimize_to_tray"].get<bool>();
        }
        if (root.contains("auto_recover_hook") && root["auto_recover_hook"].is_boolean()) {
            profile.auto_recover_hook = root["auto_recover_hook"].get<bool>();
        }
        if (root.contains("safe_mode_enabled") && root["safe_mode_enabled"].is_boolean()) {
            profile.safe_mode_enabled = root["safe_mode_enabled"].get<bool>();
        }
        if (root.contains("warn_on_game_update") && root["warn_on_game_update"].is_boolean()) {
            profile.warn_on_game_update = root["warn_on_game_update"].get<bool>();
        }
        if (root.contains("process_watchdog_enabled") && root["process_watchdog_enabled"].is_boolean()) {
            profile.process_watchdog_enabled = root["process_watchdog_enabled"].get<bool>();
        }
        if (root.contains("visual_hitboxes_enabled") && root["visual_hitboxes_enabled"].is_boolean()) {
            profile.visual_hitboxes_enabled = root["visual_hitboxes_enabled"].get<bool>();
        }
        if (root.contains("combo_recognition_enabled") && root["combo_recognition_enabled"].is_boolean()) {
            profile.combo_recognition_enabled = root["combo_recognition_enabled"].get<bool>();
        }
        if (root.contains("prediction_tuning_enabled") && root["prediction_tuning_enabled"].is_boolean()) {
            profile.prediction_tuning_enabled = root["prediction_tuning_enabled"].get<bool>();
        }
        if (root.contains("prediction_aggression") && root["prediction_aggression"].is_number()) {
            profile.prediction_aggression = std::max(0.0f, std::min(1.0f, static_cast<float>(root["prediction_aggression"].get<double>())));
        }
        if (root.contains("dodge_padding_x") && root["dodge_padding_x"].is_number()) {
            profile.dodge_padding_x = std::max(0.0, std::min(200.0, root["dodge_padding_x"].get<double>()));
        }
        if (root.contains("dodge_padding_y") && root["dodge_padding_y"].is_number()) {
            profile.dodge_padding_y = std::max(0.0, std::min(200.0, root["dodge_padding_y"].get<double>()));
        }
        if (root.contains("react_chance") && root["react_chance"].is_number()) {
            profile.react_chance = SanitizeFloat(static_cast<float>(root["react_chance"].get<double>()), 1.0f, 0.0f, 1.0f);
        }
        if (root.contains("offense_enabled") && root["offense_enabled"].is_boolean()) {
            profile.offense_enabled = root["offense_enabled"].get<bool>();
        }
        if (root.contains("ui_scale") && root["ui_scale"].is_number()) {
            profile.ui_scale = SanitizeFloat(static_cast<float>(root["ui_scale"].get<double>()), 1.0f, 0.75f, 1.5f);
        }
        if (root.contains("analysis_mode") && root["analysis_mode"].is_boolean()) {
            profile.analysis_mode = root["analysis_mode"].get<bool>();
        }
        if (root.contains("weapon_swap_detection") && root["weapon_swap_detection"].is_boolean()) {
            profile.weapon_swap_detection = root["weapon_swap_detection"].get<bool>();
        }
        if (root.contains("discord_rpc_enabled") && root["discord_rpc_enabled"].is_boolean()) {
            profile.discord_rpc_enabled = root["discord_rpc_enabled"].get<bool>();
        }
        if (root.contains("discord_webhook_url") && root["discord_webhook_url"].is_string()) {
            profile.discord_webhook_url = root["discord_webhook_url"].get<std::string>();
        }
        if (root.contains("github_repo") && root["github_repo"].is_string()) {
            profile.github_repo = root["github_repo"].get<std::string>();
        }
        if (root.contains("export_import_path") && root["export_import_path"].is_string()) {
            profile.export_import_path = root["export_import_path"].get<std::string>();
        }
        if (root.contains("log_level") && root["log_level"].is_string()) {
            profile.log_level = root["log_level"].get<std::string>();
        }
        if (root.contains("csv_output_path") && root["csv_output_path"].is_string()) {
            profile.csv_output_path = root["csv_output_path"].get<std::string>();
        }

        if (root.contains("hotkeys") && root["hotkeys"].is_object()) {
            const nlohmann::json hk = root["hotkeys"];
            const auto parseKey = [&](const std::string& key, std::string& value) {
                if (hk.contains(key) && hk[key].is_string()) value = hk[key].get<std::string>();
            };
            parseKey("toggle_bot", profile.hotkeys.toggle_bot);
            parseKey("dry_run", profile.hotkeys.dry_run);
            parseKey("offset_toggle", profile.hotkeys.offset_toggle);
            parseKey("calibration", profile.hotkeys.calibration);
            parseKey("safe_mode", profile.hotkeys.safe_mode);
            parseKey("reconnect", profile.hotkeys.reconnect);
            parseKey("test_dodge", profile.hotkeys.test_dodge);
            parseKey("export_diagnostics", profile.hotkeys.export_diagnostics);
            parseKey("shutdown", profile.hotkeys.shutdown);
            parseKey("dodge", profile.hotkeys.dodge);
        }

        if (root.contains("window_x") && root["window_x"].is_number_integer()) profile.window_x = root["window_x"].get<int>();
        if (root.contains("window_y") && root["window_y"].is_number_integer()) profile.window_y = root["window_y"].get<int>();
        if (root.contains("window_w") && root["window_w"].is_number_integer()) profile.window_w = root["window_w"].get<int>();
        if (root.contains("window_h") && root["window_h"].is_number_integer()) profile.window_h = root["window_h"].get<int>();
    }

    nlohmann::json profileToJson(const SavedProfile& profile) const {
        nlohmann::json root;
        root["name"] = profile.name;
        root["character"] = profile.character;
        root["weapon"] = profile.weapon;
        root["dodge_key"] = profile.dodge_key;
        root["polling_rate_ms"] = profile.polling_rate_ms;
        root["idle_polling_rate_ms"] = profile.idle_polling_rate_ms;
        root["active_polling_rate_ms"] = profile.active_polling_rate_ms;
        root["adaptive_polling"] = profile.adaptive_polling;
        root["frame_skip_enabled"] = profile.frame_skip_enabled;
        root["target_fps"] = profile.target_fps;
        root["min_dodge_distance"] = profile.min_dodge_distance;
        root["max_dodge_distance"] = profile.max_dodge_distance;
        root["dry_run_enabled"] = profile.dry_run_enabled;
        root["calibration_mode"] = profile.calibration_mode;
        root["weapon_specific_distances"] = profile.weapon_specific_distances;
        root["use_legacy_offsets"] = profile.use_legacy_offsets;
        root["sound_notifications"] = profile.sound_notifications;
        root["sound_event_enabled"] = profile.sound_event_enabled;
        root["sound_event_disabled"] = profile.sound_event_disabled;
        root["sound_event_dodge"] = profile.sound_event_dodge;
        root["sound_event_hook_lost"] = profile.sound_event_hook_lost;
        root["sound_event_hook_reconnected"] = profile.sound_event_hook_reconnected;
        root["minimize_to_tray"] = profile.minimize_to_tray;
        root["auto_recover_hook"] = profile.auto_recover_hook;
        root["safe_mode_enabled"] = profile.safe_mode_enabled;
        root["warn_on_game_update"] = profile.warn_on_game_update;
        root["process_watchdog_enabled"] = profile.process_watchdog_enabled;
        root["visual_hitboxes_enabled"] = profile.visual_hitboxes_enabled;
        root["combo_recognition_enabled"] = profile.combo_recognition_enabled;
        root["prediction_tuning_enabled"] = profile.prediction_tuning_enabled;
        root["prediction_aggression"] = profile.prediction_aggression;
        root["dodge_padding_x"] = profile.dodge_padding_x;
        root["dodge_padding_y"] = profile.dodge_padding_y;
        root["react_chance"] = profile.react_chance;
        root["offense_enabled"] = profile.offense_enabled;
        root["ui_scale"] = profile.ui_scale;
        root["analysis_mode"] = profile.analysis_mode;
        root["weapon_swap_detection"] = profile.weapon_swap_detection;
        root["discord_rpc_enabled"] = profile.discord_rpc_enabled;
        root["discord_webhook_url"] = profile.discord_webhook_url;
        root["github_repo"] = profile.github_repo;
        root["export_import_path"] = profile.export_import_path;
        root["log_level"] = profile.log_level;
        root["csv_output_path"] = profile.csv_output_path;
        root["hotkeys"]["toggle_bot"] = profile.hotkeys.toggle_bot;
        root["hotkeys"]["dry_run"] = profile.hotkeys.dry_run;
        root["hotkeys"]["offset_toggle"] = profile.hotkeys.offset_toggle;
        root["hotkeys"]["calibration"] = profile.hotkeys.calibration;
        root["hotkeys"]["safe_mode"] = profile.hotkeys.safe_mode;
        root["hotkeys"]["reconnect"] = profile.hotkeys.reconnect;
        root["hotkeys"]["test_dodge"] = profile.hotkeys.test_dodge;
        root["hotkeys"]["export_diagnostics"] = profile.hotkeys.export_diagnostics;
        root["hotkeys"]["shutdown"] = profile.hotkeys.shutdown;
        root["hotkeys"]["dodge"] = profile.hotkeys.dodge;
        root["window_x"] = profile.window_x;
        root["window_y"] = profile.window_y;
        root["window_w"] = profile.window_w;
        root["window_h"] = profile.window_h;
        return root;
    }

    std::string dodge_key = "Z";
    int polling_rate_ms = 8;
    int idle_polling_rate_ms = 40;
    int active_polling_rate_ms = 8;
    bool adaptive_polling = true;
    bool frame_skip_enabled = false;
    int target_fps = 60;
    double min_dodge_distance = 50.0;
    double max_dodge_distance = 600.0;
    bool dry_run_enabled = false;
    bool calibration_mode = false;
    bool weapon_specific_distances = true;
    bool use_legacy_offsets = false;
    bool sound_notifications = true;
    bool sound_event_enabled = true;
    bool sound_event_disabled = true;
    bool sound_event_dodge = true;
    bool sound_event_hook_lost = true;
    bool sound_event_hook_reconnected = true;
    bool minimize_to_tray = true;
    bool auto_recover_hook = true;
    bool safe_mode_enabled = true;
    bool warn_on_game_update = true;
    bool process_watchdog_enabled = true;
    bool visual_hitboxes_enabled = false;
    bool combo_recognition_enabled = true;
    bool prediction_tuning_enabled = true;
    float prediction_aggression = 0.5f;
    double dodge_padding_x = 35.0;
    double dodge_padding_y = 30.0;
    float react_chance = 1.0f;
    bool offense_enabled = false;
    float ui_scale = 1.0f;
    bool analysis_mode = false;
    bool weapon_swap_detection = true;
    bool discord_rpc_enabled = false;
    std::string discord_webhook_url = "";
    std::string github_repo = "";
    std::string export_import_path = "config/exports/shared_profile.json";
    std::string log_level = "INFO";
    std::string csv_output_path = "data/calibration.csv";
    HotkeyConfig hotkeys;
    int window_x = 100;
    int window_y = 100;
    int window_w = 1120;
    int window_h = 720;

    const std::filesystem::path& config_path() const {
        return config_path_;
    }

    std::map<std::string, SavedProfile> profiles() const {
        return profiles_;
    }

private:
    Config() {
        resetDefaults();
    }

    static std::filesystem::path getExecutableDirectory() {
#ifdef _WIN32
        char buffer[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(buffer).parent_path();
#else
        return std::filesystem::current_path();
#endif
    }

    std::filesystem::path config_path_;
    std::string active_profile = "default";
    SavedProfile current_profile_;
    std::map<std::string, SavedProfile> profiles_;
    std::mutex mutex_;
};
