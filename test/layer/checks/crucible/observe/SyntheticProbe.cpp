// The compile-time checks of crucible/observe/SyntheticProbe.h.

#include <crucible/observe/SyntheticProbe.h>

namespace crucible::observe {

namespace detail {

// The stats grid and the metric ids index a kind by the position of its bit.
// Each kind is one bit, and that bit is at the position of the kind in
// all_transport_probe_kinds, so no index is outside the grid.  O(kinds).
[[nodiscard]] consteval bool each_probe_bit_is_its_position() noexcept {
    for (std::size_t position = 0; position < all_transport_probe_kinds.size(); ++position) {
        auto const kind = all_transport_probe_kinds[position];
        if (!std::has_single_bit(static_cast<std::uint32_t>(kind)) || transport_probe_index(kind) != position) {
            return false;
        }
    }
    return true;
}

static_assert(each_probe_bit_is_its_position(),
              "Each TransportProbeKind must be one bit at the position of its entry in "
              "all_transport_probe_kinds, because the stats grid and the metric ids use that position.");

static_assert(alignof(AtomicProbeStats) >= 64, "AtomicProbeStats must be cache-line-aligned so that adjacent "
                                               "(peer, transport_kind) slots in the SyntheticProbeRunner "
                                               "stats_ grid land on distinct lines under concurrent recording");
static_assert(sizeof(AtomicProbeStats) >= 64, "AtomicProbeStats occupies a full cache line. The trailing padding "
                                              "is intentional.");

// A standard library may substitute mutex-backed operations where the target
// lacks the intrinsic, silently putting a lock inside every recorded outcome.
// Refuse to build instead.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target.");
static_assert(std::atomic<std::uint8_t>::is_always_lock_free, "std::atomic<uint8_t> must be lock-free on this target.");

}  // namespace detail

static_assert(std::is_base_of_v<::foundation::Pinned<SyntheticProbeRunner<1>>, SyntheticProbeRunner<1>>);
static_assert(!CtxFitsSyntheticProbeMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsSyntheticProbeMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSyntheticProbeRecord<::fixy::HotFgCtx>);
static_assert(CtxFitsSyntheticProbeRecord<::fixy::BgDrainCtx>);

}  // namespace crucible::observe
