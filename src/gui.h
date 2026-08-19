#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "HotkeyManager.h"

struct UiState {
    std::atomic<bool> shutdown{false};
    std::atomic<bool> active{false};
    std::atomic<bool> dryRun{false};
    std::atomic<bool> calibrationMode{false};
    std::atomic<bool> analysisMode{false};
    std::atomic<bool> offenseEnabled{false};
    std::atomic<bool> processFound{false};
    std::atomic<bool> hookInstalled{false};
    std::atomic<bool> safeModeActive{false};
    std::atomic<bool> validationOk{true};
    std::atomic<int> moveCount{0};
    std::atomic<int> powerIdCount{0};
    std::atomic<int> dodgeCount{0};
    std::atomic<int> entityCount{0};
    std::atomic<int> validEntityCount{0};
    std::atomic<int> attackCount{0};
    std::atomic<bool> testInput{false};
    std::atomic<bool> manualReconnect{false};
    std::atomic<bool> exportDiagnostics{false};
    std::atomic<bool> openLogsFolder{false};
    std::atomic<bool> openConfigFolder{false};
    std::atomic<bool> resetCalibration{false};
    std::atomic<bool> autoRecoverEnabled{true};
    std::atomic<bool> safeModeEnabled{true};
    std::atomic<bool> gameUpdateWarningEnabled{true};
    std::atomic<bool> processWatchdogEnabled{true};
    std::atomic<int> lastPowerId{0};
    std::atomic<double> lastDistance{0.0};
    std::atomic<double> lastThreshold{320.0};
    std::atomic<double> localX{0.0};
    std::atomic<double> localY{0.0};
    std::atomic<double> enemyX{0.0};
    std::atomic<double> enemyY{0.0};
    std::atomic<bool> lastDodgeTriggered{false};
    std::atomic<bool> visualHitboxesEnabled{false};
    std::atomic<int> lastFrame{0};
    std::atomic<int> lastConfidence{0};

    ActionBus actions;

    std::mutex textMutex;
    std::mutex statsMutex;
    std::string status = "Initializing...";
    std::string lastMove = "None";
    std::string lastEvent = "Waiting for bot initialization";
    std::string mostDodgedMove = "None";
    std::string gameVersion = "unknown";
    std::string notification = "";
    std::string validationText = "";
    std::string decisionBanner = "";
    std::string debugText = "";
    std::string hotkeyConflict = "";
    std::string safeModeReason = "";

    std::map<std::string, int> dodgeMoveCounts;
    std::vector<std::string> dodgeTimeline;

    long long dodgeAttempts = 0;
    long long dodgeSuccesses = 0;
    long long totalReactionMs = 0;
    int reactionSamples = 0;
    double averageReactionMs = 0.0;
};

class Gui {
public:
    int run(UiState& state);
};
