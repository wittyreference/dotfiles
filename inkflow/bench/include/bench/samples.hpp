// ABOUTME: Fixed-capacity accumulator for timing samples, kept sorted so statistics
// ABOUTME: are always queryable. No allocation -- it runs on a 400 KB device.

#ifndef BENCH_SAMPLES_HPP
#define BENCH_SAMPLES_HPP

#include <cstddef>
#include <cstdint>

namespace bench {

/// Collects timing measurements and reports their distribution.
///
/// Values are held in sorted order, inserted by a shift rather than sorted on demand,
/// so every accessor is const and there is no finalise step to forget. Capacities here
/// are in the hundreds, so the linear insert costs nothing next to a refresh that takes
/// milliseconds.
///
/// Reports median and p95 rather than mean alone on purpose: e-paper refresh timing is
/// not symmetric, and one slow refresh in twenty is something a reader feels while a
/// mean hides it entirely.
template <std::size_t Capacity>
class Samples {
public:
    bool add(std::uint32_t value) noexcept {
        if (count_ >= Capacity) {
            return false;
        }
        std::size_t i = count_;
        while (i > 0u && values_[i - 1u] > value) {
            values_[i] = values_[i - 1u];
            --i;
        }
        values_[i] = value;
        ++count_;
        sum_ += value;
        return true;
    }

    void reset() noexcept {
        count_ = 0u;
        sum_ = 0u;
    }

    std::size_t count() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0u; }

    /// Sorted access. Out-of-range reads return 0 rather than running off the buffer.
    std::uint32_t at(std::size_t index) const noexcept {
        return index < count_ ? values_[index] : 0u;
    }

    std::uint32_t min() const noexcept { return count_ == 0u ? 0u : values_[0]; }
    std::uint32_t max() const noexcept { return count_ == 0u ? 0u : values_[count_ - 1u]; }

    /// 64-bit accumulator: a few hundred multi-second timings in microseconds would
    /// otherwise wrap a 32-bit sum.
    std::uint32_t mean() const noexcept {
        if (count_ == 0u) {
            return 0u;
        }
        return static_cast<std::uint32_t>(sum_ / count_);
    }

    std::uint32_t median() const noexcept {
        if (count_ == 0u) {
            return 0u;
        }
        const std::size_t mid = count_ / 2u;
        if (count_ % 2u == 1u) {
            return values_[mid];
        }
        // Averaged via the halves to avoid overflowing when both are near UINT32_MAX.
        const std::uint32_t lo = values_[mid - 1u];
        const std::uint32_t hi = values_[mid];
        return lo + (hi - lo) / 2u;
    }

    std::uint32_t p95() const noexcept {
        if (count_ == 0u) {
            return 0u;
        }
        // Nearest-rank: the smallest value at or above the 95th percentile position.
        std::size_t rank = (count_ * 95u + 99u) / 100u;
        if (rank == 0u) {
            rank = 1u;
        }
        if (rank > count_) {
            rank = count_;
        }
        return values_[rank - 1u];
    }

private:
    std::uint32_t values_[Capacity] = {};
    std::size_t count_ = 0u;
    std::uint64_t sum_ = 0u;
};

}  // namespace bench

#endif  // BENCH_SAMPLES_HPP
