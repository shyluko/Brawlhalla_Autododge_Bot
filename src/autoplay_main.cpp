
#include "memory.h"
#include "attack_table.h"
#include "hitbox_table.h"
#include "frame_hitbox_table.h"
#include "combo_engine.h"
#include <windows.h>
#include <iostream>
#include <string>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <chrono>

// Windows console dashboard. Rendering is throttled independently from the
// 60 Hz decision loop so diagnostics do not affect gameplay timing.
namespace Con {
    enum Color {
        GRAY = 8, WHITE = 7, BRIGHT = 15,
        YELLOW = 14, GREEN = 10, RED = 12, CYAN = 11
    };
    constexpr int kMaxWidth = 100;
    int width = 80;
    HANDLE handle = nullptr;

    void Init() {
        handle = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTitleA("Brawlhalla Autoplay - Dashboard");
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD inMode = 0;
        if (GetConsoleMode(hIn, &inMode)) {
            inMode &= ~ENABLE_QUICK_EDIT_MODE;
            inMode |= ENABLE_EXTENDED_FLAGS;
            SetConsoleMode(hIn, inMode);
        }
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(handle, &info)) {
            int visibleCols = (int)(info.srWindow.Right - info.srWindow.Left + 1);
            width = std::max(60, std::min({ kMaxWidth, (int)info.dwSize.X, visibleCols }));
        }

        CONSOLE_CURSOR_INFO ci;
        GetConsoleCursorInfo(handle, &ci);
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(handle, &ci);
    }
    void SetColor(int c) { SetConsoleTextAttribute(handle, (WORD)c); }
    void Home() {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        SHORT top = 0;
        if (GetConsoleScreenBufferInfo(handle, &info)) top = info.srWindow.Top;
        SetConsoleCursorPosition(handle, { 0, top });
    }
    bool inPanel = false;
    void RawLine(const std::string& text, int color = WHITE) {
        SetColor(color);
        std::string padded = text;
        if ((int)padded.size() < width) padded.append(width - padded.size(), ' ');
        else padded.resize(width);
        std::cout << padded << "\n";
    }
    void ClosePanel() {
        if (!inPanel) return;
        std::string bottom = "+" + std::string(std::max(0, width - 2), '-') + "+";
        RawLine(bottom, GRAY);
        inPanel = false;
    }

    void Line(const std::string& text, int color = WHITE) {
        if (!inPanel) { RawLine(text, color); return; }
        int innerWidth = std::max(1, width - 4);
        std::string content = text;
        if ((int)content.size() > innerWidth) content.resize(innerWidth);
        else content.append(innerWidth - content.size(), ' ');
        SetColor(GRAY);
        std::cout << "| ";
        SetColor(color);
        std::cout << content;
        SetColor(GRAY);
        std::cout << " |\n";
    }
    void Section(const std::string& title) {
        ClosePanel();
        std::string bar = "+" + std::string(std::max(0, width - 2), '-') + "+";
        RawLine(bar, GRAY);
        inPanel = true;
        Line(title, BRIGHT);
        RawLine(bar, GRAY);
    }
    void ClearRest() {
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(handle, &info);
        COORD cur = info.dwCursorPosition;
        SHORT visibleBottom = info.srWindow.Bottom;
        if (cur.Y <= visibleBottom) {
            DWORD cells = (DWORD)(visibleBottom - cur.Y + 1) * info.dwSize.X;
            DWORD written;
            COORD start = { 0, cur.Y };
            FillConsoleOutputCharacterA(handle, ' ', cells, start, &written);
            FillConsoleOutputAttribute(handle, WHITE, cells, start, &written);
        }
    }
}
namespace Keys {
    constexpr WORD LEFT   = VK_LEFT;
    constexpr WORD RIGHT  = VK_RIGHT;
    constexpr WORD UP     = VK_UP;
    constexpr WORD DOWN   = VK_DOWN;
    constexpr WORD DODGE  = VK_SHIFT;
    constexpr WORD ATTACK = 'J';
    constexpr WORD HEAVY  = 'K';
    constexpr WORD JUMP   = VK_SPACE;
    constexpr WORD TOGGLE = VK_F1;
    constexpr WORD ESP_TOGGLE = VK_F2;
    constexpr WORD OFFSET_TOGGLE = VK_F3;
    constexpr WORD CALIB_RESET = VK_F4;
    constexpr WORD EXIT = VK_F10;
}

namespace Tuning {
    constexpr uint32_t REACTION_BASE_MS     = 0;
    constexpr uint32_t REACTION_VARIANCE_MS = 0;
    constexpr double   REACT_CHANCE         = 0.85;

    constexpr double DODGE_RANGE          = 320.0;
    constexpr double DODGE_PADDING_X      = 35.0;
    constexpr double DODGE_PADDING_Y      = 30.0;
    constexpr double RECOVER_Y_THRESH     = 2200.0;
    constexpr double RECOVER_SIDE_X_THRESH = 2550.0;
    constexpr double RECOVER_CENTER_DEADZONE = 80.0;
    constexpr double EDGE_SOFT_LIMIT_X    = 2050.0;
    constexpr double CHASE_DEADZONE       = 20.0;
    constexpr double HIT_CONFIRM_MS       = 150.0;
    constexpr int    TRACKING_FAIL_RELEASE_FRAMES = 5;
    constexpr int    ATTACK_SIM_MAX_FRAMES = 45;
    constexpr int    LOG_EVERY_MS = 120;
    constexpr int    ATTACK_START_TIMEOUT_MS = 180;
    constexpr int    RECOVERY_INPUT_INTERVAL_MS = 140;
    constexpr int    DASHBOARD_INTERVAL_MS = 100;
    constexpr int    CALIB_STABLE_INPUT_MS = 120;
    constexpr int    CALIB_MIN_MATCH_MS_PER_DIR = 220;
    constexpr int    CALIB_MIN_SCORE_MS = 650;
    constexpr int    CALIB_WINNER_MARGIN_MS = 220;
    constexpr int    CALIB_STALE_MS = 750;
    constexpr bool USE_EXPERIMENTAL_ENTITY_FIELDS = false;
    constexpr float MULTIHIT_STARTER_BONUS = 30.0f;
    constexpr double EDGE_GUARD_TRIGGER_X = 1200.0;
    constexpr int    EDGE_GUARD_MAX_OPTIONS = 1;
    constexpr int LEDGE_GETUP_MIN_MS = 250;
    constexpr int LEDGE_GETUP_VARIANCE_MS = 200;
}
namespace Input {
    std::unordered_set<WORD> held;

    void KeyDown(WORD vk) {
        if (held.count(vk)) return;
        held.insert(vk);
        INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = vk;
        SendInput(1, &in, sizeof(INPUT));
    }
    void KeyUp(WORD vk) {
        if (!held.count(vk)) return;
        held.erase(vk);
        INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = vk; in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    }
    void Tap(WORD vk, int holdMs = 15) {
        INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = vk;
        SendInput(1, &in, sizeof(INPUT));
        Sleep(JitteredMs(holdMs, holdMs / 4 + 2));
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    }
    void ReleaseMovement() {
        KeyUp(Keys::LEFT); KeyUp(Keys::RIGHT); KeyUp(Keys::UP); KeyUp(Keys::DOWN);
    }
    void ReleaseAll() {
        ReleaseMovement();
        KeyUp(Keys::DODGE); KeyUp(Keys::ATTACK); KeyUp(Keys::HEAVY); KeyUp(Keys::JUMP);
    }
    enum class MoveDir { None, Left, Right };
    MoveDir currentMoveDir = MoveDir::None;

    void SetMoveDirection(MoveDir dir) {
        if (dir == currentMoveDir) return;
        if (currentMoveDir == MoveDir::Left)  KeyUp(Keys::LEFT);
        if (currentMoveDir == MoveDir::Right) KeyUp(Keys::RIGHT);
        if (dir == MoveDir::Left)  KeyDown(Keys::LEFT);
        if (dir == MoveDir::Right) KeyDown(Keys::RIGHT);
        currentMoveDir = dir;
    }
}
// Facing +0x110 is confirmed. F3 switches only airborne/CanDodge between the
// current post-patch candidates and the pre-patch legacy values.
bool g_useNewAirDashOffsets = true;

// Snapshot of the PhysObject and its linked Entity.
struct Actor {
    uintptr_t dp = 0, entity_ptr = 0;
    bool      valid = false;
    double    pos_x = 0, pos_y = 0;
    uint8_t   is_attacking = 0;
    int       attack_id = 0;
    int       weapon_type_id = 0;
    int       facing = 0;
    int       airborne = 0;
    int       is_stunned = 0;
    int       is_dashing = 0;
    int       is_local_player = 0;
    double    damage = 0;
    int       legend_id = 0;
    bool      has_legend = false;
    int       facing_old = 0;
    int       facing_new = 0;
    int       airborne_old = 0;
    int       airborne_new = 0;
    int       is_dashing_old = 0;
    int       is_dashing_new = 0;
    int       is_attacking_ent = 0;
    bool      is_dead = false;
    bool      is_edging = false;
    int       remaining_options = 0;
    int       stocks = 0;
    int       knocked_back = 0;
};

Memory g_mem;
ComboEngine g_comboEngine;
uintptr_t g_installedHookTarget = 0;
volatile LONG g_hookRestored = 0;

void RestoreInstalledHook() {
    if (!g_installedHookTarget) return;
    if (InterlockedCompareExchange(&g_hookRestored, 1, 0) != 0) return;
    const std::vector<uint8_t> original = { 0xF2, 0x0F, 0x10, 0x8B, 0x40, 0x01, 0x00, 0x00 };
    g_mem.write_bytes(g_installedHookTarget, original);
}

BOOL WINAPI ConsoleCloseHandler(DWORD eventType) {
    if (eventType == CTRL_C_EVENT || eventType == CTRL_BREAK_EVENT ||
        eventType == CTRL_CLOSE_EVENT || eventType == CTRL_LOGOFF_EVENT ||
        eventType == CTRL_SHUTDOWN_EVENT) {
        RestoreInstalledHook();
    }
    return FALSE;
}
uintptr_t g_calibratedYouDp = 0;
int       g_calibMissingFrames = 0;
int       g_calibHeldSign = 0;
long long g_calibHeldSinceMs = 0;
long long g_calibLastStepMs = 0;
long long g_autoCalibStartedMs = 0;

// Manual/automatic calibration correlates controlled horizontal input with
// actor movement and requires evidence in both directions plus a winner gap.
struct CalibCandidate {
    uintptr_t dp = 0;
    double lastX = 0;
    double scoreMs = 0;
    int matchedLeftMs = 0;
    int matchedRightMs = 0;
    int samples = 0;
    long long lastSeenMs = 0;
};
std::vector<CalibCandidate> g_calibCandidates;
void ResetCalibration() {
    g_calibratedYouDp = 0;
    g_calibMissingFrames = 0;
    g_calibHeldSign = 0;
    g_calibHeldSinceMs = 0;
    g_calibLastStepMs = 0;
    g_autoCalibStartedMs = 0;
    g_calibCandidates.clear();
}

void RunCalibrationStep(const std::vector<Actor>& actors, long long nowMs,
                        bool forcedInput = false, int forcedSign = 0) {
    int heldSign = forcedSign;
    if (!forcedInput) {
        bool leftHeld  = (GetAsyncKeyState(VK_LEFT)  & 0x8000) != 0;
        bool rightHeld = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
        heldSign = (leftHeld == rightHeld) ? 0 : (rightHeld ? 1 : -1);
    }
    if (heldSign != g_calibHeldSign) {
        g_calibHeldSign = heldSign;
        g_calibHeldSinceMs = nowMs;
    }

    int dtMs = g_calibLastStepMs == 0 ? 0 : (int)std::clamp(nowMs - g_calibLastStepMs, 0LL, 50LL);
    g_calibLastStepMs = nowMs;

    for (const Actor& a : actors) {
        CalibCandidate* found = nullptr;
        for (auto& c : g_calibCandidates) if (c.dp == a.dp) { found = &c; break; }
        if (!found) {
            g_calibCandidates.push_back({ a.dp, a.pos_x, 0.0, 0, 0, 0, nowMs });
            continue;
        }
        double dx = a.pos_x - found->lastX;
        found->lastX = a.pos_x;
        found->lastSeenMs = nowMs;
        bool inputStable = heldSign != 0 && nowMs - g_calibHeldSinceMs >= Tuning::CALIB_STABLE_INPUT_MS;
        if (inputStable && dtMs > 0 && std::abs(dx) > 0.25) {
            double dxSign = dx > 0 ? 1.0 : -1.0;
            bool matched = dxSign == heldSign;
            found->scoreMs += matched ? dtMs : -dtMs;
            found->samples++;
            if (matched && heldSign < 0) found->matchedLeftMs += dtMs;
            if (matched && heldSign > 0) found->matchedRightMs += dtMs;
        }
    }

    g_calibCandidates.erase(
        std::remove_if(g_calibCandidates.begin(), g_calibCandidates.end(), [nowMs](const CalibCandidate& c) {
            return nowMs - c.lastSeenMs > Tuning::CALIB_STALE_MS;
        }),
        g_calibCandidates.end());

    CalibCandidate* best = nullptr;
    double secondScore = -1e9;
    for (auto& c : g_calibCandidates) {
        if (!best || c.scoreMs > best->scoreMs) {
            if (best) secondScore = best->scoreMs;
            best = &c;
        } else if (c.scoreMs > secondScore) {
            secondScore = c.scoreMs;
        }
    }
    if (best && best->scoreMs >= Tuning::CALIB_MIN_SCORE_MS &&
        best->matchedLeftMs >= Tuning::CALIB_MIN_MATCH_MS_PER_DIR &&
        best->matchedRightMs >= Tuning::CALIB_MIN_MATCH_MS_PER_DIR &&
        best->samples >= 20 &&
        best->scoreMs - secondScore >= Tuning::CALIB_WINNER_MARGIN_MS) {
        g_calibratedYouDp = best->dp;
        g_calibCandidates.clear();
    }
}

bool IsLikelyPointer(uintptr_t value) {
    return value >= 0x10000 && value <= 0x00007FFFFFFFFFFFULL;
}

Actor ReadActor(uintptr_t dp) {
    Actor a;
    if (!IsLikelyPointer(dp)) return a;
    a.dp = dp;
    if (!g_mem.try_read(dp + 0x140, a.pos_x) || !g_mem.try_read(dp + 0x138, a.pos_y)) return a;
    if (!std::isfinite(a.pos_x) || !std::isfinite(a.pos_y) ||
        a.pos_x < -5000 || a.pos_x > 5000 || a.pos_y < -5000 || a.pos_y > 5000) return a;

    a.is_attacking = g_mem.read<uint8_t>(dp + 0x050);
    a.attack_id    = g_mem.read<int>(dp + 0x06C);
    a.entity_ptr   = g_mem.read<uintptr_t>(dp + 0x0C0);
    uintptr_t weapon_ptr = g_mem.read<uintptr_t>(dp + 0x0C8);
    if (IsLikelyPointer(weapon_ptr)) {
        uintptr_t weapon_type_ptr = g_mem.read<uintptr_t>(weapon_ptr + 0x48);
        if (IsLikelyPointer(weapon_type_ptr)) {
            int weapon_type_id = g_mem.read<int>(weapon_type_ptr + 0xB4);
            if (weapon_type_id >= 0 && weapon_type_id <= 32) {
                a.weapon_type_id = weapon_type_id;
            }
        }
    } else {
        a.weapon_type_id = 1;
    }

    if (IsLikelyPointer(a.entity_ptr)) {
        a.facing_old      = g_mem.read<int>(a.entity_ptr + 0x108);
        a.facing_new      = g_mem.read<int>(a.entity_ptr + 0x110);
        a.airborne_old    = g_mem.read<int>(a.entity_ptr + 0x124);
        a.airborne_new    = g_mem.read<int>(a.entity_ptr + 0x12C);
        a.is_dashing_old  = g_mem.read<int>(a.entity_ptr + 0x190);
        a.is_dashing_new  = g_mem.read<int>(a.entity_ptr + 0x1A4);
        a.facing          = a.facing_new;
        a.airborne        = g_useNewAirDashOffsets ? a.airborne_new   : a.airborne_old;
        a.is_dashing      = g_useNewAirDashOffsets ? a.is_dashing_new : a.is_dashing_old;
        a.is_stunned       = g_mem.read<int>(a.entity_ptr + 0x144);
        a.is_local_player  = g_mem.read<int>(a.entity_ptr + 0x250);
        a.damage           = g_mem.read<double>(a.entity_ptr + 0x5D8);

        uintptr_t legendPtr = g_mem.read<uintptr_t>(a.entity_ptr + 0x3E8);
        if (IsLikelyPointer(legendPtr)) {
            int legendId = g_mem.read<int>(legendPtr + 0x48);
            if (legendId > 0 && legendId < 1000) {
                a.legend_id = legendId;
                a.has_legend = true;
            }
        }
        a.is_attacking_ent  = g_mem.read<int>(a.entity_ptr + 0xE4);
        a.is_dead           = g_mem.read<int>(a.entity_ptr + 0x25C) != 0;
        a.is_edging         = g_mem.read<int>(a.entity_ptr + 0x154) != 0;
        a.remaining_options = g_mem.read<int>(a.entity_ptr + 0x220);
        a.stocks            = g_mem.read<int>(a.entity_ptr + 0x2A4);
        a.knocked_back      = g_mem.read<int>(a.entity_ptr + 0x084);
    } else {
        a.entity_ptr = 0;
        return a;
    }
    a.valid = true;
    return a;
}

bool IsPlausibleFighter(const Actor& a) {
    if (!a.valid || !a.entity_ptr) return false;
    if (!std::isfinite(a.damage) || a.damage < 0.0 || a.damage > 1000.0) return false;
    if (a.facing_new != 0 && a.facing_new != 1) return false;
    if (a.attack_id < 0 || a.attack_id > 100000) return false;
    return true;
}

struct VelocityTracker {
    double lastX = 0, lastY = 0;
    long long lastTimeMs = 0;
    bool hasLast = false;
    double velX = 0, velY = 0;

    void Update(double x, double y, long long nowMs) {
        if (hasLast) {
            double dt = (nowMs - lastTimeMs) / 1000.0;
            if (dt > 0.001) { velX = (x - lastX) / dt; velY = (y - lastY) / dt; }
        }
        lastX = x; lastY = y; lastTimeMs = nowMs; hasLast = true;
    }

    void Reset() {
        lastX = lastY = velX = velY = 0;
        lastTimeMs = 0;
        hasLast = false;
    }
};

struct ReactionTimer {
    bool pending = false;
    long long triggerAt = 0;
    bool rolledChance = false;
    bool chancePassed = false;
    bool ShouldReact(long long nowMs, bool threatPresent, double reactChance) {
        if (!threatPresent) { pending = false; rolledChance = false; return false; }
        if (!rolledChance) {
            rolledChance = true;
            chancePassed = (double)rand() / RAND_MAX < reactChance;
        }
        if (!chancePassed) return false;
        if (!pending) {
            pending = true;
            triggerAt = nowMs + Tuning::REACTION_BASE_MS + (rand() % (Tuning::REACTION_VARIANCE_MS + 1));
            return false;
        }
        return nowMs >= triggerAt;
    }
    void Reset() { pending = false; rolledChance = false; }
};
struct StrategyProfile {
    double reactChance;
    double edgeSoftLimitX;
};

StrategyProfile ComputeStrategy(const Actor& you, const Actor& eny) {
    StrategyProfile s;
    if (!Tuning::USE_EXPERIMENTAL_ENTITY_FIELDS) {
        s.edgeSoftLimitX = Tuning::EDGE_SOFT_LIMIT_X;
        s.reactChance = Tuning::REACT_CHANCE;
        return s;
    }
    int diff = you.stocks - eny.stocks;
    diff = std::max(-3, std::min(3, diff));
    s.edgeSoftLimitX = Tuning::EDGE_SOFT_LIMIT_X - diff * 80.0;
    s.reactChance = std::min(1.0, Tuning::REACT_CHANCE + (diff < 0 ? 0.05 * -diff : 0.0));
    return s;
}

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string BuildLogPath(const char* fileName = "autoplay_decisions.log") {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string full = path;
    size_t slash = full.find_last_of("\\/");
    if (slash == std::string::npos) return fileName;
    return full.substr(0, slash + 1) + fileName;
}
struct AttackChoice {
    const AttackData* move = nullptr;
    int id = 0;
    int hitFrame = 0;
    float score = 0;
    float aimDx = 0;
    float aimDy = 0;
};

struct HitSimResult {
    bool hits = false;
    int frame = 0;
    float centerDist = 0;
    float dx = 0;
    float dy = 0;
};

enum class DodgeDir { None, Left, Right, Up, Down };
void RelativePos(const Actor& you, const Actor& eny, float& dx, float& dy) {
    bool youFacingRight = (you.facing == 0);
    dx = (float)((eny.pos_x - you.pos_x) * (youFacingRight ? 1.0 : -1.0));
    dy = (float)(eny.pos_y - you.pos_y);
}

bool MoveInRange(const Actor& you, const Actor& eny, const AttackData& move) {
    if (move.x1 == move.x2 && move.y1 == move.y2) return false;
    if (move.is_air != (you.airborne != 0)) return false;
    float dx, dy;
    RelativePos(you, eny, dx, dy);
    if (dx < move.x1 || dx > move.x2) return false;
    if (dy < move.y1 || dy > move.y2) return false;
    return true;
}

// Predicts relative positions at each active hitbox frame.
HitSimResult SimulateMoveHit(int powerId, const Actor& you, const Actor& eny,
                             const VelocityTracker& youVel, const VelocityTracker& enyVel,
                             const AttackData& move) {
    HitSimResult result;
    if (move.is_air != (you.airborne != 0)) return result;

    bool youFacingRight = (you.facing == 0);
    double facingSign = youFacingRight ? 1.0 : -1.0;
    double baseDx = (eny.pos_x - you.pos_x) * facingSign;
    double baseDy = eny.pos_y - you.pos_y;
    double relVelX = (enyVel.velX - youVel.velX) * facingSign;
    double relVelY = enyVel.velY - youVel.velY;

    const auto& frames = get_frame_hitbox_table();
    auto it = frames.find(powerId);
    if (it == frames.end()) {
        if (!MoveInRange(you, eny, move)) return result;
        result.hits = true;
        result.frame = move.startup_frames;
        result.dx = (float)baseDx;
        result.dy = (float)baseDy;
        float centerX = (move.x1 + move.x2) * 0.5f;
        float centerY = (move.y1 + move.y2) * 0.5f;
        result.centerDist = std::hypot(result.dx - centerX, result.dy - centerY);
        return result;
    }

    int bestFrame = 9999;
    float bestCenterDist = 1e9f;
    float bestDx = 0;
    float bestDy = 0;

    for (const FrameBox& box : it->second.boxes) {
        if (box.frame > Tuning::ATTACK_SIM_MAX_FRAMES) continue;
        double t = box.frame / 60.0;
        float dx = (float)(baseDx + relVelX * t);
        float dy = (float)(baseDy + relVelY * t);
        if (dx < box.x1 || dx > box.x2 || dy < box.y1 || dy > box.y2) continue;

        float centerX = (box.x1 + box.x2) * 0.5f;
        float centerY = (box.y1 + box.y2) * 0.5f;
        float centerDist = std::hypot(dx - centerX, dy - centerY);
        if (box.frame < bestFrame || (box.frame == bestFrame && centerDist < bestCenterDist)) {
            bestFrame = box.frame;
            bestCenterDist = centerDist;
            bestDx = dx;
            bestDy = dy;
        }
    }

    if (bestFrame != 9999) {
        result.hits = true;
        result.frame = bestFrame;
        result.centerDist = bestCenterDist;
        result.dx = bestDx;
        result.dy = bestDy;
    }
    return result;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

const char* WeaponPrefix(int weaponTypeId) {
    switch (weaponTypeId) {
        case 1:  return "Base";
        case 3:  return "Sword";
        case 4:  return "Hammer";
        case 5:  return "Lance";
        case 6:  return "Pistol";
        case 7:  return "Spear";
        case 8:  return "Katar";
        case 9:  return "Axe";
        case 10: return "Bow";
        case 11: return "Fists";
        case 12: return "Scythe";
        case 13: return "Cannon";
        case 14: return "Orb";
        case 16: return "Greatsword";
        case 18: return "Boots";
        case 19: return "Chakram";
        default: return nullptr;
    }
}

bool MoveMatchesWeapon(const AttackData& move, int weaponTypeId) {
    const char* prefix = WeaponPrefix(weaponTypeId);
    if (!prefix) return false;

    std::string name = move.name ? move.name : "";
    std::string p = prefix;
    return name.rfind(p, 0) == 0;
}

bool IsInternalAttackPhase(int powerId) {
    static const std::unordered_set<int> internalIds = [] {
        std::unordered_set<int> ids;
        for (const auto& [id, move] : get_attack_table()) {
            (void)id;
            if (move.combo_next != -1) ids.insert(move.combo_next);
        }
        return ids;
    }();
    return internalIds.count(powerId) != 0;
}

void RelativeToAttackerFacingRight(const Actor& attacker, const Actor& target, float& dx, float& dy) {
    bool attackerFacingRight = (attacker.facing == 0);
    dx = (float)((target.pos_x - attacker.pos_x) * (attackerFacingRight ? 1.0 : -1.0));
    dy = (float)(target.pos_y - attacker.pos_y);
}

bool EnemyAttackThreatensYou(const Actor& you, const Actor& eny, double dist) {
    if (eny.is_attacking == 0) return false;

    const auto& hitboxes = get_hitbox_table();
    auto it = hitboxes.find(eny.attack_id);
    if (it == hitboxes.end()) {
        return dist < Tuning::DODGE_RANGE;
    }

    const AttackBox& box = it->second;
    if (box.x1 == box.x2 && box.y1 == box.y2) {
        return dist < Tuning::DODGE_RANGE;
    }

    float dx, dy;
    RelativeToAttackerFacingRight(eny, you, dx, dy);
    return dx >= box.x1 - Tuning::DODGE_PADDING_X &&
           dx <= box.x2 + Tuning::DODGE_PADDING_X &&
           dy >= box.y1 - Tuning::DODGE_PADDING_Y &&
           dy <= box.y2 + Tuning::DODGE_PADDING_Y;
}

DodgeDir ChooseDodgeDirOutOfThreat(const Actor& you, const Actor& eny) {
    const auto& hitboxes = get_hitbox_table();
    auto it = hitboxes.find(eny.attack_id);
    if (it == hitboxes.end() || (it->second.x1 == it->second.x2 && it->second.y1 == it->second.y2)) {
        return (you.pos_x > eny.pos_x) ? DodgeDir::Right : DodgeDir::Left;
    }

    const AttackBox& box = it->second;
    float dx, dy;
    RelativeToAttackerFacingRight(eny, you, dx, dy);

    float leftEdge   = box.x1 - (float)Tuning::DODGE_PADDING_X;
    float rightEdge  = box.x2 + (float)Tuning::DODGE_PADDING_X;
    float topEdge    = box.y1 - (float)Tuning::DODGE_PADDING_Y;
    float bottomEdge = box.y2 + (float)Tuning::DODGE_PADDING_Y;

    float exitLeft = std::abs(dx - leftEdge);
    float exitRight = std::abs(rightEdge - dx);
    float exitUp = std::abs(dy - topEdge);
    float exitDown = std::abs(bottomEdge - dy);

    float best = exitLeft;
    DodgeDir dir = (eny.facing == 0) ? DodgeDir::Left : DodgeDir::Right;
    if (exitRight < best) {
        best = exitRight;
        dir = (eny.facing == 0) ? DodgeDir::Right : DodgeDir::Left;
    }
    if (exitUp < best) {
        best = exitUp;
        dir = DodgeDir::Up;
    }
    if (exitDown < best) {
        dir = DodgeDir::Down;
    }
    return dir;
}

void ExecuteDodge(DodgeDir dir) {
    WORD vk = 0;
    switch (dir) {
        case DodgeDir::Left:  vk = Keys::LEFT; break;
        case DodgeDir::Right: vk = Keys::RIGHT; break;
        case DodgeDir::Up:    vk = Keys::UP; break;
        case DodgeDir::Down:  vk = Keys::DOWN; break;
        default: break;
    }
    if (vk) Input::KeyDown(vk);
    Input::Tap(Keys::DODGE);
    if (vk) Input::KeyUp(vk);
}

AttackChoice PickAttack(const Actor& you, const Actor& eny,
                        const VelocityTracker& youVel, const VelocityTracker& enyVel,
                        int forcedMoveId = -1) {
    AttackChoice best;
    if (forcedMoveId != -1) {
        const auto& table = get_attack_table();
        auto it = table.find(forcedMoveId);
        if (it != table.end() && MoveMatchesWeapon(it->second, you.weapon_type_id)) {
            HitSimResult sim = SimulateMoveHit(forcedMoveId, you, eny, youVel, enyVel, it->second);
            if (sim.hits) {
                best.move = &it->second;
                best.id = forcedMoveId;
                best.hitFrame = sim.frame;
                best.score = 0.0f;
                best.aimDx = sim.dx;
                best.aimDy = sim.dy;
                return best;
            }
        }
    }
    float bestScore = 1e9f;
    for (auto& [id, move] : get_attack_table()) {
        if (!MoveMatchesWeapon(move, you.weapon_type_id)) continue;
        if (IsInternalAttackPhase(id)) continue;
        HitSimResult sim = SimulateMoveHit(id, you, eny, youVel, enyVel, move);
        if (!sim.hits) continue;
        float score = (float)sim.frame * 6.0f +
                      (float)move.recovery_frames * 0.8f +
                      sim.centerDist * 0.12f;
        if (move.combo_next != -1) score -= Tuning::MULTIHIT_STARTER_BONUS;

        if (score < bestScore) {
            bestScore = score;
            best.move = &move;
            best.id = id;
            best.hitFrame = sim.frame;
            best.score = score;
            best.aimDx = sim.dx;
            best.aimDy = sim.dy;
        }
    }
    return best;
}
void ExecuteAttack(const AttackData& move, bool towardRight) {
    std::string name = move.name;
    bool isGroundPound = name.find("GroundPound") != std::string::npos;
    bool isHeavy = name.find("Heavy") != std::string::npos || isGroundPound;
    bool isSide  = name.find("Side")  != std::string::npos;
    bool isDown  = name.find("Down")  != std::string::npos || isGroundPound;

    if (isSide) Input::KeyDown(towardRight ? Keys::RIGHT : Keys::LEFT);
    else if (isDown) Input::KeyDown(Keys::DOWN);

    Input::Tap(isHeavy ? Keys::HEAVY : Keys::ATTACK);

    if (isSide) Input::KeyUp(towardRight ? Keys::RIGHT : Keys::LEFT);
    else if (isDown) Input::KeyUp(Keys::DOWN);
}
// Prevents input spam while waiting for the game to expose attack start/end.
struct AttackTracker {
    bool   active = false;
    bool   observedAttack = false;
    bool   waitingConfirm = false;
    int    moveId = -1;
    double enyDamageAtStart = 0;
    long long issuedAtMs = 0;
    long long finishedAtMs = 0;

    void Start(int id, double enyDamageNow, long long nowMs) {
        active = true;
        observedAttack = false;
        waitingConfirm = false;
        moveId = id;
        enyDamageAtStart = enyDamageNow;
        issuedAtMs = nowMs;
        finishedAtMs = 0;
    }
    void ObserveAttack() { observedAttack = true; }
    void Finish(long long nowMs) {
        active = false;
        observedAttack = false;
        waitingConfirm = true;
        finishedAtMs = nowMs;
    }
    void Clear() {
        active = false;
        observedAttack = false;
        waitingConfirm = false;
        moveId = -1;
        enyDamageAtStart = 0;
        issuedAtMs = 0;
        finishedAtMs = 0;
    }
};
namespace Esp {
    constexpr uintptr_t BASE_ZOOM_X = 0x01218FE0;
    constexpr uintptr_t BASE_ZOOM_Y = 0x01218FE0;
    constexpr uintptr_t BASE_POS_X  = 0x01218FE0;
    constexpr uintptr_t BASE_POS_Y  = 0x01219038;

    constexpr uintptr_t CHAIN_ZOOM_X[] = { 0x840, 0x4A0, 0x18, 0x230, 0x690, 0x30, 0x20 };
    constexpr uintptr_t CHAIN_ZOOM_Y[] = { 0x830, 0x200, 0x10, 0x110, 0x28, 0x30, 0x38 };
    constexpr uintptr_t CHAIN_POS_X[]  = { 0x800, 0xC00, 0x10, 0x28, 0x2C0, 0xE0, 0x40 };
    constexpr uintptr_t CHAIN_POS_Y[]  = { 0x288, 0x3C8, 0x118, 0x100, 0x690, 0x30, 0x48 };

    struct Camera {
        double cx = 0;
        double cy = 0;
        double zx = 0.4025;
        double zy = 0.4025;
        bool ok = false;
    };

    struct DrawActor {
        Actor actor;
        POINT screen{};
        bool on_screen = false;
        bool isYou = false;
    };

    HWND overlay = nullptr;
    HWND game = nullptr;
    bool enabled = true;
    bool f8WasDown = false;
    std::vector<DrawActor> drawActors;
    Camera lastCamera;
    Camera lastGoodCamera;
    uintptr_t airModuleBase = 0;
    RECT lastClient{};

    bool LooksPtr(uintptr_t p) {
        return p > 0x10000 && p < 0x7FFFFFFFFFFF;
    }

    template <size_t N>
    uintptr_t ResolveChainRootPtr(uintptr_t root, const uintptr_t (&offsets)[N]) {
        uintptr_t addr = g_mem.read<uintptr_t>(root);
        if (!LooksPtr(addr)) return 0;
        for (size_t i = 0; i + 1 < N; ++i) {
            addr = g_mem.read<uintptr_t>(addr + offsets[i]);
            if (!LooksPtr(addr)) return 0;
        }
        return addr + offsets[N - 1];
    }

    Camera ReadCamera() {
        Camera cam;
        if (!airModuleBase) airModuleBase = g_mem.GetModuleBaseAddress("Adobe AIR.dll");
        if (!airModuleBase) return cam;

        uintptr_t zxAddr = ResolveChainRootPtr(airModuleBase + BASE_ZOOM_X, CHAIN_ZOOM_X);
        uintptr_t zyAddr = ResolveChainRootPtr(airModuleBase + BASE_ZOOM_Y, CHAIN_ZOOM_Y);
        uintptr_t cxAddr = ResolveChainRootPtr(airModuleBase + BASE_POS_X, CHAIN_POS_X);
        uintptr_t cyAddr = ResolveChainRootPtr(airModuleBase + BASE_POS_Y, CHAIN_POS_Y);
        if (!zxAddr || !zyAddr || !cxAddr || !cyAddr) return cam;

        double zx = g_mem.read<double>(zxAddr);
        double zy = g_mem.read<double>(zyAddr);
        double cx = g_mem.read<double>(cxAddr);
        double cy = g_mem.read<double>(cyAddr);
        if (!std::isfinite(zx) || !std::isfinite(zy) || !std::isfinite(cx) || !std::isfinite(cy)) return cam;
        if (std::fabs(zx) < 0.05 || std::fabs(zx) > 5.0 ||
            std::fabs(zy) < 0.05 || std::fabs(zy) > 5.0 ||
            std::fabs(cx) > 100000.0 || std::fabs(cy) > 100000.0) return cam;

        cam.zx = std::fabs(zx);
        cam.zy = std::fabs(zy);
        cam.cx = cx;
        cam.cy = cy;
        cam.ok = true;
        return cam;
    }

    bool WorldToScreen(const Camera& cam, const Actor& a, int w, int h, POINT& out) {
        double sx = cam.cx + a.pos_x * cam.zx;
        double sy = cam.cy + a.pos_y * cam.zy;
        out.x = (LONG)std::lround(sx);
        out.y = (LONG)std::lround(sy);
        return sx > -250 && sy > -250 && sx < w + 250 && sy < h + 250;
    }

    void Text(HDC dc, int x, int y, COLORREF color, const char* text) {
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        TextOutA(dc, x, y, text, (int)strlen(text));
    }

    COLORREF Color(const DrawActor& da) {
        if (da.isYou) return RGB(70, 220, 120);
        if (da.actor.is_stunned) return RGB(255, 210, 80);
        return RGB(255, 80, 80);
    }

    void Paint(HWND hwnd) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        if (!enabled) {
            EndPaint(hwnd, &ps);
            return;
        }

        HFONT font = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, "Consolas");
        HGDIOBJ oldFont = SelectObject(dc, font);

        char header[192];
        snprintf(header, sizeof(header), "Autoplay ESP | actors=%zu | camera=%s | F2 ESP | F1 BOT",
                 drawActors.size(), lastCamera.ok ? "ok" : "fallback");
        Text(dc, 12, 10, RGB(220, 235, 255), header);

        for (const DrawActor& da : drawActors) {
            const Actor& a = da.actor;
            COLORREF color = Color(da);
            HPEN pen = CreatePen(PS_SOLID, 2, color);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

            int x = da.screen.x;
            int y = da.screen.y;
            int boxW = 46;
            int boxH = 82;
            Rectangle(dc, x - boxW / 2, y - boxH, x + boxW / 2, y + 8);
            MoveToEx(dc, x - 8, y, nullptr);
            LineTo(dc, x + 8, y);
            MoveToEx(dc, x, y - 8, nullptr);
            LineTo(dc, x, y + 8);

            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);

            char l1[160];
            snprintf(l1, sizeof(l1), "%s dmg %.1f", da.isYou ? "YOU" : "ENY", a.damage);
            Text(dc, x + 28, y - 80, color, l1);
            char l2[160];
            snprintf(l2, sizeof(l2), "wid %d atk %d", a.weapon_type_id, a.attack_id);
            Text(dc, x + 28, y - 62, RGB(230, 230, 230), l2);
        }

        SelectObject(dc, oldFont);
        DeleteObject(font);
        EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_PAINT) {
            Paint(hwnd);
            return 0;
        }
        return DefWindowProcA(hwnd, msg, wp, lp);
    }

    bool Create(HWND gameHwnd) {
        game = gameHwnd;
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "BrawlhallaAutoplayEspOverlay";
        RegisterClassExA(&wc);

        overlay = CreateWindowExA(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
                                  wc.lpszClassName, "Autoplay ESP", WS_POPUP,
                                  0, 0, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
        if (!overlay) return false;
        SetLayeredWindowAttributes(overlay, RGB(0, 0, 0), 0, LWA_COLORKEY);
        ShowWindow(overlay, SW_SHOW);
        return true;
    }

    bool UpdateBounds(RECT& clientOut) {
        if (!IsWindow(game) || !IsWindow(overlay)) return false;
        RECT client{};
        GetClientRect(game, &client);
        POINT tl{ client.left, client.top };
        POINT br{ client.right, client.bottom };
        ClientToScreen(game, &tl);
        ClientToScreen(game, &br);
        int w = br.x - tl.x;
        int h = br.y - tl.y;
        if (w <= 0 || h <= 0) return false;
        clientOut = { 0, 0, w, h };
        SetWindowPos(overlay, HWND_TOPMOST, tl.x, tl.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return true;
    }

    void PumpMessages() {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    void Update(const Actor* you, const Actor* eny) {
        bool f8Down = (GetAsyncKeyState(Keys::ESP_TOGGLE) & 0x8000) != 0;
        if (f8Down && !f8WasDown) {
            enabled = !enabled;
            std::cout << (enabled ? "\n>>> ESP ENABLED <<<\n" : "\n>>> ESP DISABLED <<<\n");
        }
        f8WasDown = f8Down;

        PumpMessages();
        if (!overlay) return;

        RECT client{};
        UpdateBounds(client);
        int w = std::max(1L, client.right - client.left);
        int h = std::max(1L, client.bottom - client.top);

        lastClient = client;
        Camera currentCamera = ReadCamera();
        if (currentCamera.ok) {
            lastGoodCamera = currentCamera;
            lastCamera = currentCamera;
        } else if (lastGoodCamera.ok) {
            lastCamera = lastGoodCamera;
            lastCamera.ok = false;
        } else {
            lastCamera = Camera{};
            lastCamera.cx = w * 0.5;
            lastCamera.cy = h * 0.5;
        }

        drawActors.clear();
        struct Tagged { const Actor* a; bool isYou; };
        Tagged list[] = { { you, true }, { eny, false } };
        for (const Tagged& t : list) {
            if (!t.a || !t.a->valid) continue;
            DrawActor da;
            da.actor = *t.a;
            da.isYou = t.isYou;
            da.on_screen = WorldToScreen(lastCamera, *t.a, w, h, da.screen);
            drawActors.push_back(da);
        }

        InvalidateRect(overlay, nullptr, FALSE);
        UpdateWindow(overlay);
    }
}
int main() {
    Con::Init();
    srand((unsigned)GetTickCount64());
    Con::RawLine("BRAWLHALLA AUTOPLAY - STARTING", Con::BRIGHT);
    Con::RawLine("F1 bot | F2 ESP | F3 air/dash | F4 recalibrate | F10 safe exit", Con::GRAY);
    Con::RawLine("Finding the process and installing the hook...", Con::YELLOW);
    std::cout << std::flush;

    HWND hwnd_out;
    if (!g_mem.init("Brawlhalla", hwnd_out)) {
        std::cout << "error: Brawlhalla is not running\n";
        system("pause");
        return 1;
    }

    const char* pattern = "\xF2\x0F\x10\x8B\x40\x01\x00\x00";
    const char* mask    = "xxxxxxxx";
    uintptr_t target_addr = g_mem.find_pattern_safe(pattern, mask);
    if (!target_addr) {
        std::cout << "error: pattern not found. Restart the game if an older build was closed without cleanup.\n";
        system("pause");
        return 1;
    }

    uintptr_t cave_addr     = g_mem.allocate_memory_near(target_addr, 0x1000);
    if (!cave_addr) {
        std::cout << "error: failed to allocate memory near the hook\n";
        system("pause");
        return 1;
    }
    uintptr_t pointer_array = cave_addr + 0x100;
    uintptr_t index_store   = cave_addr + 0x80;
    uintptr_t return_addr   = target_addr + 8;

    std::vector<uint8_t> shellcode = {
        0x50, 0x51, 0x52,
        0x48, 0xB8, 0,0,0,0,0,0,0,0,
        0x8B, 0x08, 0x83, 0xF9, 0x32, 0x7C, 0x02, 0x31, 0xC9,
        0x48, 0xBA, 0,0,0,0,0,0,0,0,
        0x48, 0x89, 0x1C, 0xCA, 0xFF, 0xC1, 0x89, 0x08,
        0x5A, 0x59, 0x58,
        0xF2, 0x0F, 0x10, 0x8B, 0x40, 0x01, 0x00, 0x00,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0,0,0,0,0,0,0,0
    };
    memcpy(&shellcode[5],  &index_store,   8);
    memcpy(&shellcode[24], &pointer_array, 8);
    memcpy(&shellcode[57], &return_addr,   8);
    if (!g_mem.write_bytes(cave_addr, shellcode)) {
        std::cout << "error: failed to write shellcode to the allocated memory\n";
        system("pause");
        return 1;
    }

    std::vector<uint8_t> patch = { 0xE9, 0,0,0,0, 0x90, 0x90, 0x90 };
    DWORD relative_jmp = (DWORD)(cave_addr - target_addr - 5);
    memcpy(&patch[1], &relative_jmp, 4);
    if (!g_mem.write_bytes(target_addr, patch)) {
        std::cout << "error: failed to install the hook patch\n";
        system("pause");
        return 1;
    }
    g_installedHookTarget = target_addr;
    SetConsoleCtrlHandler(ConsoleCloseHandler, TRUE);

    std::cout << "Hook installed.\n\n";
    if (Esp::Create(hwnd_out)) {
        std::cout << "Integrated ESP created.\n";
    } else {
        std::cout << "warning: failed to create the ESP overlay; autoplay will continue without it.\n";
    }

    VelocityTracker youVel, enyVel;
    ReactionTimer dodgeTimer;
    AttackTracker attackTracker;
    bool botOn = false;
    bool f9WasDown = false;
    uint8_t lastEnyAttacking = 0;
    uint8_t lastYouAttacking = 0;
    bool lastYouEdging = false;
    long long ledgeGetUpAt = 0;
    bool lastYouDead = false;
    bool lastEnyDead = false;
    long long lastRecoveryInputAt = 0;
    int trackingFailFrames = 0;
    std::string logPath = BuildLogPath();
    std::string diagnosticPath = BuildLogPath("autoplay_diagnostics.log");
    std::ofstream decisionLog(logPath, std::ios::out | std::ios::trunc);
    if (decisionLog) {
        decisionLog << "ms,decision,you_x,you_y,eny_x,eny_y,you_vx,you_vy,eny_vx,eny_vy,dist,"
                    << "you_wid,eny_attack_id,you_attack,eny_attack,threat,you_dash,you_stun,"
                    << "move_id,move_name,hit_frame,score,you_dmg,eny_dmg,combo_active,combo_waiting,"
                    << "you_stocks,eny_stocks,you_dead,eny_dead,you_edging,eny_edging,"
                    << "you_attack_ent,eny_attack_ent,you_remaining_opts,you_knockedback,eny_knockedback,"
                    << "react_chance,edge_soft_limit\n";
    }
    std::ofstream diagnosticLog(diagnosticPath, std::ios::out | std::ios::trunc);
    if (diagnosticLog) {
        diagnosticLog << "ms,decision,bot_on,dps,actors,fighters,filter,local9,have_you,have_eny,"
                      << "calibrated_dp,auto_calib,air_dash_mode\n";
        diagnosticLog.flush();
    }
    long long lastLogAt = 0;
    long long lastDiagnosticAt = 0;
    long long lastDashboardAt = 0;
    std::string lastLoggedDecision;

    auto resetBotState = [&]() {
        Input::ReleaseAll();
        Input::currentMoveDir = Input::MoveDir::None;
        dodgeTimer.Reset();
        attackTracker.Clear();
        g_comboEngine.Clear();
        lastEnyAttacking = 0;
        lastYouAttacking = 0;
        lastYouEdging = false;
        ledgeGetUpAt = 0;
        lastYouDead = false;
        lastEnyDead = false;
        lastRecoveryInputAt = 0;
        youVel.Reset();
        enyVel.Reset();
    };

    bool f7WasDown = false;
    bool f4WasDown = false;

    bool exitRequested = false;
    while (!exitRequested) {
        if (GetAsyncKeyState(Keys::EXIT) & 1) {
            exitRequested = true;
            break;
        }
        bool f9Down = (GetAsyncKeyState(Keys::TOGGLE) & 0x8000) != 0;
        if (f9Down && !f9WasDown) {
            botOn = !botOn;
            if (!botOn) resetBotState();
            std::cout << (botOn ? "\n>>> BOT ENABLED <<<\n" : "\n>>> BOT DISABLED <<<\n");
        }
        f9WasDown = f9Down;

        bool f7Down = (GetAsyncKeyState(Keys::OFFSET_TOGGLE) & 0x8000) != 0;
        if (f7Down && !f7WasDown) {
            g_useNewAirDashOffsets = !g_useNewAirDashOffsets;
            resetBotState();
            std::cout << (g_useNewAirDashOffsets
                ? "\n>>> AIR/DASH: using CURRENT post-patch offsets <<<\n"
                : "\n>>> AIR/DASH: using LEGACY pre-patch offsets (diagnostic mode) <<<\n");
        }
        f7WasDown = f7Down;

        bool f4Down = (GetAsyncKeyState(Keys::CALIB_RESET) & 0x8000) != 0;
        if (f4Down && !f4WasDown) {
            resetBotState();
            ResetCalibration();
            std::cout << "\n>>> IDENTIFICATION RESET - move left and right to calibrate <<<\n";
        }
        f4WasDown = f4Down;

        long long now = NowMs();

        std::vector<uintptr_t> dps;
        for (int i = 0; i < 50; i++) {
            uintptr_t dp = g_mem.read<uintptr_t>(pointer_array + i * 8);
            if (!dp) continue;
            bool dup = false;
            for (auto d : dps) if (d == dp) { dup = true; break; }
            if (!dup) dps.push_back(dp);
        }

        Actor you, eny;
        bool haveYou = false, haveEny = false;
        std::vector<Actor> actors;
        for (auto dp : dps) {
            Actor a = ReadActor(dp);
            if (!a.valid) continue;
            if (!a.entity_ptr) continue;
            actors.push_back(a);
        }
        std::vector<Actor> fighterActors;
        for (const Actor& a : actors) {
            if (IsPlausibleFighter(a) && a.has_legend) fighterActors.push_back(a);
        }
        bool fighterFilterStrong = fighterActors.size() >= 2;
        if (!fighterFilterStrong) {
            fighterActors.clear();
            for (const Actor& a : actors) {
                if (IsPlausibleFighter(a)) fighterActors.push_back(a);
            }
        }
        int localMatches = 0;
        Actor localActor;
        for (const Actor& a : fighterActors) {
            if (a.is_local_player == 9) {
                localActor = a;
                localMatches++;
            }
        }
        std::string identificationMode = "nenhum";
        if (localMatches == 1) {
            you = localActor;
            haveYou = true;
            identificationMode = "unique online marker";
        } else if (g_calibratedYouDp != 0) {
            for (const Actor& a : fighterActors) {
                if (a.dp == g_calibratedYouDp) { you = a; haveYou = true; break; }
            }
            if (haveYou) {
                g_calibMissingFrames = 0;
                identificationMode = "movement calibration";
            }
            else if (++g_calibMissingFrames > 90) {
                ResetCalibration();
            }
        }
        bool calibrating = !haveYou && g_calibratedYouDp == 0 && fighterActors.size() >= 2;
        bool autoCalibrating = false;
        int autoCalibSign = 0;
        if (calibrating) {
            if (botOn) {
                autoCalibrating = true;
                if (!g_autoCalibStartedMs) g_autoCalibStartedMs = now;
                int phaseMs = (int)((now - g_autoCalibStartedMs) % 1450);
                if (phaseMs >= 150 && phaseMs < 650) autoCalibSign = -1;
                else if (phaseMs >= 800 && phaseMs < 1300) autoCalibSign = 1;

                Input::SetMoveDirection(autoCalibSign < 0 ? Input::MoveDir::Left :
                                        autoCalibSign > 0 ? Input::MoveDir::Right :
                                                            Input::MoveDir::None);
                RunCalibrationStep(fighterActors, now, true, autoCalibSign);
            } else {
                if (!Input::held.empty()) {
                    Input::ReleaseAll();
                    Input::currentMoveDir = Input::MoveDir::None;
                } else {
                    RunCalibrationStep(fighterActors, now);
                }
            }
            if (g_calibratedYouDp != 0) {
                Input::SetMoveDirection(Input::MoveDir::None);
            }
        }

        if (haveYou) {
            double bestDistSq = 1e18;
            for (const Actor& a : fighterActors) {
                if (a.dp == you.dp) continue;
                double dx = a.pos_x - you.pos_x;
                double dy = a.pos_y - you.pos_y;
                double distSq = dx * dx + dy * dy;
                if (distSq < bestDistSq) {
                    bestDistSq = distSq;
                    eny = a;
                    haveEny = true;
                }
            }
        }

        Esp::Update(haveYou ? &you : nullptr, haveEny ? &eny : nullptr);

        bool trackingOk = haveYou && haveEny;

        double dist = 0;
        bool enemyThreatensYou = false;
        std::string decision = "waiting";
        bool actedThisFrame = false;
        int debugMoveId = -1;
        std::string debugMoveName = "-";
        int debugHitFrame = -1;
        float debugScore = 0.0f;
        StrategyProfile strategy{ Tuning::REACT_CHANCE, Tuning::EDGE_SOFT_LIMIT_X };
        if (!botOn) {
            decision = "bot DISABLED (press F1)";
            youVel.Reset();
            enyVel.Reset();
        } else if (!trackingOk) {
            if (++trackingFailFrames == Tuning::TRACKING_FAIL_RELEASE_FRAMES) resetBotState();
            youVel.Reset();
            enyVel.Reset();
            if (autoCalibrating) {
                decision = "AUTO CALIBRATION - running the left/right probe";
            } else if (calibrating) {
                decision = "CALIBRATING - move LEFT, then RIGHT";
            } else if (!haveYou) {
                decision = "NO LOCAL PLAYER - waiting for identification";
            } else {
                decision = "NO OPPONENT - only one live fighter was found";
            }
        } else {
        trackingFailFrames = 0;

        youVel.Update(you.pos_x, you.pos_y, now);
        enyVel.Update(eny.pos_x, eny.pos_y, now);

        dist = std::hypot(you.pos_x - eny.pos_x, you.pos_y - eny.pos_y);
        bool enyAttackStarted = (eny.is_attacking > 0 && lastEnyAttacking == 0);
        bool youAttackJustEnded = (you.is_attacking == 0 && lastYouAttacking > 0);
        enemyThreatensYou = EnemyAttackThreatensYou(you, eny, dist);
        lastEnyAttacking = eny.is_attacking;
        lastYouAttacking = you.is_attacking;
        bool youDiedNow = Tuning::USE_EXPERIMENTAL_ENTITY_FIELDS && you.is_dead && !lastYouDead;
        bool enyDiedNow = Tuning::USE_EXPERIMENTAL_ENTITY_FIELDS && eny.is_dead && !lastEnyDead;
        lastYouDead = you.is_dead;
        lastEnyDead = eny.is_dead;
        if (youDiedNow || enyDiedNow) {
            attackTracker.Clear();
            dodgeTimer.Reset();
            Input::SetMoveDirection(Input::MoveDir::None);
        }
        strategy = ComputeStrategy(you, eny);
        if (you.is_attacking > 0 && attackTracker.active) {
            attackTracker.ObserveAttack();
        }
        if (youAttackJustEnded && attackTracker.active && attackTracker.observedAttack) {
            attackTracker.Finish(now);
        }
        if (attackTracker.active && !attackTracker.observedAttack) {
            if (now - attackTracker.issuedAtMs > Tuning::ATTACK_START_TIMEOUT_MS) {
                attackTracker.Clear();
            } else {
                decision = "waiting for attack start";
                actedThisFrame = true;
            }
        }
        if (attackTracker.waitingConfirm) {
            bool hit = (eny.damage > attackTracker.enyDamageAtStart + 0.01) ||
                       (Tuning::USE_EXPERIMENTAL_ENTITY_FIELDS && eny.knocked_back != 0);
            bool expired = (now - attackTracker.finishedAtMs) > (long long)Tuning::HIT_CONFIRM_MS;

            if (hit) {
                decision = "HIT CONFIRMED";
                g_comboEngine.OnHitConfirmed(attackTracker.moveId, now);
                attackTracker.Clear();
                actedThisFrame = true;
            } else if (expired) {
                g_comboEngine.Clear();
                attackTracker.Clear();
            } else {
                decision = "confirming hit";
                actedThisFrame = true;
            }
        }
        if (!actedThisFrame) {
            bool looksOffstage = you.airborne &&
                (you.pos_y > Tuning::RECOVER_Y_THRESH ||
                 (you.pos_y > 1900.0 && std::abs(you.pos_x) > Tuning::RECOVER_SIDE_X_THRESH));

            if (Tuning::USE_EXPERIMENTAL_ENTITY_FIELDS && you.is_edging) {
                if (!lastYouEdging) {
                    ledgeGetUpAt = now + Tuning::LEDGE_GETUP_MIN_MS + (rand() % (Tuning::LEDGE_GETUP_VARIANCE_MS + 1));
                }
                Input::SetMoveDirection(Input::MoveDir::None);
                dodgeTimer.Reset();
                attackTracker.Clear();
                if (now >= ledgeGetUpAt) {
                    decision = "LEDGE GETUP";
                    Input::Tap(Keys::JUMP);
                } else {
                    decision = "LEDGE HANG";
                }
            }
            else if (looksOffstage) {
                decision = "RECOVER";
                if (std::abs(you.pos_x) > Tuning::RECOVER_CENTER_DEADZONE) {
                    Input::SetMoveDirection(you.pos_x > 0 ? Input::MoveDir::Left : Input::MoveDir::Right);
                } else {
                    Input::SetMoveDirection(Input::MoveDir::None);
                }
                if (now - lastRecoveryInputAt >= Tuning::RECOVERY_INPUT_INTERVAL_MS) {
                    Input::Tap(Keys::JUMP);
                    lastRecoveryInputAt = now;
                }
                dodgeTimer.Reset();
                attackTracker.Clear();
            }
            else if (you.is_attacking == 0 &&
                     dodgeTimer.ShouldReact(now, enemyThreatensYou, strategy.reactChance)) {
                decision = enyAttackStarted ? "DODGE hitbox" : "DODGE threat";
                Input::SetMoveDirection(Input::MoveDir::None);
                ExecuteDodge(ChooseDodgeDirOutOfThreat(you, eny));
                dodgeTimer.Reset();
                attackTracker.Clear();
                g_comboEngine.Clear();
            }
            else if (you.is_attacking > 0) {
                decision = "attacking (waiting)";
            }
            else if (!you.is_stunned) {
                bool enemyVulnerable = Tuning::USE_EXPERIMENTAL_ENTITY_FIELDS &&
                                       (eny.is_edging || eny.remaining_options <= Tuning::EDGE_GUARD_MAX_OPTIONS) &&
                                       std::abs(eny.pos_x) > Tuning::EDGE_GUARD_TRIGGER_X;

                bool targetPastRightEdge = you.pos_x > strategy.edgeSoftLimitX && eny.pos_x > you.pos_x;
                bool targetPastLeftEdge  = you.pos_x < -strategy.edgeSoftLimitX && eny.pos_x < you.pos_x;

                if (enemyVulnerable) {
                    decision = "EDGE GUARD";
                    double dx = eny.pos_x - you.pos_x;
                    if (std::abs(dx) > Tuning::CHASE_DEADZONE) {
                        Input::SetMoveDirection(dx > 0 ? Input::MoveDir::Right : Input::MoveDir::Left);
                    }
                    AttackChoice guardChoice = PickAttack(you, eny, youVel, enyVel, g_comboEngine.PendingMoveId(now));
                    if (guardChoice.move) {
                        decision = std::string("EDGE GUARD ATTACK: ") + guardChoice.move->name;
                        debugMoveId = guardChoice.id;
                        debugMoveName = guardChoice.move->name;
                        debugHitFrame = guardChoice.hitFrame;
                        debugScore = guardChoice.score;
                        Input::SetMoveDirection(Input::MoveDir::None);
                        ExecuteAttack(*guardChoice.move, eny.pos_x > you.pos_x);
                        attackTracker.Start(guardChoice.id, eny.damage, now);
                    }
                }
                else if (targetPastRightEdge || targetPastLeftEdge) {
                    decision = "EDGE HOLD";
                    Input::SetMoveDirection(targetPastRightEdge ? Input::MoveDir::Left : Input::MoveDir::Right);
                    attackTracker.Clear();
                }
                else {
                AttackChoice choice = PickAttack(you, eny, youVel, enyVel, g_comboEngine.PendingMoveId(now));
                if (choice.move) {
                    decision = std::string(g_comboEngine.IsActive(now) ? "COMBO: " : "ATTACK: ") + choice.move->name;
                    debugMoveId = choice.id;
                    debugMoveName = choice.move->name;
                    debugHitFrame = choice.hitFrame;
                    debugScore = choice.score;
                    Input::SetMoveDirection(Input::MoveDir::None);
                    ExecuteAttack(*choice.move, eny.pos_x > you.pos_x);
                    attackTracker.Start(choice.id, eny.damage, now);
                } else {
                    decision = "approaching";
                    double dx = eny.pos_x - you.pos_x;
                    if (std::abs(dx) > Tuning::CHASE_DEADZONE) {
                        Input::SetMoveDirection(dx > 0 ? Input::MoveDir::Right : Input::MoveDir::Left);
                    } else {
                        Input::SetMoveDirection(Input::MoveDir::None);
                    }
                }
                }
            }
        }
        lastYouEdging = you.is_edging;
        }

        if (diagnosticLog && now - lastDiagnosticAt >= 500) {
            diagnosticLog << now << ',' << '"' << decision << '"' << ','
                          << (botOn ? 1 : 0) << ',' << dps.size() << ',' << actors.size() << ','
                          << fighterActors.size() << ',' << (fighterFilterStrong ? "legend" : "fallback") << ','
                          << localMatches << ',' << (haveYou ? 1 : 0) << ',' << (haveEny ? 1 : 0) << ','
                          << g_calibratedYouDp << ',' << (autoCalibrating ? 1 : 0) << ','
                          << (g_useNewAirDashOffsets ? "new" : "legacy") << '\n';
            diagnosticLog.flush();
            lastDiagnosticAt = now;
        }

        if (trackingOk && decisionLog && (now - lastLogAt >= Tuning::LOG_EVERY_MS || decision != lastLoggedDecision)) {
            decisionLog << now << ','
                        << '"' << decision << '"' << ','
                        << you.pos_x << ',' << you.pos_y << ','
                        << eny.pos_x << ',' << eny.pos_y << ','
                        << youVel.velX << ',' << youVel.velY << ','
                        << enyVel.velX << ',' << enyVel.velY << ','
                        << dist << ','
                        << you.weapon_type_id << ','
                        << eny.attack_id << ','
                        << (int)you.is_attacking << ','
                        << (int)eny.is_attacking << ','
                        << (enemyThreatensYou ? 1 : 0) << ','
                        << you.is_dashing << ','
                        << you.is_stunned << ','
                        << debugMoveId << ','
                        << '"' << debugMoveName << '"' << ','
                        << debugHitFrame << ','
                        << std::fixed << std::setprecision(2) << debugScore << ','
                        << you.damage << ','
                        << eny.damage << ','
                        << (attackTracker.active ? 1 : 0) << ','
                        << (attackTracker.waitingConfirm ? 1 : 0) << ','
                        << you.stocks << ',' << eny.stocks << ','
                        << (you.is_dead ? 1 : 0) << ',' << (eny.is_dead ? 1 : 0) << ','
                        << (you.is_edging ? 1 : 0) << ',' << (eny.is_edging ? 1 : 0) << ','
                        << you.is_attacking_ent << ',' << eny.is_attacking_ent << ','
                        << you.remaining_options << ','
                        << you.knocked_back << ',' << eny.knocked_back << ','
                        << std::fixed << std::setprecision(2) << strategy.reactChance << ','
                        << strategy.edgeSoftLimitX
                        << "\n";
            decisionLog.flush();
            lastLogAt = now;
            lastLoggedDecision = decision;
        }
        if (now - lastDashboardAt >= Tuning::DASHBOARD_INTERVAL_MS) {
            lastDashboardAt = now;
            using namespace Con;
            char buf[256];
            Home();

            Line("BRAWLHALLA AUTOPLAY - LIVE DEBUG", BRIGHT);
            snprintf(buf, sizeof(buf), "bot=%s | esp=%s | air/dash=%s | F1 bot | F2 esp | F3 air/dash | F4 calib | F10 exit",
                     botOn ? "ENABLED" : "disabled", Esp::enabled ? "ENABLED" : "disabled",
                     g_useNewAirDashOffsets ? "CURRENT" : "LEGACY");
            Line(buf, GRAY);

            Section("CURRENT DECISION");
            snprintf(buf, sizeof(buf), "  >> %s", decision.c_str());
            Line(buf, trackingOk ? YELLOW : RED);
            if (debugMoveId != -1) {
                snprintf(buf, sizeof(buf), "     move: %s (id %d)  score=%.1f  predicted hit in %d frame(s)",
                         debugMoveName.c_str(), debugMoveId, debugScore, debugHitFrame);
                Line(buf, GRAY);
            }
            snprintf(buf, sizeof(buf), "  attack: tracking=%s waiting_for_confirmation=%s",
                     attackTracker.active ? "yes" : "no", attackTracker.waitingConfirm ? "yes" : "no");
            Line(buf, attackTracker.active || attackTracker.waitingConfirm ? CYAN : GRAY);
            Section("DETECTION");
            snprintf(buf, sizeof(buf), "  raw=%zu  entities=%zu  fighters=%zu  filter=%s  local9=%d  you=%s  enemy=%s",
                     dps.size(), actors.size(), fighterActors.size(), fighterFilterStrong ? "legend" : "fallback",
                     localMatches, haveYou ? "OK" : "MISSING", haveEny ? "OK" : "MISSING");
            Line(buf, trackingOk ? GREEN : RED);
            {
                int shown = 0;
                for (const Actor& a : actors) {
                    if (shown >= 4) { Line("  ... (additional entities hidden)", GRAY); break; }
                    bool isCalibratedYou = (g_calibratedYouDp != 0 && a.dp == g_calibratedYouDp);
                    snprintf(buf, sizeof(buf),
                             "  #%d dp=0x%08llX ent=0x%08llX local=%d legend=%d face=%d air(old/new)=%d/%d%s",
                             shown, (unsigned long long)a.dp, (unsigned long long)a.entity_ptr,
                             a.is_local_player, a.legend_id, a.facing_new, a.airborne_old, a.airborne_new,
                             isCalibratedYou ? "  <- YOU (calibrated)" : "");
                    Line(buf, isCalibratedYou ? GREEN : GRAY);
                    shown++;
                }
                if (actors.empty()) Line("  no valid PhysObject + Entity pair found", RED);
            }

            if (haveYou) {
                Section("IDENTIFICATION");
                snprintf(buf, sizeof(buf), "  method=%s  YOU dp=0x%08llX  F4 discards and recalibrates",
                         identificationMode.c_str(), (unsigned long long)you.dp);
                Line(buf, GREEN);
            } else if (calibrating) {
                Section(autoCalibrating ? "AUTOMATIC CALIBRATION" : "CALIBRATION - move in BOTH directions");
                Line(autoCalibrating
                         ? "  Controlled probe in progress: left, pause, right. Please wait."
                         : "  1. Hold LEFT briefly; 2. then hold RIGHT. F4 resets calibration.",
                     YELLOW);
                Line("  Identification requires enough evidence and a clear lead over the other actor.", GRAY);
                for (auto& c : g_calibCandidates) {
                    snprintf(buf, sizeof(buf), "  dp=0x%08llX score=%.0fms left=%dms right=%dms samples=%d",
                             (unsigned long long)c.dp, c.scoreMs, c.matchedLeftMs, c.matchedRightMs, c.samples);
                    Line(buf, c.scoreMs > 0 ? GREEN : (c.scoreMs < 0 ? RED : GRAY));
                }
            }

            if (trackingOk) {
                Section("YOU");
                snprintf(buf, sizeof(buf), "  pos=(%.0f, %.0f)   velocity=(%.0f, %.0f)   opponent_distance=%.0f",
                         you.pos_x, you.pos_y, youVel.velX, youVel.velY, dist);
                Line(buf, YELLOW);
                snprintf(buf, sizeof(buf), "  weapon=%s(%d)   facing=%s   airborne=%s   attacking=%d (attack_id %d)",
                         WeaponPrefix(you.weapon_type_id) ? WeaponPrefix(you.weapon_type_id) : "?",
                         you.weapon_type_id, you.facing == 0 ? "right" : "left",
                         you.airborne ? "yes" : "no", (int)you.is_attacking, you.attack_id);
                Line(buf, WHITE);
                snprintf(buf, sizeof(buf), "  damage=%.1f%%   stocks=%d   stunned=%s   can_dodge(dash==0)=%s   ledge=%s",
                         you.damage, you.stocks, you.is_stunned ? "YES" : "no",
                         you.is_dashing == 0 ? "yes" : "NO", you.is_edging ? "YES" : "no");
                Line(buf, you.is_stunned ? RED : WHITE);
                snprintf(buf, sizeof(buf), "  dp=0x%08llX entity_ptr=0x%08llX weapon_ptr_ok=%s",
                         (unsigned long long)you.dp, (unsigned long long)you.entity_ptr,
                         you.weapon_type_id != 0 ? "yes" : "no");
                Line(buf, GRAY);
                snprintf(buf, sizeof(buf), "  [experimental] dead=%s  entity_attacking=%d  remaining_options=%d  knocked_back=%d",
                         you.is_dead ? "YES" : "no", you.is_attacking_ent, you.remaining_options, you.knocked_back);
                Line(buf, GRAY);

                Section("OPPONENT");
                snprintf(buf, sizeof(buf), "  pos=(%.0f, %.0f)   velocity=(%.0f, %.0f)",
                         eny.pos_x, eny.pos_y, enyVel.velX, enyVel.velY);
                Line(buf, YELLOW);
                snprintf(buf, sizeof(buf), "  damage=%.1f%%   stocks=%d   attacking=%d (attack_id %d)   threatening=%s",
                         eny.damage, eny.stocks, (int)eny.is_attacking, eny.attack_id,
                         enemyThreatensYou ? "YES" : "no");
                Line(buf, enemyThreatensYou ? RED : WHITE);
                snprintf(buf, sizeof(buf), "  dp=0x%08llX entity_ptr=0x%08llX",
                         (unsigned long long)eny.dp, (unsigned long long)eny.entity_ptr);
                Line(buf, GRAY);
                snprintf(buf, sizeof(buf), "  ledge=%s   remaining_options=%d   [experimental] dead=%s  knocked_back=%d",
                         eny.is_edging ? "YES (vulnerable)" : "no", eny.remaining_options,
                         eny.is_dead ? "YES" : "no", eny.knocked_back);
                Line(buf, eny.is_edging ? GREEN : GRAY);
                Section(std::string("AIR/DASH OFFSETS - ACTIVE: ") +
                        (g_useNewAirDashOffsets ? "CURRENT (post-patch)" : "LEGACY (pre-patch diagnostic)"));
                snprintf(buf, sizeof(buf), "  YOU confirmed facing: current(+0x110)=%d | legacy(+0x108)=%d | air old/new=%d/%d %s",
                         you.facing_new, you.facing_old, you.airborne_old, you.airborne_new,
                         g_useNewAirDashOffsets ? "<CURRENT ACTIVE" : "<LEGACY ACTIVE");
                Line(buf, WHITE);
                snprintf(buf, sizeof(buf), "  YOU is_dashing: legacy(+0x190)=%d %s  current(+0x1A4)=%d %s",
                         you.is_dashing_old, g_useNewAirDashOffsets ? "" : "<ACTIVE", you.is_dashing_new, g_useNewAirDashOffsets ? "<ACTIVE" : "");
                Line(buf, GRAY);
                snprintf(buf, sizeof(buf), "  ENEMY facing: legacy=%d current=%d   air: legacy=%d current=%d   dash: legacy=%d current=%d",
                         eny.facing_old, eny.facing_new, eny.airborne_old, eny.airborne_new, eny.is_dashing_old, eny.is_dashing_new);
                Line(buf, GRAY);
                Line("  F3 never changes facing; +0x110 is confirmed. It switches only airborne/dash.", GRAY);

                Section(Tuning::USE_EXPERIMENTAL_ENTITY_FIELDS
                            ? "EXPERIMENTAL STRATEGY (stocks)"
                            : "SAFE STRATEGY (experimental fields are telemetry only)");
                int stockDiff = you.stocks - eny.stocks;
                snprintf(buf, sizeof(buf), "  stock difference: %+d (%s)", stockDiff,
                         stockDiff > 0 ? "ahead" : (stockDiff < 0 ? "behind" : "tied"));
                Line(buf, stockDiff > 0 ? GREEN : (stockDiff < 0 ? RED : WHITE));
                snprintf(buf, sizeof(buf), "  threat reaction chance: %.0f%%   edge chase limit: %.0f",
                         strategy.reactChance * 100.0, strategy.edgeSoftLimitX);
                Line(buf, GRAY);
            }

            Section("LOG");
            snprintf(buf, sizeof(buf), "  decisions: %s", logPath.c_str());
            Line(buf, GRAY);
            snprintf(buf, sizeof(buf), "  always-on diagnostics: %s", diagnosticPath.c_str());
            Line(buf, GRAY);
            ClosePanel();

            ClearRest();
            std::cout << std::flush;
        }

        Sleep(16);
    }
    resetBotState();
    RestoreInstalledHook();
    if (Esp::overlay) DestroyWindow(Esp::overlay);
    return 0;
}

