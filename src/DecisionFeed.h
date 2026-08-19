#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

struct DecisionEvent {
    std::string kind;
    std::string move;
    std::string reason;
    int frame = 0;
    int confidence = 0;
    long long createdMs = 0;

    std::string banner() const {
        if (kind.empty()) return "";
        std::string text = kind;
        if (!move.empty()) text += "\n" + move;
        if (frame > 0) text += "\nFrame " + std::to_string(frame);
        if (confidence > 0) text += "\nConfidence " + std::to_string(confidence) + "%";
        if (!reason.empty()) text += "\nReason: " + reason;
        return text;
    }
};

class DecisionFeed {
public:
    void push(DecisionEvent event) {
        event.createdMs = NowMs();
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = event;
        recent_.push_back(event);
        if (recent_.size() > 50) recent_.erase(recent_.begin());
    }

    DecisionEvent latest(long long maxAgeMs = 1800) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (latest_.kind.empty()) return {};
        if (NowMs() - latest_.createdMs > maxAgeMs) return {};
        return latest_;
    }

    std::vector<DecisionEvent> recent(size_t count = 12) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (recent_.size() <= count) return recent_;
        return std::vector<DecisionEvent>(recent_.end() - static_cast<std::ptrdiff_t>(count), recent_.end());
    }

    void clearTransient() {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = {};
    }

private:
    static long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    mutable std::mutex mutex_;
    DecisionEvent latest_;
    std::vector<DecisionEvent> recent_;
};
