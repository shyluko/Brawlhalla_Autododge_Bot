#include "HotkeyManager.h"
#include "config.h"

#include <algorithm>
#include <cctype>

WORD HotkeyManager::Parse(const std::string& binding, WORD fallback) {
    std::string value = binding;
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return fallback;
    const auto end = value.find_last_not_of(" \t\r\n");
    value = value.substr(begin, end - begin + 1);

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    const auto plus = value.find('+');
    if (plus != std::string::npos) value = value.substr(plus + 1);

    if (value == "SPACE") return VK_SPACE;
    if (value == "ENTER") return VK_RETURN;
    if (value == "TAB") return VK_TAB;
    if (value == "ESC" || value == "ESCAPE") return VK_ESCAPE;
    if (value == "SHIFT") return VK_SHIFT;
    if (value == "CTRL") return VK_CONTROL;
    if (value == "ALT") return VK_MENU;
    if (value.rfind("F", 0) == 0 && value.size() > 1) {
        const std::string digits = value.substr(1);
        if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
            const int index = std::stoi(digits);
            if (index >= 1 && index <= 12) return static_cast<WORD>(VK_F1 + (index - 1));
        }
    }
    if (value.size() == 1 && std::isalpha(static_cast<unsigned char>(value[0])) != 0) {
        return static_cast<WORD>(value[0]);
    }
    return fallback;
}

void HotkeyManager::reloadFromConfig() {
    const auto& hk = Config::instance().hotkeys;
    bindings_ = {
        {AppAction::ToggleBot, "toggle_bot", "Toggle bot", Parse(hk.toggle_bot, VK_F1)},
        {AppAction::DryRun, "dry_run", "Dry run", Parse(hk.dry_run, VK_F2)},
        {AppAction::OffsetToggle, "offset_toggle", "Legacy offsets", Parse(hk.offset_toggle, VK_F3)},
        {AppAction::Calibration, "calibration", "Calibration", Parse(hk.calibration, VK_F4)},
        {AppAction::SafeMode, "safe_mode", "Safe Mode", Parse(hk.safe_mode, VK_F5)},
        {AppAction::Reconnect, "reconnect", "Reconnect", Parse(hk.reconnect, VK_F6)},
        {AppAction::TestDodge, "test_dodge", "Test dodge", Parse(hk.test_dodge, VK_F7)},
        {AppAction::ExportDiagnostics, "export_diagnostics", "Export diagnostics", Parse(hk.export_diagnostics, VK_F9)},
        {AppAction::Shutdown, "shutdown", "Exit", Parse(hk.shutdown, VK_F10)},
    };

    conflictReport_.clear();
    std::unordered_map<WORD, std::string> used;
    for (auto& binding : bindings_) {
        if (binding.vk == 0) continue;
        auto it = used.find(binding.vk);
        if (it != used.end()) {
            conflictReport_ += binding.name + " conflicts with " + it->second + "; keeping first binding. ";
            binding.vk = 0;
        } else {
            used[binding.vk] = binding.name;
        }
    }
}

void HotkeyManager::poll(ActionBus& bus) {
    for (const auto& binding : bindings_) {
        if (binding.vk == 0) continue;
        const bool down = (GetAsyncKeyState(binding.vk) & 0x8000) != 0;
        const bool wasDown = wasDown_[binding.vk];
        wasDown_[binding.vk] = down;
        if (down && !wasDown) {
            bus.request(binding.action);
        }
    }
}
