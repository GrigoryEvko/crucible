// The compile-time checks of crucible/canopy/VectorClock.h.

#include <crucible/canopy/VectorClock.h>

namespace crucible::canopy {

namespace detail::vector_clock_order_check {

// The snapshot order agrees with the lattice on every pair of four clocks
// that the lattice itself built: the empty history, one event at each of
// two processes, and their join.
[[nodiscard]] consteval bool snapshot_order_agrees_with_lattice() noexcept {
    using HB = ::foundation::algebra::lattices::HappensBeforeLattice<3>;
    using Snap = VectorClockSnapshot<3>;
    const auto first = HB::successor_at(HB::bottom(), 0);
    const auto second = HB::successor_at(HB::bottom(), 1);
    const std::array<HB::element_type, 4> clocks{HB::bottom(), first, second, HB::join(first, second)};
    for (const auto& lhs : clocks) {
        for (const auto& rhs : clocks) {
            const Snap left = Snap::from_lattice_clock(lhs);
            const Snap right = Snap::from_lattice_clock(rhs);
            if ((left <=> right) != (lhs <=> rhs)) return false;
            if (left.happens_before(right) != HB::happens_before(lhs, rhs)) return false;
            if (left.concurrent_with(right) != HB::is_concurrent(lhs, rhs)) return false;
            if (left.comparable_with(right) != HB::comparable(lhs, rhs)) return false;
        }
    }
    return true;
}

static_assert(snapshot_order_agrees_with_lattice(),
              "VectorClockSnapshot must order its counts as HappensBeforeLattice orders clocks.");

}  // namespace detail::vector_clock_order_check

static_assert(!std::is_constructible_v<VectorClock<1>, VectorClockNodeIndex<1>>);
static_assert(!std::is_copy_constructible_v<VectorClock<1>>);
static_assert(!std::is_move_constructible_v<VectorClock<1>>);
static_assert(alignof(VectorClock<1>) == 64);

// Every peer handler reads and writes these entries at the same time.  On an
// ISA that lacks the required instruction the standard library substitutes a
// mutex-backed atomic without saying so, and that mutex would serialize every
// peer against every other peer, which defeats the merge design.  The build
// refuses such a target instead of regressing quietly.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target");
static_assert(std::is_trivially_copyable_v<VectorClockSnapshot<4>>);
static_assert(std::is_trivially_destructible_v<VectorClockSnapshot<4>>);
static_assert(sizeof(VectorClockSnapshot<4>) == 4 * sizeof(std::uint64_t));

}  // namespace crucible::canopy
