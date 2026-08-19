#include "gui.h"
#include "config.h"
#include "generated/app-window.h"
#include "HotkeyManager.h"
#include "paths.h"

#include <slint.h>
#include <cstdio>
#include <thread>
#include <chrono>

namespace {
    std::string ReadLocked(UiState& state, const std::string UiState::* field) {
        std::lock_guard<std::mutex> lock(state.textMutex);
        return state.*field;
    }

    void RefreshWindow(const slint::ComponentHandle<MainWindow>& window, UiState& state) {
        window->set_active(state.active.load());
        window->set_dry_run(state.dryRun.load());
        window->set_calibration_mode(state.calibrationMode.load());
        window->set_analysis_mode(state.analysisMode.load());
        window->set_process_found(state.processFound.load());
        window->set_hook_installed(state.hookInstalled.load());
        window->set_safe_mode_active(state.safeModeActive.load());
        window->set_validation_ok(state.validationOk.load());
        window->set_dodge_count(state.dodgeCount.load());
        window->set_entity_count(state.entityCount.load());
        window->set_status_text(slint::SharedString(ReadLocked(state, &UiState::status)));
        window->set_game_version(slint::SharedString(ReadLocked(state, &UiState::gameVersion)));
        window->set_notification_text(slint::SharedString(ReadLocked(state, &UiState::notification)));
        window->set_validation_text(slint::SharedString(ReadLocked(state, &UiState::validationText)));
        window->set_debug_text(slint::SharedString(ReadLocked(state, &UiState::debugText)));
        window->set_decision_banner(slint::SharedString(ReadLocked(state, &UiState::decisionBanner)));
        window->set_hotkey_conflict(slint::SharedString(state.hotkeyConflict));

        char engagement[256];
        snprintf(engagement, sizeof(engagement),
                 "LOCAL  (%+.0f, %+.0f)\nENEMY  (%+.0f, %+.0f)\nDistance: %.0f px / %.0f px",
                 state.localX.load(), state.localY.load(),
                 state.enemyX.load(), state.enemyY.load(),
                 state.lastDistance.load(), state.lastThreshold.load());
        window->set_engagement_text(slint::SharedString(engagement));

        char decision[320];
        std::string lastMove;
        std::string lastEvent;
        {
            std::lock_guard<std::mutex> lock(state.textMutex);
            lastMove = state.lastMove;
            lastEvent = state.lastEvent;
        }
        snprintf(decision, sizeof(decision),
                 "%s\nMove: %s\nPower ID %d\nEntities: %d / %d valid, %d attacking",
                 lastEvent.c_str(), lastMove.c_str(), state.lastPowerId.load(),
                 state.validEntityCount.load(), state.entityCount.load(), state.attackCount.load());
        window->set_decision_text(slint::SharedString(decision));

        char stats[256];
        std::string most;
        {
            std::lock_guard<std::mutex> lock(state.statsMutex);
            most = state.mostDodgedMove;
        }
        snprintf(stats, sizeof(stats),
                 "Dodges attempted: %lld   successful: %lld\nAvg reaction: %.1f ms   Most dodged: %s",
                 state.dodgeAttempts, state.dodgeSuccesses, state.averageReactionMs, most.c_str());
        window->set_stats_text(slint::SharedString(stats));

        std::string recent;
        {
            std::lock_guard<std::mutex> lock(state.statsMutex);
            const size_t count = state.dodgeTimeline.size();
            const size_t start = count > 12 ? count - 12 : 0;
            for (size_t i = start; i < count; ++i) {
                if (!recent.empty()) recent += "\n";
                recent += state.dodgeTimeline[i];
            }
        }
        window->set_recent_decisions_text(slint::SharedString(recent));
    }

    void RefreshProfiles(const slint::ComponentHandle<MainWindow>& window) {
        Config& config = Config::instance();
        std::vector<slint::SharedString> names;
        for (const auto& name : config.listProfiles()) names.emplace_back(name);
        if (names.empty()) names.emplace_back("default");
        window->set_profiles(std::make_shared<slint::VectorModel<slint::SharedString>>(names));
        window->set_active_profile(slint::SharedString(config.activeProfileName()));
    }

    void PushInitialConfig(const slint::ComponentHandle<MainWindow>& window) {
        Config& config = Config::instance();
        window->set_min_dodge_distance(static_cast<float>(config.min_dodge_distance));
        char label[32];
        snprintf(label, sizeof(label), "%.0f px", config.min_dodge_distance);
        window->set_min_dodge_distance_label(slint::SharedString(label));
        window->set_auto_recover(config.auto_recover_hook);
        window->set_safe_mode(config.safe_mode_enabled);
        window->set_process_watchdog(config.process_watchdog_enabled);
        window->set_adaptive_polling(config.adaptive_polling);
        window->set_frame_skip(config.frame_skip_enabled);
        window->set_sound_notifications(config.sound_notifications);
        window->set_minimize_to_tray(config.minimize_to_tray);
        window->set_dodge_padding_x(static_cast<float>(config.dodge_padding_x));
        window->set_dodge_padding_y(static_cast<float>(config.dodge_padding_y));
        window->set_react_chance(config.react_chance);
        window->set_offense_enabled(config.offense_enabled);
        window->set_combo_recognition(config.combo_recognition_enabled);
        window->set_prediction_tuning(config.prediction_tuning_enabled);
        window->set_prediction_aggression(config.prediction_aggression);
        window->set_ui_scale(config.ui_scale);
        char padX[32], padY[32], reactLabel[16], agg[16], scale[16];
        snprintf(padX, sizeof(padX), "%.0f px", config.dodge_padding_x);
        snprintf(padY, sizeof(padY), "%.0f px", config.dodge_padding_y);
        snprintf(reactLabel, sizeof(reactLabel), "%.0f%%", config.react_chance * 100.0f);
        snprintf(agg, sizeof(agg), "%.0f%%", config.prediction_aggression * 100.0f);
        snprintf(scale, sizeof(scale), "%.0f%%", config.ui_scale * 100.0f);
        window->set_dodge_padding_x_label(slint::SharedString(padX));
        window->set_dodge_padding_y_label(slint::SharedString(padY));
        window->set_react_chance_label(slint::SharedString(reactLabel));
        window->set_prediction_aggression_label(slint::SharedString(agg));
        window->set_ui_scale_label(slint::SharedString(scale));
        window->set_hotkey_help(slint::SharedString(
            config.hotkeys.toggle_bot + " toggle  " +
            config.hotkeys.dry_run + " dry-run  " +
            config.hotkeys.safe_mode + " Safe Mode  " +
            config.hotkeys.shutdown + " exit"));
        RefreshProfiles(window);
    }
}

int Gui::run(UiState& state) {
    auto window = MainWindow::create();

    window->on_toggle_active([&state] { state.actions.request(AppAction::ToggleBot); });
    window->on_toggle_dry_run([&state] { state.actions.request(AppAction::DryRun); });
    window->on_toggle_calibration([&state] { state.actions.request(AppAction::Calibration); });
    window->on_toggle_analysis([&state] {
        state.analysisMode = !state.analysisMode.load();
        Config::instance().analysis_mode = state.analysisMode.load();
    });
    window->on_toggle_safe_mode([&state] { state.actions.request(AppAction::SafeMode); });
    window->on_request_reconnect([&state] { state.actions.request(AppAction::Reconnect); });
    window->on_request_test_dodge([&state] { state.actions.request(AppAction::TestDodge); });
    window->on_export_diagnostics([&state] { state.actions.request(AppAction::ExportDiagnostics); });
    window->on_reset_calibration([&state] { state.resetCalibration = true; });
    window->on_toggle_debug([window] { window->set_debug_expanded(!window->get_debug_expanded()); });
    window->on_open_logs([&state] { state.openLogsFolder = true; });
    window->on_open_config([&state] { state.openConfigFolder = true; });

    window->on_min_dodge_distance_changed([window](float value) {
        Config::instance().min_dodge_distance = value;
        char label[32];
        snprintf(label, sizeof(label), "%.0f px", value);
        window->set_min_dodge_distance_label(slint::SharedString(label));
    });
    window->on_auto_recover_changed([](bool value) { Config::instance().auto_recover_hook = value; });
    window->on_safe_mode_changed([](bool value) { Config::instance().safe_mode_enabled = value; });
    window->on_process_watchdog_changed([](bool value) { Config::instance().process_watchdog_enabled = value; });
    window->on_adaptive_polling_changed([](bool value) { Config::instance().adaptive_polling = value; });
    window->on_frame_skip_changed([](bool value) { Config::instance().frame_skip_enabled = value; });
    window->on_sound_notifications_changed([](bool value) { Config::instance().sound_notifications = value; });
    window->on_minimize_to_tray_changed([](bool value) { Config::instance().minimize_to_tray = value; });
    window->on_save_config([] { Config::instance().saveToFile(); });
    window->on_reset_settings([window] {
        Config::instance().resetDefaults();
        PushInitialConfig(window);
    });

    window->on_dodge_padding_x_changed([window](float value) {
        Config::instance().dodge_padding_x = value;
        char label[32];
        snprintf(label, sizeof(label), "%.0f px", value);
        window->set_dodge_padding_x_label(slint::SharedString(label));
    });
    window->on_dodge_padding_y_changed([window](float value) {
        Config::instance().dodge_padding_y = value;
        char label[32];
        snprintf(label, sizeof(label), "%.0f px", value);
        window->set_dodge_padding_y_label(slint::SharedString(label));
    });
    window->on_react_chance_changed([window](float value) {
        Config::instance().react_chance = value;
        char label[16];
        snprintf(label, sizeof(label), "%.0f%%", value * 100.0f);
        window->set_react_chance_label(slint::SharedString(label));
    });
    window->on_offense_enabled_changed([&state](bool value) {
        Config::instance().offense_enabled = value;
        state.offenseEnabled = value;
    });
    window->on_combo_recognition_changed([](bool value) { Config::instance().combo_recognition_enabled = value; });
    window->on_prediction_tuning_changed([](bool value) { Config::instance().prediction_tuning_enabled = value; });
    window->on_prediction_aggression_changed([window](float value) {
        Config::instance().prediction_aggression = value;
        char label[16];
        snprintf(label, sizeof(label), "%.0f%%", value * 100.0f);
        window->set_prediction_aggression_label(slint::SharedString(label));
    });
    window->on_ui_scale_changed([window](float value) {
        Config::instance().ui_scale = value;
        char label[16];
        snprintf(label, sizeof(label), "%.0f%%", value * 100.0f);
        window->set_ui_scale_label(slint::SharedString(label));
    });

    window->on_save_profile([window](slint::SharedString name) {
        Config::instance().saveCurrentProfile(std::string(name));
        RefreshProfiles(window);
        window->set_profiles_status_text(slint::SharedString("Saved profile '" + std::string(name) + "'"));
    });
    window->on_load_profile([window](slint::SharedString name) {
        const bool ok = Config::instance().loadProfile(std::string(name));
        RefreshProfiles(window);
        PushInitialConfig(window);
        window->set_profiles_status_text(slint::SharedString(ok ? "Loaded profile '" + std::string(name) + "'" : "Profile not found"));
    });
    window->on_duplicate_profile([window](slint::SharedString from, slint::SharedString to) {
        Config& config = Config::instance();
        if (config.loadProfile(std::string(from))) config.saveCurrentProfile(std::string(to));
        RefreshProfiles(window);
        window->set_profiles_status_text(slint::SharedString("Duplicated '" + std::string(from) + "' as '" + std::string(to) + "'"));
    });
    window->on_delete_profile([window](slint::SharedString name) {
        const bool ok = Config::instance().deleteProfile(std::string(name));
        RefreshProfiles(window);
        window->set_profiles_status_text(slint::SharedString(ok ? "Deleted profile '" + std::string(name) + "'" : "Cannot delete this profile"));
    });
    window->on_reset_profile([window] {
        Config::instance().resetDefaults();
        RefreshProfiles(window);
        PushInitialConfig(window);
        window->set_profiles_status_text(slint::SharedString("Reset to defaults"));
    });
    window->on_export_profile([window] {
        const bool ok = Config::instance().exportProfileToFile(Config::instance().export_import_path);
        window->set_profiles_status_text(slint::SharedString(ok ? "Exported profile" : "Export failed"));
    });
    window->on_import_profile([window] {
        const bool ok = Config::instance().importProfileFromFile(Config::instance().export_import_path);
        RefreshProfiles(window);
        PushInitialConfig(window);
        window->set_profiles_status_text(slint::SharedString(ok ? "Imported profile" : "Import failed"));
    });

    PushInitialConfig(window);
    RefreshWindow(window, state);

    std::thread pollThread([window, &state] {
        while (!state.shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (state.shutdown.load()) break;
            slint::invoke_from_event_loop([window, &state] { RefreshWindow(window, state); });
        }
    });

    window->window().on_close_requested([&state] {
        state.shutdown = true;
        state.actions.request(AppAction::Shutdown);
        return slint::CloseRequestResponse::HideWindow;
    });

    window->run();
    state.shutdown = true;
    if (pollThread.joinable()) pollThread.join();
    return 0;
}
