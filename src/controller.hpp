#pragma once
#include <algorithm>
#include <cmath>

namespace lament {
// Restart the envelope from the current gain when the requested state reverses.
class CombatFade {
public:
    void reset() { value_ = from_ = target_ = elapsed_ = 0.0f; duration_ = 0.5f; }
    void setCombat(bool combat) {
        const float next = combat ? 1.0f : 0.0f;
        if (next == target_) return;
        from_ = value_;
        target_ = next;
        elapsed_ = 0.0f;
        duration_ = combat ? 0.5f : 1.5f;
    }
    float advance(float seconds) {
        if (!std::isfinite(seconds) || seconds < 0.0f) seconds = 0.0f;
        elapsed_ = std::min(duration_, elapsed_ + seconds);
        value_ = from_ + (target_ - from_) * (elapsed_ / duration_);
        return value_;
    }
    float value() const { return value_; }
private:
    float value_ = 0, from_ = 0, target_ = 0, elapsed_ = 0, duration_ = 0.5f;
};
// Native battle cleanup can clear the battle flags before its main-volume
// fade-in finishes. Keep the replacement at full main gain through that tail.
class MainGainGuard {
public:
    void reset() { recovering_ = false; }
    float select(bool battleContext, bool otherSubMusic, float current, float destination) {
        if (otherSubMusic) { recovering_ = false; return current; }
        if (battleContext) { recovering_ = true; return 1.0f; }
        if (recovering_ && destination == 1.0f && current < 1.0f) return 1.0f;
        recovering_ = false;
        return current;
    }
private:
    bool recovering_ = false;
};
constexpr unsigned LamentId = 0x0100005f;
inline bool isBattle(unsigned id, unsigned normal, unsigned twilight) {
    return id == normal || id == twilight;
}
inline bool combatRequested(unsigned subId, unsigned normal, unsigned twilight,
                            unsigned countdown, bool disabled, bool sceneExists) {
    return isBattle(subId, normal, twilight) && countdown == 0 && !disabled && sceneExists;
}
}
