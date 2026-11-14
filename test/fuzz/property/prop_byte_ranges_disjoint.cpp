// ═══════════════════════════════════════════════════════════════════
// prop_byte_ranges_disjoint.cpp — differential-oracle fuzzer for
// crucible::are_byte_ranges_disjoint (crucible/MerkleDag.h).
//
// The memory plan checks its live set with are_byte_ranges_disjoint, so a
// wrong answer passes a plan whose slots overlap.  The production form
// tests each pair of ranges.  The oracle reaches the answer by a different
// route: it refuses a malformed range, drops each empty range, sorts the
// remaining ranges by their low end, and requires that each range ends at
// or before the start of the next one.  A divergence on any input is a
// defect of one of the two.
//
// The ends come from a narrow range, so overlaps, touching ends, empty
// ranges and malformed ranges all occur often.  Half of the rounds lay the
// ranges end to end with gaps, so the accepting answer is common, and such
// a set must pass.  Half of those rounds reverse the order of the ranges,
// because the answer must not depend on the order.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/MerkleDag.h>
#include <fixy/Core.h>

#include <cstdint>

namespace {

using crucible::ByteRange;
using crucible::fuzz::prop::Rng;

// The largest count of ranges in one set.
constexpr std::uint32_t kMaxRanges = 8;

// The seed of the stream, and the count of the sets that one run checks.
constexpr std::uint64_t kSeed = 0xC001C0DE000C000CULL;
constexpr std::uint64_t kCases = 10000;

using Ranges = ::fixy::FixedList<ByteRange, kMaxRanges>;

struct RangeSet {
    Ranges ranges{};
    bool is_disjoint_by_construction = false;
};

// The oracle.  The sort is an insertion sort, which is quadratic and
// correct by inspection for at most kMaxRanges ranges.  Complexity:
// O(n^2) for n ranges.
[[nodiscard]] bool are_disjoint_by_sweep(::fixy::View<ByteRange const> ranges) noexcept {
    Ranges occupied{};
    for (ByteRange const& range : ranges) {
        if (range.hi < range.lo) return false;
        if (range.lo == range.hi) continue;
        ByteRange carried = range;
        // Each range of occupied with a larger low end moves one place
        // to the right, and carried takes the free place.
        for (ByteRange& placed : occupied.view()) {
            if (carried.lo < placed.lo) {
                const ByteRange moved = placed;
                placed = carried;
                carried = moved;
            }
        }
        static_cast<void>(occupied.push(carried).expect("occupied holds at most the ranges of the set"));
    }
    std::uint64_t end_of_last = 0;
    bool has_last = false;
    for (ByteRange const& range : occupied.view()) {
        if (has_last && range.lo < end_of_last) return false;
        end_of_last = range.hi;
        has_last = true;
    }
    return true;
}

// The set of a round, with the ranges in reverse order.
[[nodiscard]] Ranges reversed(Ranges const& ranges) noexcept {
    Ranges result{};
    for (ByteRange const& range : ranges.view().reverse()) {
        static_cast<void>(result.push(range).expect("the reverse holds the same count"));
    }
    return result;
}

}  // namespace

// The run reads no argument of the command line.  Its seed and its count of
// cases are fixed, so each run checks the same sets.
int main() {
    using namespace crucible::fuzz::prop;
    using crucible::are_byte_ranges_disjoint;

    const Config cfg{.seed = kSeed, .iterations = kCases, .verbose = false};

    return run(
        "byte_ranges_disjoint", cfg,
        // ── Generator: ranges end to end, or ranges with free ends ──
        [](Rng& rng) noexcept -> RangeSet {
            RangeSet set{};
            const std::uint32_t count = rng.next_below(kMaxRanges + 1u);
            set.is_disjoint_by_construction = rng.next_below(2) == 1u;
            const bool is_reversed = rng.next_below(2) == 1u;
            std::uint64_t cursor = 0;
            for (std::uint32_t index = 0; index < count; ++index) {
                ByteRange range{};
                if (set.is_disjoint_by_construction) {
                    range.lo = cursor + rng.next_below(4);
                    range.hi = range.lo + rng.next_below(8);
                    cursor = range.hi;
                } else {
                    range.lo = rng.next_below(128);
                    range.hi = rng.next_below(128);
                }
                static_cast<void>(set.ranges.push(range).expect("a set holds at most kMaxRanges ranges"));
            }
            if (set.is_disjoint_by_construction && is_reversed) set.ranges = reversed(set.ranges);
            return set;
        },
        // ── Property: the two answers agree, and a laid set passes ──
        [](RangeSet const& set) noexcept -> bool {
            const bool answer = are_byte_ranges_disjoint(set.ranges.view());
            if (answer != are_disjoint_by_sweep(set.ranges.view())) return false;
            return !set.is_disjoint_by_construction || answer;
        });
}
