// ═══════════════════════════════════════════════════════════════════
// prop_intervals_pairwise_disjoint.cpp — differential-oracle fuzzer for
// foundation::decide::intervals_pairwise_disjoint
// (foundation/contracts/Decide.h).
//
// The memory plan checks its live set with intervals_pairwise_disjoint, so a
// wrong answer passes a plan whose slots overlap.  The production form
// tests each pair of intervals.  The oracle reaches the answer by a
// different route: it refuses a malformed interval, drops each empty
// interval, sorts the remaining intervals by their low end, and requires
// that each interval ends at or before the start of the next one.  A
// divergence on any input is a defect of one of the two.
//
// The ends come from a narrow range, so overlaps, touching ends, empty
// intervals and malformed intervals all occur often.  Half of the rounds lay
// the intervals end to end with gaps, so the accepting answer is common, and
// such a set must pass.  Half of those rounds reverse the order of the
// intervals, because the answer must not depend on the order.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <fixy/Core.h>
#include <foundation/contracts/Decide.h>

#include <cstdint>

namespace {

using crucible::fuzz::prop::Rng;
using Interval = ::foundation::decide::Interval<std::uint64_t>;

// The largest count of intervals in one set.
constexpr std::uint32_t kMaxIntervals = 8;

// The seed of the stream, and the count of the sets that one run checks.
constexpr std::uint64_t kSeed = 0xC001C0DE000C000CULL;
constexpr std::uint64_t kCases = 10000;

using Intervals = ::fixy::FixedList<Interval, kMaxIntervals>;

struct IntervalSet {
    Intervals intervals{};
    bool is_disjoint_by_construction = false;
};

// The oracle.  The sort is an insertion sort, which is quadratic and
// correct by inspection for at most kMaxIntervals intervals.  Complexity:
// O(n^2) for n intervals.
[[nodiscard]] bool are_disjoint_by_sweep(::fixy::View<Interval const> intervals) noexcept {
    Intervals occupied{};
    for (Interval const& interval : intervals) {
        if (interval.hi < interval.lo) return false;
        if (interval.lo == interval.hi) continue;
        Interval carried = interval;
        // Each interval of occupied with a larger low end moves one place
        // to the right, and carried takes the free place.
        for (Interval& placed : occupied.view()) {
            if (carried.lo < placed.lo) {
                const Interval moved = placed;
                placed = carried;
                carried = moved;
            }
        }
        static_cast<void>(occupied.append(carried).expect("occupied holds at most the intervals of the set"));
    }
    std::uint64_t end_of_last = 0;
    bool has_last = false;
    for (Interval const& interval : occupied.view()) {
        if (has_last && interval.lo < end_of_last) return false;
        end_of_last = interval.hi;
        has_last = true;
    }
    return true;
}

// The set of a round, with the intervals in reverse order.
[[nodiscard]] Intervals reversed(Intervals const& intervals) noexcept {
    Intervals result{};
    for (Interval const& interval : intervals.view().reverse()) {
        static_cast<void>(result.append(interval).expect("the reverse holds the same count"));
    }
    return result;
}

}  // namespace

// The run reads no argument of the command line.  Its seed and its count of
// cases are fixed, so each run checks the same sets.
int main() {
    using namespace crucible::fuzz::prop;
    using ::foundation::decide::intervals_pairwise_disjoint;

    const Config cfg{.seed = kSeed, .iterations = kCases, .verbose = false};

    return run(
        "intervals_pairwise_disjoint", cfg,
        // ── Generator: intervals end to end, or intervals with free ends ──
        [](Rng& rng) noexcept -> IntervalSet {
            IntervalSet set{};
            const std::uint32_t count = rng.next_below(kMaxIntervals + 1u);
            set.is_disjoint_by_construction = rng.next_below(2) == 1u;
            const bool is_reversed = rng.next_below(2) == 1u;
            std::uint64_t cursor = 0;
            for (std::uint32_t index = 0; index < count; ++index) {
                Interval interval{};
                if (set.is_disjoint_by_construction) {
                    interval.lo = cursor + rng.next_below(4);
                    interval.hi = interval.lo + rng.next_below(8);
                    cursor = interval.hi;
                } else {
                    interval.lo = rng.next_below(128);
                    interval.hi = rng.next_below(128);
                }
                static_cast<void>(set.intervals.append(interval).expect("a set holds at most kMaxIntervals intervals"));
            }
            if (set.is_disjoint_by_construction && is_reversed) set.intervals = reversed(set.intervals);
            return set;
        },
        // ── Property: the two answers agree, and a laid set passes ──
        [](IntervalSet const& set) noexcept -> bool {
            const bool answer = intervals_pairwise_disjoint(set.intervals.view());
            if (answer != are_disjoint_by_sweep(set.intervals.view())) return false;
            return !set.is_disjoint_by_construction || answer;
        });
}
