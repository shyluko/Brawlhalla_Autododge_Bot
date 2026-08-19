#include "AutododgeBot.h"

#include "DefenseController.h"
#include "DecisionFeed.h"
#include "GameDataManager.h"
#include "GameVersionDetector.h"
#include "HotkeyManager.h"
#include "InputController.h"
#include "LocalCalibrator.h"
#include "OffenseController.h"
#include "OffsetValidator.h"
#include "combo_engine.h"
#include "config.h"
#include "logger.h"
#include "memory.h"
#include "numeric.h"
#include "offsets.h"
#include "paths.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

namespace {
void PlayTone(int frequency, int durationMs, int gapMs = 18) {
    if (frequency <= 0 || durationMs <= 0) return;
    Beep(static_cast<DWORD>(frequency), static_cast<DWORD>(durationMs));
    if (gapMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
}

void NotifySound(const std::string& event) {
    Config& cfg = Config::instance();
    if (!cfg.sound_notifications) return;
    bool allow = true;
    if (event == "enabled") allow = cfg.sound_event_enabled;
    else if (event == "disabled") allow = cfg.sound_event_disabled;
    else if (event == "dodge") allow = cfg.sound_event_dodge;
    else if (event == "hook_lost") allow = cfg.sound_event_hook_lost;
    else if (event == "hook_reconnected") allow = cfg.sound_event_hook_reconnected;
    if (!allow) return;
    if (event == "enabled") { PlayTone(440, 65); PlayTone(660, 80); }
    else if (event == "disabled") { PlayTone(260, 60); PlayTone(200, 90); }
    else if (event == "dodge") { PlayTone(520, 55); PlayTone(700, 65); PlayTone(900, 85); }
    else if (event == "hook_lost") { PlayTone(220, 80); PlayTone(180, 130); }
    else if (event == "hook_reconnected") { PlayTone(540, 65); PlayTone(720, 80); PlayTone(860, 90); }
}

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

struct AutododgeBot::Impl {
    UiState& ui;
    Memory mem;
    HWND hwnd = nullptr;
    uintptr_t pointer_array = 0;
    uintptr_t index_store = 0;
    uintptr_t hookTarget = 0;
    uintptr_t codeCave = 0;
    bool active = false;
    bool useNewOffsets = true;
    GameDataManager gameData;
    InputController input;
    HotkeyManager hotkeys;
    OffsetValidator validator;
    DecisionFeed decisions;
    LocalCalibrator calibrator;
    ComboEngine combo;
    VelocityTracker youVel;
    VelocityTracker enyVel;
    std::ofstream calibrationLog;
    std::ofstream csvLog;
    int consecutiveHookFailures = 0;
    std::string lastGameVersion = "unknown";
    DWORD lastProcessId = 0;
    Actor localActor;
    Actor enemyActor;
    double local_x = 0, local_y = 0;
    double enemy_x = 0, enemy_y = 0;
    int enemy_powerID = 0;
    bool enemy_attacking = false;
    static constexpr int kAutoPauseAfterFailures = 5;

    explicit Impl(UiState& state) : ui(state) {}

    ~Impl() {
        RestoreHook();
        input.Reset();
        if (calibrationLog.is_open()) calibrationLog.close();
        if (csvLog.is_open()) csvLog.close();
    }

    void SetStatus(const std::string& status, const std::string& event = {}) {
        std::lock_guard<std::mutex> lock(ui.textMutex);
        ui.status = status;
        if (!event.empty()) ui.lastEvent = event;
    }

    void Notify(const std::string& text) {
        std::lock_guard<std::mutex> lock(ui.textMutex);
        ui.notification = text;
        ui.lastEvent = text;
    }

    void RestoreHook() {
        if (!hookTarget && !codeCave) return;
        if (hookTarget) {
            const std::vector<uint8_t> original = {0xF2, 0x0F, 0x10, 0x8B, 0x40, 0x01, 0x00, 0x00};
            mem.write_bytes(hookTarget, original);
        }
        if (codeCave) mem.free_memory(codeCave);
        hookTarget = 0;
        codeCave = 0;
        ui.hookInstalled = false;
        input.Reset();
    }

    void EnterSafeMode(const std::string& reason, bool automatic) {
        input.Reset();
        active = false;
        ui.active = false;
        ui.safeModeActive = true;
        combo.Clear();
        youVel.Reset();
        enyVel.Reset();
        decisions.clearTransient();
        {
            std::lock_guard<std::mutex> lock(ui.textMutex);
            ui.safeModeReason = reason;
            ui.lastEvent = std::string(automatic ? "Safe Mode (auto): " : "Safe Mode: ") + reason;
            ui.decisionBanner.clear();
        }
        Logger::warn(std::string(automatic ? "Safe Mode entered automatically: " : "Safe Mode entered: ") + reason);
        NotifySound("disabled");
    }

    void ExitSafeMode() {
        ui.safeModeActive = false;
        std::lock_guard<std::mutex> lock(ui.textMutex);
        ui.safeModeReason.clear();
        ui.lastEvent = "Safe Mode cleared";
        Logger::info("Safe Mode cleared");
    }

    void OpenCsv() {
        Config& cfg = Config::instance();
        if (csvLog.is_open()) csvLog.close();
        std::filesystem::path path = cfg.csv_output_path;
        if (path.empty()) path = "data/calibration.csv";
        if (path.is_relative()) path = ExecutableDirectory() / path;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        csvLog.open(path, std::ios::out | std::ios::app);
    }

    void WriteCsvRow(double distance, bool dodgeTriggered, bool hitConfirmed) {
        const auto now = NowMs();
        auto write = [&](std::ofstream& out) {
            if (!out) return;
            out << now << ',' << local_x << ',' << local_y << ','
                << enemy_x << ',' << enemy_y << ',' << enemy_powerID << ','
                << distance << ',' << (dodgeTriggered ? 1 : 0) << ','
                << (hitConfirmed ? 1 : 0) << '\n';
            out.flush();
        };
        write(calibrationLog);
        write(csvLog);
    }

    Actor readActor(uintptr_t dp) {
        Actor a;
        if (dp == 0) return a;
        a.dp = dp;
        if (!mem.try_read(dp + Offsets::POS_X, a.pos_x)) return a;
        if (!mem.try_read(dp + Offsets::POS_Y, a.pos_y)) return a;
        if (!IsFiniteNumber(a.pos_x) || !IsFiniteNumber(a.pos_y)) return a;
        if (a.pos_x < -5000 || a.pos_x > 5000 || a.pos_y < -5000 || a.pos_y > 5000) return a;
        a.valid_position = true;

        a.light_attack = mem.read<uint8_t>(dp + Offsets::LIGHT_ATTACK);
        a.attack_family = mem.read<uint8_t>(dp + Offsets::ATTACK_FAMILY);
        a.heavy_attack = mem.read<uint8_t>(dp + Offsets::HEAVY_ATTACK);
        a.is_attacking = (a.light_attack > 0 || a.attack_family > 0 || a.heavy_attack > 0);
        a.attack_id = mem.read<int>(dp + Offsets::ATTACK_ID);
        a.entity_ptr = mem.read<uintptr_t>(dp + Offsets::ENTITY_PTR);
        a.weapon_ptr = mem.read<uintptr_t>(dp + Offsets::WEAPON_PTR);
        if (a.entity_ptr != 0 && a.weapon_ptr != 0 && a.entity_ptr == a.weapon_ptr) a.weapon_ptr = 0;

        if (a.weapon_ptr > 0x10000 && a.weapon_ptr < 0x7FFFFFFFFFFF) {
            const uintptr_t typePtr = mem.read<uintptr_t>(a.weapon_ptr + Offsets::WEAPON_TYPE_PTR);
            if (typePtr > 0x10000 && typePtr < 0x7FFFFFFFFFFF) {
                const int typeId = mem.read<int>(typePtr + Offsets::WEAPON_TYPE_ID);
                if (typeId >= 0 && typeId <= 32) a.weapon_type_id = typeId;
            }
        }

        if (a.entity_ptr > 0x10000 && a.entity_ptr < 0x7FFFFFFFFFFF) {
            a.facing = mem.read<int>(a.entity_ptr + Offsets::FACING);
            a.airborne = mem.read<int>(a.entity_ptr + (useNewOffsets ? Offsets::AIRBORNE_CURRENT : Offsets::AIRBORNE_LEGACY));
            a.can_dodge = mem.read<int>(a.entity_ptr + (useNewOffsets ? Offsets::CAN_DODGE_CURRENT : Offsets::CAN_DODGE_LEGACY));
            a.is_stunned = mem.read<int>(a.entity_ptr + Offsets::IS_STUNNED);
            a.local_marker = mem.read<int>(a.entity_ptr + Offsets::LOCAL_MARKER);
            a.damage = mem.read<double>(a.entity_ptr + Offsets::DAMAGE);
            const uintptr_t legendPtr = mem.read<uintptr_t>(a.entity_ptr + Offsets::LEGEND_PTR);
            if (legendPtr > 0x10000 && legendPtr < 0x7FFFFFFFFFFF) {
                a.legend_id = mem.read<int>(legendPtr + Offsets::LEGEND_ID);
                a.has_legend = (a.legend_id > 0 && a.legend_id < 1000);
            }
            if (!a.has_legend && a.local_marker == 9) a.has_legend = true;
            a.is_dead = mem.read<int>(a.entity_ptr + Offsets::IS_DEAD) != 0;
            a.is_edging = mem.read<int>(a.entity_ptr + Offsets::IS_EDGING) != 0;
            a.remaining_options = mem.read<int>(a.entity_ptr + Offsets::REMAINING_OPTIONS);
            a.stocks = mem.read<int>(a.entity_ptr + Offsets::STOCKS);
            a.knocked_back = mem.read<int>(a.entity_ptr + Offsets::KNOCKED_BACK);
        }
        a.valid = true;
        return a;
    }

    bool installHook() {
        if (hookTarget && codeCave) return true;
        if (!mem.init("Brawlhalla", hwnd)) {
            ui.processFound = false;
            ui.hookInstalled = false;
            SetStatus("Waiting for Brawlhalla");
            return false;
        }
        ui.processFound = true;
        const uintptr_t target_addr = mem.find_pattern_safe(Offsets::CAPTURE_PATTERN, Offsets::CAPTURE_MASK);
        if (!target_addr) {
            SetStatus("Waiting for match data", "Capture signature not present yet");
            ui.hookInstalled = false;
            return false;
        }
        uintptr_t cave_addr = mem.allocate_memory_near(target_addr, 0x1000);
        if (!cave_addr) {
            Logger::error("Could not allocate memory");
            return false;
        }
        codeCave = cave_addr;
        pointer_array = cave_addr + 0x100;
        index_store = cave_addr + 0x80;
        const uintptr_t return_addr = target_addr + 8;
        std::vector<uint8_t> shellcode = {
            0x50, 0x51, 0x52,
            0x48, 0xB8, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x8B, 0x08, 0x83, 0xF9, 0x32, 0x7C, 0x02, 0x31, 0xC9,
            0x48, 0xBA, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x48, 0x89, 0x1C, 0xCA, 0xFF, 0xC1, 0x89, 0x08,
            0x5A, 0x59, 0x58,
            0xF2, 0x0F, 0x10, 0x8B, 0x40, 0x01, 0x00, 0x00,
            0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
        };
        memcpy(&shellcode[5], &index_store, 8);
        memcpy(&shellcode[24], &pointer_array, 8);
        memcpy(&shellcode[57], &return_addr, 8);
        if (!mem.write_bytes(cave_addr, shellcode)) {
            Logger::error("Failed to write shellcode");
            return false;
        }
        std::vector<uint8_t> patch = {0xE9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90};
        DWORD relative_jmp = static_cast<DWORD>(cave_addr - target_addr - 5);
        memcpy(&patch[1], &relative_jmp, 4);
        if (!mem.write_bytes(target_addr, patch)) {
            Logger::error("Failed to install hook");
            return false;
        }
        hookTarget = target_addr;
        ui.hookInstalled = true;
        SetStatus("Hook installed");
        Logger::info("Hook successful");
        return true;
    }

    void RefreshVersion() {
        const std::string version = GameVersionDetector::ReadProductVersion(mem.hproccess);
        if (lastGameVersion != "unknown" && version != "unknown" && version != lastGameVersion) {
            Logger::versionChange("Game version changed from " + lastGameVersion + " to " + version);
            Notify("Game version changed: " + version + " — offsets need explicit validation");
            if (ui.gameUpdateWarningEnabled.load() || Config::instance().warn_on_game_update) {
                EnterSafeMode("game version changed", true);
            }
        }
        if (version != "unknown") lastGameVersion = version;
        std::lock_guard<std::mutex> lock(ui.textMutex);
        ui.gameVersion = lastGameVersion;
    }

    void PublishDecision(const DecisionEvent& event) {
        decisions.push(event);
        ui.lastFrame = event.frame;
        ui.lastConfidence = event.confidence;
        std::lock_guard<std::mutex> lock(ui.textMutex);
        ui.decisionBanner = event.banner();
        if (!event.move.empty()) ui.lastMove = event.move;
        Logger::decision(event.kind + " | " + event.move + " | " + event.reason);
        std::lock_guard<std::mutex> stats(ui.statsMutex);
        ui.dodgeTimeline.push_back(event.kind + " | " + event.move + (event.reason.empty() ? "" : " | " + event.reason));
        if (ui.dodgeTimeline.size() > 50) ui.dodgeTimeline.erase(ui.dodgeTimeline.begin());
    }

    void UpdateBanner() {
        const auto latest = decisions.latest();
        std::lock_guard<std::mutex> lock(ui.textMutex);
        ui.decisionBanner = latest.banner();
    }

    void ExportDiagnostics() {
        const auto path = LogsDirectory() / ("diagnostics_" + std::to_string(std::time(nullptr)) + ".txt");
        std::ofstream out(path);
        if (!out) {
            Logger::error("Could not export diagnostics");
            return;
        }
        out << validator.last.toText() << "\n";
        out << "Game version: " << lastGameVersion << "\n";
        out << "Hook: " << (hookTarget ? "yes" : "no") << "\n";
        out << "Safe Mode: " << (ui.safeModeActive.load() ? "yes" : "no") << "\n";
        {
            std::lock_guard<std::mutex> lock(ui.statsMutex);
            out << "Recent decisions:\n";
            for (const auto& line : ui.dodgeTimeline) out << "  " << line << "\n";
        }
        Logger::diagnostic("Exported " + path.string());
        Notify("Diagnostics exported");
    }

    void HandleAction(AppAction action) {
        switch (action) {
            case AppAction::ToggleBot:
                if (ui.safeModeActive.load()) ExitSafeMode();
                active = !active;
                ui.active = active;
                if (!active) input.Reset();
                NotifySound(active ? "enabled" : "disabled");
                Logger::info(std::string("Autododge ") + (active ? "ENABLED" : "DISABLED"));
                break;
            case AppAction::DryRun:
                ui.dryRun = !ui.dryRun.load();
                Logger::info(std::string("Dry-run ") + (ui.dryRun.load() ? "ON" : "OFF"));
                break;
            case AppAction::OffsetToggle:
                useNewOffsets = !useNewOffsets;
                Config::instance().use_legacy_offsets = !useNewOffsets;
                Logger::diagnostic(std::string("Explicit offset set: ") + (useNewOffsets ? "POST-PATCH candidates for airborne/CanDodge" : "LEGACY airborne/CanDodge"));
                Notify(useNewOffsets ? "Using post-patch airborne/CanDodge" : "Using legacy airborne/CanDodge");
                break;
            case AppAction::Calibration:
                ui.calibrationMode = !ui.calibrationMode.load();
                Logger::calibration(std::string("Calibration logging ") + (ui.calibrationMode.load() ? "ON" : "OFF"));
                break;
            case AppAction::SafeMode:
                if (ui.safeModeActive.load()) ExitSafeMode();
                else EnterSafeMode("manual pause", false);
                break;
            case AppAction::Reconnect:
                ui.manualReconnect = true;
                break;
            case AppAction::TestDodge:
                ui.testInput = true;
                break;
            case AppAction::ExportDiagnostics:
                ExportDiagnostics();
                break;
            case AppAction::Shutdown:
                ui.shutdown = true;
                input.Reset();
                RestoreHook();
                break;
            default:
                break;
        }
    }

    bool init() {
        Config::instance().loadFromFile();
        Config::instance().sanitizeRuntime();
        hotkeys.reloadFromConfig();
        ui.hotkeyConflict = hotkeys.conflictReport();
        ui.dryRun = Config::instance().dry_run_enabled;
        ui.calibrationMode = Config::instance().calibration_mode;
        ui.analysisMode = Config::instance().analysis_mode;
        ui.offenseEnabled = Config::instance().offense_enabled;
        useNewOffsets = !Config::instance().use_legacy_offsets;
        calibrationLog.open("calibration.log", std::ios::out | std::ios::app);
        OpenCsv();
        Logger::info("Brawlhalla Autododge started");
        if (!hotkeys.conflictReport().empty()) Logger::warn(hotkeys.conflictReport());
        if (!gameData.loadAllData()) Logger::warn("Could not load all game data, using defaults");
        ui.moveCount = gameData.getMoveCount();
        ui.powerIdCount = gameData.getPowerIdCount();
        installHook();
        RefreshVersion();
        return true;
    }

    void run() {
        using clock = std::chrono::steady_clock;
        auto lastFrame = clock::now();
        std::chrono::milliseconds accumulator{0};
        bool dodgeArmed = true;
        ActiveThreat activeThreat;
        bool previousEnemyAttacking = false;
        long long lastDodgeAtMs = 0;
        int lastReportedAttackId = 0;
        constexpr long long DODGE_COOLDOWN_MS = 220;
        auto lastValidate = clock::now();

        while (!ui.shutdown.load()) {
            const auto now = clock::now();
            accumulator += std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrame);
            lastFrame = now;

            hotkeys.poll(ui.actions);
            HandleAction(ui.actions.take());

            if (ui.resetCalibration.exchange(false)) {
                calibrator.reset();
                Logger::calibration("Local-player calibration reset");
            }
            if (ui.openLogsFolder.exchange(false)) OpenFolderInExplorer(LogsDirectory());
            if (ui.openConfigFolder.exchange(false)) OpenFolderInExplorer(ConfigDirectory());
            if (ui.exportDiagnostics.exchange(false)) ExportDiagnostics();

            if (ui.testInput.exchange(false)) {
                const WORD dodgeKey = HotkeyManager::Parse(Config::instance().hotkeys.dodge.empty() ? Config::instance().dodge_key : Config::instance().hotkeys.dodge, 'Z');
                if (!ui.dryRun.load() && !ui.safeModeActive.load()) input.Press(dodgeKey);
                Notify("Manual dodge input sent");
            }

            Config& cfg = Config::instance();
            int activeTickMs = SanitizeInt(cfg.active_polling_rate_ms, 8, 1, 100);
            int idleTickMs = SanitizeInt(cfg.idle_polling_rate_ms, 40, 10, 500);
            if (cfg.frame_skip_enabled) {
                const int frameLimitMs = std::max(1, static_cast<int>(1000.0 / std::max(1, cfg.target_fps)));
                activeTickMs = std::max(activeTickMs, frameLimitMs);
                idleTickMs = std::max(idleTickMs, frameLimitMs);
            }
            int currentTickMs = (ui.validEntityCount.load() > 0) ? activeTickMs : idleTickMs;
            if (!cfg.adaptive_polling) currentTickMs = activeTickMs;

            while (accumulator >= std::chrono::milliseconds{currentTickMs}) {
                accumulator -= std::chrono::milliseconds{currentTickMs};
                tick(dodgeArmed, activeThreat, previousEnemyAttacking, lastDodgeAtMs, lastReportedAttackId, DODGE_COOLDOWN_MS);
            }

            if (now - lastValidate > std::chrono::seconds(2)) {
                lastValidate = now;
                const auto report = validator.run(lastGameVersion, ui.hookInstalled.load(), hookTarget,
                    ui.entityCount.load(), ui.validEntityCount.load(), local_x != 0 || localActor.valid,
                    enemyActor.valid, gameData.getMoveCount(), ui.processFound.load());
                ui.validationOk = !report.criticalFailure;
                {
                    std::lock_guard<std::mutex> lock(ui.textMutex);
                    ui.validationText = report.toText();
                }
                if (report.criticalFailure && ui.safeModeEnabled.load() && !ui.safeModeActive.load()) {
                    EnterSafeMode(report.summary, true);
                }
            }

            UpdateBanner();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        input.Reset();
        RestoreHook();
    }

    void tick(bool& dodgeArmed, ActiveThreat& activeThreat, bool& previousEnemyAttacking,
              long long& lastDodgeAtMs, int& lastReportedAttackId, long long dodgeCooldownMs) {
        Config& cfg = Config::instance();
        const bool autoRecover = ui.autoRecoverEnabled.load() || cfg.auto_recover_hook;
        const bool processWatchdog = ui.processWatchdogEnabled.load() || cfg.process_watchdog_enabled;

        if (ui.manualReconnect.exchange(false)) {
            RestoreHook();
            mem.close();
            if (!installHook()) {
                consecutiveHookFailures++;
                if (consecutiveHookFailures >= kAutoPauseAfterFailures) EnterSafeMode("hook unavailable", true);
                return;
            }
            consecutiveHookFailures = 0;
            NotifySound("hook_reconnected");
        }

        HWND hwndCandidate = FindWindowA(nullptr, "Brawlhalla");
        DWORD processId = 0;
        if (hwndCandidate) GetWindowThreadProcessId(hwndCandidate, &processId);
        ui.processFound = processId != 0;
        if (processWatchdog && lastProcessId != 0 && processId == 0) {
            RestoreHook();
            mem.close();
            EnterSafeMode("game process disappeared", true);
            lastProcessId = 0;
            return;
        }
        if (processWatchdog && processId != 0 && processId != lastProcessId && lastProcessId != 0) {
            Logger::warn("Brawlhalla process ID changed");
            RestoreHook();
            mem.close();
            installHook();
            RefreshVersion();
        }
        lastProcessId = processId;

        if (autoRecover && (!hookTarget || !codeCave || !mem.hproccess) && processId != 0) {
            if (hookTarget || codeCave) NotifySound("hook_lost");
            if (!installHook()) {
                if (++consecutiveHookFailures >= kAutoPauseAfterFailures) EnterSafeMode("hook unavailable", true);
                return;
            }
            consecutiveHookFailures = 0;
            NotifySound("hook_reconnected");
            RefreshVersion();
        }

        if (ui.safeModeActive.load()) {
            input.Reset();
            SetStatus("Safe Mode", ui.safeModeReason);
            return;
        }

        active = ui.active.load();
        if (!active) {
            input.Reset();
            return;
        }

        std::vector<uintptr_t> unique_entities;
        for (int i = 0; i < 50; ++i) {
            uintptr_t current_dp = mem.read<uintptr_t>(pointer_array + (i * 8));
            if (current_dp != 0 && std::find(unique_entities.begin(), unique_entities.end(), current_dp) == unique_entities.end()) {
                unique_entities.push_back(current_dp);
            }
        }
        ui.entityCount = static_cast<int>(unique_entities.size());
        ui.validEntityCount = 0;
        ui.attackCount = 0;

        std::vector<Actor> actors;
        for (uintptr_t dp : unique_entities) {
            Actor a = readActor(dp);
            if (!IsPlausibleFighter(a)) continue;
            actors.push_back(a);
            ui.validEntityCount++;
        }

        const long long nowMs = NowMs();
        int heldSign = 0;
        if ((GetAsyncKeyState(VK_LEFT) & 0x8000) && !(GetAsyncKeyState(VK_RIGHT) & 0x8000)) heldSign = -1;
        if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) && !(GetAsyncKeyState(VK_LEFT) & 0x8000)) heldSign = 1;
        if (ui.calibrationMode.load() || ui.analysisMode.load()) {
            calibrator.step(actors, nowMs, heldSign);
            Logger::calibration(calibrator.status());
        }

        local_x = local_y = enemy_x = enemy_y = 0;
        enemy_powerID = 0;
        enemy_attacking = false;
        bool local_found = false;
        bool enemy_found = false;
        localActor = {};
        enemyActor = {};

        for (const Actor& a : actors) {
            if (a.local_marker == 9 || (calibrator.localDp != 0 && a.dp == calibrator.localDp)) {
                localActor = a;
                local_x = a.pos_x;
                local_y = a.pos_y;
                local_found = true;
                break;
            }
        }
        if (!local_found && !actors.empty()) {
            localActor = actors.front();
            local_x = localActor.pos_x;
            local_y = localActor.pos_y;
            local_found = true;
        }
        for (const Actor& a : actors) {
            if (local_found && a.dp == localActor.dp) continue;
            if (!enemy_found) {
                enemyActor = a;
                enemy_x = a.pos_x;
                enemy_y = a.pos_y;
                enemy_found = true;
            }
            if (a.is_attacking) {
                enemy_attacking = true;
                enemy_powerID = a.attack_id;
                enemyActor = a;
                enemy_x = a.pos_x;
                enemy_y = a.pos_y;
                ui.attackCount++;
            }
        }

        ui.localX = local_x;
        ui.localY = local_y;
        ui.enemyX = enemy_x;
        ui.enemyY = enemy_y;

        if (ui.analysisMode.load() && local_found) {
            std::ostringstream debug;
            debug << "Local dp=" << localActor.dp
                  << " facing=" << localActor.facing
                  << " dmg=" << localActor.damage
                  << " airborne=" << localActor.airborne
                  << " canDodge=" << localActor.can_dodge
                  << " weapon=" << localActor.weapon_type_id
                  << "\nCandidates are not applied automatically. Use F3 only to switch documented airborne/CanDodge pairs.";
            std::lock_guard<std::mutex> lock(ui.textMutex);
            ui.debugText = debug.str();
        }

        if (cfg.safe_mode_enabled && (!local_found || !enemy_found)) {
            previousEnemyAttacking = false;
            SetStatus("Hook installed", "Safe mode: waiting for valid fight state");
            return;
        }

        youVel.Update(local_x, local_y, nowMs);
        enyVel.Update(enemy_x, enemy_y, nowMs);

        const bool attackStart = enemy_attacking && (!previousEnemyAttacking || activeThreat.attack_id != enemy_powerID || activeThreat.attack_id == 0);
        if (enemy_attacking && (activeThreat.attack_id == 0 || activeThreat.attack_id != enemy_powerID)) {
            activeThreat.attack_id = enemy_powerID;
            activeThreat.start_timestamp = std::chrono::steady_clock::now();
            activeThreat.hasReacted = false;
        } else if (!enemy_attacking) {
            activeThreat = ActiveThreat{};
            dodgeArmed = true;
        }

        if (local_found && enemy_found && enemy_attacking) {
            const double startupSeconds = gameData.getStartupFrames(enemy_powerID) / 60.0;
            const double predictedEnemyX = enemy_x + enyVel.velX * startupSeconds;
            const double predictedEnemyY = enemy_y + enyVel.velY * startupSeconds;
            const double dist = std::hypot(local_x - predictedEnemyX, local_y - predictedEnemyY);
            if (!IsFiniteNumber(dist)) return;

            double optimal_dist = gameData.getDodgeDistance(enemy_powerID);
            if (cfg.prediction_tuning_enabled) {
                const double aggression = SanitizeDouble(cfg.prediction_aggression, 0.5, 0.0, 1.0);
                optimal_dist *= 0.85 + (aggression * 0.35);
            }
            const std::string moveName = gameData.getMoveName(enemy_powerID);
            ui.lastPowerId = enemy_powerID;
            ui.lastDistance = dist;
            ui.lastThreshold = optimal_dist;

            const bool hitboxConfirms = DefenseController::ThreatensPosition(
                enemyActor, local_x, local_y, dist, optimal_dist, cfg.dodge_padding_x, cfg.dodge_padding_y);
            const bool withinDodgeWindow = hitboxConfirms && dist > cfg.min_dodge_distance;
            const bool earlyDodgeWindow = dist < optimal_dist * 1.15 && dist > cfg.min_dodge_distance * 0.6;
            const bool cooldownReady = (nowMs - lastDodgeAtMs) >= dodgeCooldownMs;
            const float reactChance = SanitizeFloat(cfg.react_chance, 1.0f, 0.0f, 1.0f);
            const bool reactRoll = reactChance >= 1.0f || (static_cast<float>(rand()) / RAND_MAX) < reactChance;
            const int startup = gameData.getStartupFrames(enemy_powerID);
            const int confidence = std::clamp(static_cast<int>(100.0 - std::min(dist, 400.0) / 4.0), 10, 99);

            if (dodgeArmed && cooldownReady && reactRoll && (withinDodgeWindow || earlyDodgeWindow)) {
                const bool dryRun = ui.dryRun.load();
                lastDodgeAtMs = nowMs;
                activeThreat.hasReacted = true;
                const DodgeDir dir = DefenseController::ChooseDodgeDir(enemyActor, local_x, local_y);
                if (!dryRun) {
                    WORD dirKey = 0;
                    switch (dir) {
                        case DodgeDir::Left: dirKey = VK_LEFT; break;
                        case DodgeDir::Right: dirKey = VK_RIGHT; break;
                        case DodgeDir::Up: dirKey = VK_UP; break;
                        case DodgeDir::Down: dirKey = VK_DOWN; break;
                        default: break;
                    }
                    if (dirKey) input.Down(dirKey);
                    input.Press(HotkeyManager::Parse(cfg.hotkeys.dodge.empty() ? cfg.dodge_key : cfg.hotkeys.dodge, 'Z'));
                    if (dirKey) input.Release(dirKey);
                    NotifySound("dodge");
                }
                dodgeArmed = false;
                ui.lastDodgeTriggered = true;
                ui.dodgeCount++;
                {
                    std::lock_guard<std::mutex> lock(ui.statsMutex);
                    ui.dodgeAttempts++;
                    ui.dodgeSuccesses++;
                    ui.dodgeMoveCounts[moveName]++;
                    ui.mostDodgedMove = moveName;
                    const auto reactionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - activeThreat.start_timestamp);
                    ui.totalReactionMs += reactionDuration.count();
                    ui.reactionSamples++;
                    ui.averageReactionMs = static_cast<double>(ui.totalReactionMs) / std::max(1, ui.reactionSamples);
                }
                PublishDecision({dryRun ? "WOULD DODGE" : "DODGE", moveName, "Incoming hitbox", startup, confidence});
                if (ui.calibrationMode.load()) WriteCsvRow(dist, true, false);
                lastReportedAttackId = enemy_powerID;
            } else if (attackStart || enemy_powerID != lastReportedAttackId) {
                lastReportedAttackId = enemy_powerID;
                if (ui.calibrationMode.load()) WriteCsvRow(dist, false, false);
            }
        } else if (local_found && enemy_found && (ui.offenseEnabled.load() || cfg.offense_enabled) && !ui.dryRun.load()) {
            const int forced = cfg.combo_recognition_enabled ? combo.PendingMoveId(nowMs) : -1;
            const AttackChoice choice = OffenseController::PickAttack(localActor, enemyActor, youVel, enyVel, forced);
            if (choice.move) {
                const bool towardRight = enemy_x >= local_x;
                OffenseController::Execute(input, *choice.move, towardRight);
                if (choice.move->combo_next != -1) combo.OnHitConfirmed(choice.id, nowMs);
                PublishDecision({"ATTACK", choice.move->name ? choice.move->name : "move",
                                 "Opponent in simulated hit window", choice.hitFrame,
                                 std::clamp(90 - choice.hitFrame * 3, 20, 95)});
            }
        } else {
            previousEnemyAttacking = false;
        }
        previousEnemyAttacking = enemy_attacking;
    }
};

AutododgeBot::AutododgeBot(UiState& state) : impl_(new Impl(state)) {}
AutododgeBot::~AutododgeBot() { delete impl_; }
bool AutododgeBot::init() { return impl_->init(); }
void AutododgeBot::run() { impl_->run(); }
void AutododgeBot::RestoreHook() { impl_->RestoreHook(); }
void AutododgeBot::EnterSafeMode(const std::string& reason, bool automatic) { impl_->EnterSafeMode(reason, automatic); }
void AutododgeBot::ExitSafeMode() { impl_->ExitSafeMode(); }
void AutododgeBot::ExportDiagnostics() { impl_->ExportDiagnostics(); }
