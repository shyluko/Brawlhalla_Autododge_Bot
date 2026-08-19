#pragma once

#include "gui.h"

class AutododgeBot {
public:
    explicit AutododgeBot(UiState& state);
    ~AutododgeBot();

    bool init();
    void run();
    void RestoreHook();
    void EnterSafeMode(const std::string& reason, bool automatic = true);
    void ExitSafeMode();
    void ExportDiagnostics();

private:
    struct Impl;
    Impl* impl_;
};
