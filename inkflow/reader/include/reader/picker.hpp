// ABOUTME: The book picker -- the list of documents on the card, and which one is chosen.
// ABOUTME: Draws through the same injected Surface the reading loop uses.

#pragma once

#include "reader/layout.hpp"
#include "reader/surface.hpp"

#include <cstddef>
#include <cstdint>

namespace reader {

/// Lets a reader choose what to read, on a device whose only input is a thumb rocker.
///
/// Until this existed the reader opened the first `.rsvp` it found at the root of the card
/// and there was no way to open anything else without taking the card out. A book the
/// device cannot be asked to open is a book the reader does not have.
///
/// Holds its own copies of the names rather than pointers into the caller's storage: the
/// card walk that produces them reuses one buffer per entry, and the list has to outlive
/// it. No allocation -- this runs on a device with 400KB of SRAM and no heap to speak of.
class Picker {
public:
    /// A card root with more books than this is not a card anyone is choosing from by
    /// thumb. Extra entries are refused rather than overwriting the ones already listed.
    static constexpr std::size_t kMaxEntries = 32u;
    /// Including the terminator. FAT long names can exceed this; they are shown short.
    static constexpr std::size_t kMaxNameBytes = 64u;

    Picker(Surface& surface, const Layout& layout);

    /// Forgets every entry and returns the selection to the top.
    void clear();

    /// Appends a name. Returns false when full, having changed nothing.
    bool add(const char* name);

    std::size_t count() const;
    /// The name at `index`, or null when there is nothing there.
    const char* name(std::size_t index) const;

    std::size_t selected() const;
    void moveUp();
    void moveDown();

    /// Baseline of the row that `index` is drawn on. Exposed so a test can assert on the
    /// pixels of a particular row rather than on a screenshot.
    int16_t rowY(std::size_t index) const;

    /// Draws the whole list. A full refresh: this screen replaces the reading frame
    /// entirely, and arriving at it with the previous screen's ghosting still on the panel
    /// is how a list becomes unreadable.
    void render();

private:
    class ListPainter;
    void drawList(Surface& s) const;

    Surface& surface_;
    Layout layout_;
    char names_[kMaxEntries][kMaxNameBytes];
    std::size_t count_ = 0u;
    std::size_t selected_ = 0u;
};

}  // namespace reader
