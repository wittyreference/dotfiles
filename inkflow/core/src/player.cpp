// ABOUTME: Playback state machine implementation -- position, ramp-aware timing,
// ABOUTME: sentence rewind, and resume points guarded by a document fingerprint.

#include "rsvp/player.hpp"

#include "rsvp/fingerprint.hpp"

namespace rsvp {
// Fingerprinting lives in fingerprint.hpp so the .rsvp writer and the player
// cannot drift apart on what identifies a document.

Player::Player(const Token* tokens, std::uint32_t count, const TimingConfig& config) noexcept
    : tokens_(tokens),
      count_(tokens == nullptr ? 0u : count),
      config_(config),
      index_(0u),
      sinceResume_(0u),
      playing_(false),
      atEnd_(tokens == nullptr || count == 0u) {}

void Player::play() noexcept {
    if (count_ == 0u) {
        return;
    }
    playing_ = true;

    // Every resume restarts the ramp. Un-pausing straight back to cruise speed is
    // jarring, and a pause is precisely when the reader lost the thread.
    sinceResume_ = 0u;
}

void Player::pause() noexcept {
    playing_ = false;
}

const Token* Player::current() const noexcept {
    if (count_ == 0u || atEnd_ || index_ >= count_) {
        return nullptr;
    }
    return &tokens_[index_];
}

std::uint32_t Player::currentHoldMs() const noexcept {
    const Token* token = current();
    if (token == nullptr) {
        return 0u;
    }

    TimingConfig effective = config_;
    effective.wpm = rampedWpm(sinceResume_, config_);
    return holdMs(*token, effective);
}

bool Player::advance() noexcept {
    if (count_ == 0u || atEnd_) {
        return false;
    }
    if (index_ + 1u >= count_) {
        // Off the last token: stop rather than wrap. Wrapping a finished chapter
        // back to its start would be indistinguishable from a crash.
        atEnd_ = true;
        playing_ = false;
        return false;
    }
    ++index_;
    ++sinceResume_;
    return true;
}

std::uint32_t Player::sentenceStartAt(std::uint32_t from) const noexcept {
    // The sentence containing `from` begins just after the nearest preceding
    // sentence terminator, or at the document start if there is none.
    for (std::uint32_t i = from; i > 0u; --i) {
        if (tokens_[i - 1u].has(kTokenFlagSentenceEnd)) {
            return i;
        }
    }
    return 0u;
}

void Player::rewindSentence() noexcept {
    if (count_ == 0u) {
        return;
    }
    atEnd_ = false;

    const std::uint32_t start = sentenceStartAt(index_);
    if (start < index_) {
        index_ = start;
    } else if (index_ > 0u) {
        // Already on this sentence's first token, so step into the previous one.
        // That is what makes repeated presses walk backwards instead of sticking.
        index_ = sentenceStartAt(index_ - 1u);
    }

    sinceResume_ = 0u;
}

void Player::seek(std::uint32_t tokenIndex) noexcept {
    if (count_ == 0u) {
        return;
    }
    index_ = tokenIndex >= count_ ? count_ - 1u : tokenIndex;
    atEnd_ = false;
    sinceResume_ = 0u;
}

std::uint32_t Player::remainingMs() const noexcept {
    // Linear in the tokens left, so this is for refreshing a time-remaining
    // indicator occasionally -- not for calling once per token in the hot loop.
    std::uint32_t total = 0u;
    for (std::uint32_t i = index_; i < count_; ++i) {
        total += holdMs(tokens_[i], config_);
    }
    return total;
}

ResumePoint Player::resumePoint() const noexcept {
    return ResumePoint{index_, documentFingerprint(tokens_, count_)};
}

bool Player::restore(const ResumePoint& point) noexcept {
    if (count_ == 0u || point.tokenIndex >= count_) {
        return false;
    }
    if (point.documentId != documentFingerprint(tokens_, count_)) {
        return false;
    }

    index_ = point.tokenIndex;
    atEnd_ = false;
    sinceResume_ = 0u;
    return true;
}

}  // namespace rsvp
