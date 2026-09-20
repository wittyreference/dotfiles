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
    explicit ButtonLatch(ButtonSource& source) : source_(source) {}

    /// Samples now, recording any button that has just gone down.
    void service() override {
        const uint16_t now = source_.levels();
        // A rising edge on any bit: down now, up when last sampled.
        latched_ = static_cast<uint16_t>(latched_ | (now & static_cast<uint16_t>(~last_)));
        last_ = now;
    }

    /// Everything pressed since the last drain, and clears it.
    uint16_t take() {
        const uint16_t seen = latched_;
        latched_ = 0u;
        return seen;
    }

    /// Whether `button` is down right now, without draining anything.
    bool held(uint8_t button) const {
        return (last_ & static_cast<uint16_t>(1u << button)) != 0u;
    }

private:
    ButtonSource& source_;
    uint16_t last_ = 0u;
    uint16_t latched_ = 0u;
};

}  // namespace reader
