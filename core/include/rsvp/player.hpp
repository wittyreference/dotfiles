// ABOUTME: Playback state machine over a token array -- play, pause, advance,
// ABOUTME: rewind by sentence, and serialisable resume state. No allocation, no I/O.

#ifndef RSVP_PLAYER_HPP
#define RSVP_PLAYER_HPP

#include "rsvp/timing.hpp"
#include "rsvp/token.hpp"

#include <cstdint>

namespace rsvp {

/// Where a reader left off, small enough to persist cheaply after every token.
///
/// Carries a fingerprint of the document as well as the position. Resume state
/// lives on an SD card next to the book, and applying one book's position to
/// another would drop the reader into a random paragraph -- so `restore` refuses a
/// point whose fingerprint does not match.
struct ResumePoint {
    std::uint32_t tokenIndex;
    std::uint32_t documentId;
};

/// Drives RSVP playback across a contiguous array of tokens.
///
/// Holds a borrowed pointer to the tokens and never copies or owns them: on the
/// device that array is a window mapped over an `.rsvp` file, and it can be far
/// larger than available RAM. The player only ever touches the token at the
/// current index and scans backwards for sentence starts, so it needs no
/// lookahead buffer.
class Player {
public:
    /// Constructs a player over `[tokens, tokens + count)`, paused at index 0.
    ///
    /// A null `tokens` or zero `count` yields a player that reports itself at the
    /// end immediately, and tolerates every other call without crashing -- an
    /// empty chapter is a real thing to hand this.
    Player(const Token* tokens, std::uint32_t count, const TimingConfig& config) noexcept;

    void play() noexcept;
    void pause() noexcept;

    bool isPlaying() const noexcept { return playing_; }
    bool isAtEnd() const noexcept { return atEnd_; }

    /// The token to display now, or null when there is nothing to show.
    const Token* current() const noexcept;

    /// How long the current token should be held, accounting for the speed ramp.
    std::uint32_t currentHoldMs() const noexcept;

    /// Moves to the next token. Returns false when there is no next token, which
    /// also ends playback rather than wrapping.
    bool advance() noexcept;

    /// Jumps to the first token of the current sentence.
    ///
    /// The single most-requested thing missing from RSVP readers: you blink, you
    /// lose the thread, and there is no way back. When already sitting on a
    /// sentence's first token this steps back to the *previous* sentence instead,
    /// so repeated presses walk backwards rather than sticking.
    ///
    /// Clears the end state, so a finished chapter can be re-read from here.
    void rewindSentence() noexcept;

    /// Moves to `tokenIndex`, clamped to the last valid token.
    void seek(std::uint32_t tokenIndex) noexcept;

    std::uint32_t position() const noexcept { return index_; }

    /// Estimated milliseconds of reading left, at the configured speed and
    /// ignoring the ramp. Intended for a time-remaining indicator: "8 minutes
    /// left in this chapter" is a far kinder progress cue than a percentage of a
    /// 900-page book.
    std::uint32_t remainingMs() const noexcept;

    ResumePoint resumePoint() const noexcept;

    /// Restores a previously saved point. Returns false and changes nothing if the
    /// fingerprint does not match this document or the index is out of range.
    bool restore(const ResumePoint& point) noexcept;

private:
    /// Index of the first token of the sentence containing `from`.
    std::uint32_t sentenceStartAt(std::uint32_t from) const noexcept;

    const Token* tokens_;
    std::uint32_t count_;
    TimingConfig config_;
    std::uint32_t index_;

    /// Tokens shown since playback last started. Drives the ramp, and resets on
    /// every `play()` so each resume eases back in rather than snapping to cruise.
    std::uint32_t sinceResume_;

    bool playing_;
    bool atEnd_;
};

}  // namespace rsvp

#endif  // RSVP_PLAYER_HPP
