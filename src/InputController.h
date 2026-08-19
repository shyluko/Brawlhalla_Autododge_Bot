#pragma once

#include <windows.h>
#include <mutex>
#include <unordered_set>
#include <vector>

class InputController {
public:
    void Press(WORD vk, int holdMs = 5) {
        Down(vk);
        Sleep(holdMs);
        Up(vk);
    }

    void Release(WORD vk) {
        Up(vk);
    }

    void Down(WORD vk) {
        if (vk == 0 || held_.count(vk)) return;
        held_.insert(vk);
        SendScan(vk, false);
    }

    void Up(WORD vk) {
        if (vk == 0 || !held_.count(vk)) return;
        held_.erase(vk);
        SendScan(vk, true);
    }

    void ReleaseAll() {
        const std::vector<WORD> keys(held_.begin(), held_.end());
        for (WORD vk : keys) Up(vk);
        held_.clear();
    }

    void Reset() {
        ReleaseAll();
    }

    bool IsHeld(WORD vk) const { return held_.count(vk) != 0; }

private:
    void SendScan(WORD vk, bool up) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
        SendInput(1, &input, sizeof(INPUT));
    }

    std::unordered_set<WORD> held_;
};
