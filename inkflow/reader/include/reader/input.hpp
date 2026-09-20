// ABOUTME: Turns sampled button levels into presses, including ones that begin and end
// ABOUTME: while the panel is blocked mid-refresh.

#pragma once

#include "reader/surface.hpp"

#include <cstdint>

namespace reader {

/// Reads the state of every button at this instant.
///
/// Levels, not events: edge detection belongs here rather than in a platform driver, so
/// that the simulator and the device derive presses from the same code.
class ButtonSource {
public:
    /// One bit per button, in the order the platform numbers them.
    virtual uint16_t levels() = 0;

protected:
    ~ButtonSource() = default;
};

/// Whatever the platform counts milliseconds with.
///
/// The latch needs one because a hold is a duration, and the only honest place to
/// measure it is where the button is sampled -- which is inside a blocking refresh,
/// where the reading loop is not running and cannot time anything.
class Clock {
public:
    virtual uint32_t nowMs() = 0;

protected:
    ~Clock() = default;
};

/// Catches presses the reading loop would otherwise sleep through.
///
/// The panel blocks for about 500ms of every 650ms reading cycle. Sampling buttons once
/// per turn therefore misses roughly three presses in four -- not because the press is
/// too short, but because the device is not looking. Documented as a known limitation
/// for as long as it existed, and reported as "pausing doesn't work" the first time
/// anyone read a book on it, which is the more honest description.
///
/// Registered as the surface's `Servicer`, this samples during the blocking draw and
/// latches what it sees. The latch is drained once per turn, so a press is acted on
/// exactly once no matter how many samples observed it.
class ButtonLatch : public Servicer {
public:
    ButtonLatch(ButtonSource& source, Clock& clock) : source_(source), clock_(clock) {}

    /// Samples now, recording any button that has just gone down and how long any button
    /// that has just come up was held for.
    void service() override {
        const uint16_t now = source_.levels();
        const uint16_t rising = static_cast<uint16_t>(now & static_cast<uint16_t>(~last_));
        const uint16_t falling = static_cast<uint16_t>(static_cast<uint16_t>(~now) & last_);
        const uint32_t at = clock_.nowMs();

        for (uint8_t b = 0u; b < kMaxButtons; ++b) {
            const uint16_t bit = static_cast<uint16_t>(1u << b);
            if ((rising & bit) != 0u) {
                downAt_[b] = at;
            } else if ((falling & bit) != 0u && downAt_[b] != 0u) {
                // Longest hold since the last drain, so a drain that spans two presses
                // reports the deliberate one rather than whichever came last.
                const uint32_t forMs = at - downAt_[b];
                if (forMs > heldFor_[b]) {
                    heldFor_[b] = forMs;
                }
                downAt_[b] = 0u;
            }
        }

        latched_ = static_cast<uint16_t>(latched_ | rising);
        last_ = now;
        lastSampleAt_ = at;
    }

    /// How long `button` was held on its last completed press, in milliseconds.
    ///
    /// Measured between samples taken inside the refresh, so it is honest to about the
    /// sampling interval rather than to the loop's 650ms turn -- which is the difference
    /// between a one-second hold being recognised and being missed.
    uint32_t heldFor(uint8_t button) const {
        return button < kMaxButtons ? heldFor_[button] : 0u;
    }

    /// Whether `button` has been down, uninterrupted, for at least `ms`.
    ///
    /// For a gesture that should fire while still held rather than on release.
    bool heldAtLeast(uint8_t button, uint32_t ms) const {
        if (button >= kMaxButtons || downAt_[button] == 0u) {
            return false;
        }
        return (last_ & static_cast<uint16_t>(1u << button)) != 0u &&
               (lastSampleAt_ - downAt_[button]) >= ms;
    }

    /// Everything pressed since the last drain, and clears it -- along with the hold
    /// durations, which belong to the presses being drained.
    uint16_t take() {
        const uint16_t seen = latched_;
        latched_ = 0u;
        for (uint8_t b = 0u; b < kMaxButtons; ++b) {
            heldFor_[b] = 0u;
        }
        return seen;
    }

    /// Whether `button` is down right now, without draining anything.
    bool held(uint8_t button) const {
        return (last_ & static_cast<uint16_t>(1u << button)) != 0u;
    }

    static constexpr uint8_t kMaxButtons = 16u;

private:
    ButtonSource& source_;
    Clock& clock_;
    uint16_t last_ = 0u;
    uint16_t latched_ = 0u;
    uint32_t lastSampleAt_ = 0u;
    uint32_t downAt_[kMaxButtons] = {};
    uint32_t heldFor_[kMaxButtons] = {};
};

}  // namespace reader
