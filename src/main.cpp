#include "AutododgeBot.h"
#include "gui.h"
#include "logger.h"
#include "config.h"

#include <thread>
#include <windows.h>

AutododgeBot* g_botInstance = nullptr;

BOOL WINAPI ConsoleHandler(DWORD eventType) {
    if (eventType == CTRL_C_EVENT || eventType == CTRL_BREAK_EVENT ||
        eventType == CTRL_CLOSE_EVENT || eventType == CTRL_LOGOFF_EVENT ||
        eventType == CTRL_SHUTDOWN_EVENT) {
        if (g_botInstance) g_botInstance->RestoreHook();
    }
    return FALSE;
}

int main() {
    Logger::initialize();
    Config::instance().loadFromFile();
    Config::instance().sanitizeRuntime();

    UiState ui;
    AutododgeBot bot(ui);
    g_botInstance = &bot;
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    const bool initialized = bot.init();

    std::thread botThread;
    if (initialized) botThread = std::thread(&AutododgeBot::run, &bot);

    Gui gui;
    const int result = gui.run(ui);

    ui.shutdown = true;
    ui.actions.request(AppAction::Shutdown);
    if (botThread.joinable()) botThread.join();
    bot.RestoreHook();
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
    g_botInstance = nullptr;
    Logger::shutdown();
    return result;
}
