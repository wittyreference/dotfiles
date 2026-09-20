// ABOUTME: The book picker's list handling and drawing.
// ABOUTME: See picker.hpp for why it copies names rather than borrowing them.

#include "reader/picker.hpp"

#include <cstring>

namespace reader {

namespace {

/// Top of the first row, and the step between rows. Both in panel pixels, measured from
/// the top so the list starts where the eye does rather than where the reading band is.
constexpr int16_t kFirstRowY = 120;
constexpr int16_t kRowStep = 38;
constexpr int16_t kMargin = 40;
/// Room for the marker drawn against the selected row.
constexpr int16_t kMarkerWidth = 22;

}  // namespace

Picker::Picker(Surface& surface, const Layout& layout) : surface_(surface), layout_(layout) {
    names_[0][0] = '\0';
}

void Picker::clear() {
    count_ = 0u;
    selected_ = 0u;
}

bool Picker::add(const char* name) {
    if (name == nullptr || count_ >= kMaxEntries) {
        return false;
    }
    // Truncated rather than refused: a book with a long filename is still a book the
    // reader owns, and a shortened name beats pretending the card is empty.
    std::strncpy(names_[count_], name, kMaxNameBytes - 1u);
    names_[count_][kMaxNameBytes - 1u] = '\0';
    ++count_;
    return true;
}

std::size_t Picker::count() const { return count_; }

const char* Picker::name(std::size_t index) const {
    return index < count_ ? names_[index] : nullptr;
}

std::size_t Picker::selected() const { return selected_; }

void Picker::moveUp() {
    if (selected_ > 0u) {
        --selected_;
    }
}

void Picker::moveDown() {
    if (count_ > 0u && selected_ + 1u < count_) {
        ++selected_;
    }
}

int16_t Picker::rowY(std::size_t index) const {
    return static_cast<int16_t>(kFirstRowY + static_cast<int16_t>(index) * kRowStep);
}

/// Painters are nested for the same reason the reader's are: GxEPD2 walks the framebuffer
/// in pages and calls back once per page, so the drawing has to be repeatable.
class Picker::ListPainter : public Painter {
public:
    explicit ListPainter(const Picker& picker) : picker_(picker) {}

    void paint(Surface& s) const override {
        s.fill(Ink::kWhite);
        picker_.drawList(s);
    }

private:
    const Picker& picker_;
};

void Picker::drawList(Surface& s) const {
    s.text(Font::kStatus, kMargin, 56, "Books on the card");
    s.rect(kMargin, 74, static_cast<int16_t>(layout_.width - 2 * kMargin), 2, Ink::kBlack);

    if (count_ == 0u) {
        // Not an error and not a crash: the reader still has its built-in passage, and
        // saying so is more use than an empty list the reader has to interpret.
        s.text(Font::kStatus, kMargin, kFirstRowY, "No books here.");
        s.text(Font::kStatus, kMargin, static_cast<int16_t>(kFirstRowY + kRowStep),
               "Send one over wifi, or put a .rsvp on the card.");
        return;
    }

    for (std::size_t i = 0u; i < count_; ++i) {
        const int16_t y = rowY(i);
        if (y > layout_.height) {
            break;
        }
        if (i == selected_) {
            // A filled bar rather than an outline or an inverted row. One bit of ink, no
            // colour to fall back on, and a bar survives ghosting better than a hairline.
            s.rect(kMargin, static_cast<int16_t>(y - 18), kMarkerWidth, 20, Ink::kBlack);
        }
        s.text(Font::kStatus, static_cast<int16_t>(kMargin + kMarkerWidth + 14), y, names_[i]);
    }
}

void Picker::render() {
    const ListPainter painter(*this);
    surface_.renderFull(painter);
}

}  // namespace reader
