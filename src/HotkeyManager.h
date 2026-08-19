#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

enum class AppAction {
    None,
    ToggleBot,
    DryRun,
    OffsetToggle,
    Calibration,
    SafeMode,
    Reconnect,
    TestDodge,
    ExportDiagnostics,
    Shutdown
};

struct HotkeyBinding {
    AppAction action = AppAction::None;
    std::string name;
    std::string label;
    WORD vk = 0;
};

class ActionBus {
public:
    void request(AppAction action) {
        pending_.store(static_cast<int>(action), std::memory_order_relaxed);
    }

    AppAction take() {
        const int value = pending_.exchange(static_cast<int>(AppAction::None), std::memory_order_relaxed);
        return static_cast<AppAction>(value);
    }

private:
    std::atomic<int> pending_{static_cast<int>(AppAction::None)};
};

class HotkeyManager {
public:
    static WORD Parse(const std::string& binding, WORD fallback);

    void reloadFromConfig();
    void poll(ActionBus& bus);
    std::string conflictReport() const { return conflictReport_; }
    std::vector<HotkeyBinding> bindings() const { return bindings_; }

private:
    std::vector<HotkeyBinding> bindings_;
    std::unordered_map<WORD, bool> wasDown_;
    std::string conflictReport_;
};
