#pragma once

#include "GameDataManager.h"

#include <algorithm>
#include <cmath>
#include <vector>

struct CalibCandidate {
    uintptr_t dp = 0;
    double lastX = 0;
    double scoreMs = 0;
    int samples = 0;
    int matchedLeftMs = 0;
    int matchedRightMs = 0;
    long long lastSeenMs = 0;
};

class LocalCalibrator {
public:
    uintptr_t localDp = 0;

    void reset() {
        localDp = 0;
        missingFrames_ = 0;
        heldSign_ = 0;
        heldSinceMs_ = 0;
        lastStepMs_ = 0;
        candidates_.clear();
    }

    void step(const std::vector<Actor>& actors, long long nowMs, int heldSign) {
        if (heldSign != heldSign_) {
            heldSign_ = heldSign;
            heldSinceMs_ = nowMs;
        }

        const int dtMs = lastStepMs_ == 0 ? 0 : static_cast<int>(std::clamp(nowMs - lastStepMs_, 0LL, 50LL));
        lastStepMs_ = nowMs;

        for (const Actor& actor : actors) {
            CalibCandidate* found = nullptr;
            for (auto& candidate : candidates_) {
                if (candidate.dp == actor.dp) {
                    found = &candidate;
                    break;
                }
            }
            if (!found) {
                candidates_.push_back({actor.dp, actor.pos_x, 0, 0, 0, 0, nowMs});
                continue;
            }
            const double dx = actor.pos_x - found->lastX;
            found->lastX = actor.pos_x;
            found->lastSeenMs = nowMs;
            const bool inputStable = heldSign != 0 && nowMs - heldSinceMs_ >= 80;
            if (inputStable && dtMs > 0 && std::abs(dx) > 0.25) {
                const double dxSign = dx > 0 ? 1.0 : -1.0;
                const bool matched = dxSign == heldSign;
                found->scoreMs += matched ? dtMs : -dtMs;
                found->samples++;
                if (matched && heldSign < 0) found->matchedLeftMs += dtMs;
                if (matched && heldSign > 0) found->matchedRightMs += dtMs;
            }
        }

        candidates_.erase(std::remove_if(candidates_.begin(), candidates_.end(), [nowMs](const CalibCandidate& c) {
            return nowMs - c.lastSeenMs > 800;
        }), candidates_.end());

        CalibCandidate* best = nullptr;
        double secondScore = -1e9;
        for (auto& candidate : candidates_) {
            if (!best || candidate.scoreMs > best->scoreMs) {
                if (best) secondScore = best->scoreMs;
                best = &candidate;
            } else if (candidate.scoreMs > secondScore) {
                secondScore = candidate.scoreMs;
            }
        }
        if (best && best->scoreMs >= 180 &&
            best->matchedLeftMs >= 60 &&
            best->matchedRightMs >= 60 &&
            best->samples >= 20 &&
            best->scoreMs - secondScore >= 80) {
            localDp = best->dp;
            candidates_.clear();
        }
    }

    std::string status() const {
        if (localDp) return "Calibrated local actor";
        return "Hold left/right to identify local player (" + std::to_string(candidates_.size()) + " candidates)";
    }

private:
    int missingFrames_ = 0;
    int heldSign_ = 0;
    long long heldSinceMs_ = 0;
    long long lastStepMs_ = 0;
    std::vector<CalibCandidate> candidates_;
};
