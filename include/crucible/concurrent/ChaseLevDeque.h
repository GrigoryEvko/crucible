#pragma once

// Single-owner, multi-thief lock-free work-stealing deque.  The owner
// pushes and pops at the bottom, which is LIFO and takes no CAS on the
// common path.  Thieves take from the top, so they contend with each
// other and with the owner only over the last element.
//
// That last element is the whole difficulty.  Without sequential
// consistency at two points, store-buffer reordering lets the owner and
// a thief both observe the pre-decrement state, both return the same
// item, and one item is lost.  The two points are:
//
//   1. In pop_bottom, after the owner decrements bottom.  The fence
//      orders that store globally ahead of the following load of top,
//      so a thief that already read top loses the CAS below.
//   2. In steal_top, between the load of top and the load of bottom.
//      The fence stops the bottom load from moving ahead of the top
//      load, where it would miss the owner's claim.
//
// Both CAS sites on top are seq_cst on success, so the total order
// admits exactly one winner.
//
// T lives in std::atomic<T> and must be always-lock-free, which in
// practice caps it at the width of the target's widest atomic
// instruction.  Anything larger is passed as a pointer.

#include <crucible/Platform.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/_Pinned.h>
#include <crucible/fixy/Hw.h>
#include <crucible/fixy/Dim.h>
#include <crucible/algebra/lattices/_BarrierStrengthLattice.h>

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace crucible::concurrent {

// The deque emits two fence strengths: a release fence in push_bottom
// and a seq_cst fence at each of the two critical points.  A region
// holding both is graded at their lattice join, so SeqCst is the grant
// that characterizes the deque.  The grant is pinned on the portable
// compiler family, because the fences are std::atomic_thread_fence and
// the target instruction is the compiler's choice.
namespace chaselev_hw {

namespace fh = ::crucible::fixy::hw;
namespace fgh = ::crucible::fixy::grant::hw;
using BSL = ::crucible::algebra::lattices::BarrierStrengthLattice;
using BS = ::crucible::fixy::hw::BarrierStrength;

using ActiveBarrierGrant = fh::barrier_compiler_seqcst;

static_assert(::crucible::fixy::grant::IsGrantTag<ActiveBarrierGrant>,
              "the active barrier grant must be a well-formed grant tag");
static_assert(::crucible::fixy::grant::which_dim_v<ActiveBarrierGrant>
                  == ::crucible::fixy::dim::DimensionAxis::BarrierStrength,
              "the barrier grant routes to the BarrierStrength axis");

// Compared through the lattice ordering rather than the underlying
// values, so a renumbering of the enum cannot silently invert this.
static_assert(BSL::leq(BS::ReleaseStore, BS::SeqCst), "the release fence is weaker than the seq_cst fence");
static_assert(BSL::join(BS::ReleaseStore, BS::SeqCst) == BS::SeqCst,
              "the deque's barrier grade is the join of its two "
              "fence strengths — SeqCst dominates the release fence");

}  // namespace chaselev_hw

template <typename T>
concept DequeValue =
    std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> && std::atomic<T>::is_always_lock_free;

template <DequeValue T, std::size_t Capacity>
class ChaseLevDeque : public safety::Pinned<ChaseLevDeque<T, Capacity>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;

    static_assert(std::has_single_bit(Capacity), "Capacity must be a power of two");
    // The ABA-safety argument assumes top and bottom index distinct
    // cells whenever the deque is non-empty.  At Capacity 1 the mask is
    // zero, so owner push, owner pop and thief steal all touch cell 0
    // whatever top and bottom hold, and the argument's separability
    // premise is false.  The published proof leaves that premise
    // implicit, and this bound makes it explicit.
    static_assert(Capacity >= 2, "Capacity must be >= 2 — the ABA-safety argument "
                                 "assumes top and bottom index distinct cells, and at "
                                 "Capacity 1 a zero mask collapses every access to cell 0");
    static_assert(Capacity <= (std::size_t{1} << 30), "Capacity must fit in 31-bit signed range "
                                                      "(top/bottom are int64; Capacity <= 2^30 keeps the "
                                                      "subtraction safe under any deque state)");

private:
    static constexpr int64_t MASK = static_cast<int64_t>(Capacity - 1);

public:
    ChaseLevDeque() noexcept = default;

    // Owner only.  The acquire load of top pairs with the thieves'
    // releasing CAS, so the capacity check counts completed steals.
    // The release fence makes the cell store visible before the bottom
    // store that advertises it, which is why both stores are relaxed.
    [[nodiscard]] bool push_bottom(T item) noexcept {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        const int64_t t = top_.get();
        if (b - t >= static_cast<int64_t>(Capacity)) [[unlikely]] {
            return false;
        }
        buffer_[b & MASK].store(item, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return true;
    }

    // Owner only.
    [[nodiscard]] std::optional<T> pop_bottom() noexcept {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        // The first of the two critical points.  It orders the bottom
        // decrement globally ahead of the load of top, so a thief
        // cannot slip in between and be missed.
        safety::AtomicMonotonic<int64_t>::fence_seq_cst();
        // A relaxed load suffices: the fence above already carries the
        // cross-thread ordering.
        int64_t t = top_.peek_relaxed();

        if (t > b) {
            bottom_.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }

        const T item = buffer_[b & MASK].load(std::memory_order_relaxed);
        if (t < b) {
            // More than one element, so no thief can reach slot b.
            return item;
        }

        // One element left, and a thief may be claiming it.  A won CAS
        // takes it, a lost CAS means the thief already advanced top and
        // owns it.  Either way the deque ends up empty, so bottom is
        // restored on both paths.
        if (!top_.compare_exchange_advance(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
            bottom_.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
        bottom_.store(b + 1, std::memory_order_relaxed);
        return item;
    }

    // Any thread.  Returns nullopt when the deque is empty and also
    // when another thief or the owner wins the race, which are not
    // distinguished.  This does not spin, so the caller retries.
    [[nodiscard]] std::optional<T> steal_top() noexcept {
        int64_t t = top_.get();
        // The second critical point.  It keeps the bottom load from
        // moving ahead of the top load, where it would miss an owner
        // decrement that happened after that load.
        safety::AtomicMonotonic<int64_t>::fence_seq_cst();
        // Acquire, to pair with the release fence in push_bottom and
        // so observe the cell the owner just wrote.
        const int64_t b = bottom_.load(std::memory_order_acquire);

        if (t >= b) {
            return std::nullopt;
        }

        // Speculative: a lost CAS below means the owner or another
        // thief claimed slot t, and this value is discarded.
        const T item = buffer_[t & MASK].load(std::memory_order_relaxed);

        if (!top_.compare_exchange_advance(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
            return std::nullopt;
        }

        return item;
    }

    // Snapshots.  Both values can change before the call returns, so
    // these are for telemetry and polling decisions, never for a
    // correctness invariant in the caller.

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const int64_t t = top_.get();
        const int64_t b = bottom_.load(std::memory_order_acquire);
        const int64_t diff = b - t;
        return diff > 0 ? static_cast<std::size_t>(diff) : 0;
    }

    [[nodiscard]] bool empty_approx() const noexcept { return size_approx() == 0; }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // Each member takes a cache line of its own.  The thieves' CAS
    // traffic on top must not invalidate the line the owner writes
    // bottom on, and neither counter must share a line with the cells.
    //
    // top only ever advances, and compare_exchange_advance is what
    // holds that at the type level.
    alignas(64) safety::AtomicMonotonic<int64_t> top_{0};
    alignas(64) std::atomic<int64_t> bottom_{0};
    alignas(64) std::array<std::atomic<T>, Capacity> buffer_{};
};

}  // namespace crucible::concurrent
