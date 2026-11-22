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
// THE ENDS
//   The deque is a body that the code of its ends reads, as the Worker and
//   the Stealer of crossbeam-deque split one deque.  The body has no public
//   door other than its constructor and its capacity:
//
//     auto ends = mint_chase_lev_ends(deque);
//     ends.owner      the OwnerEnd: push_bottom and pop_bottom
//     ends.stealer    a StealerEnd: steal_top, and it copies
//
//   - The OwnerEnd is linear: it does not copy, and it ends through the drop
//     door of foundation/Linear.h.  It moves to another thread, so it is
//     Sendable, and it states not_shareable{}, so no two threads read one.
//     The mint takes the claim of the owner of the deque for the life of
//     the deque.  So a deque has one OwnerEnd, and a second mint of one
//     deque ends the process.
//   - The OwnerEnd is the one writer of bottom.  The cell of bottom names its
//     writer in its type (THE CELL OF AN OWNER of foundation/core/Atomic.h),
//     and only the OwnerEnd makes that proof, so no other code stores bottom.
//     Each deque of one type takes the same proof type.  The structure keeps
//     two deques apart: the Ref of an OwnerEnd is const, so each OwnerEnd
//     reaches only the deque of its mint.
//   - A StealerEnd copies, and two threads can use one at the same time: its
//     one part is a const Ref of a deque that two threads can write, so it is
//     Synchronized.  It holds steal_top and the snapshots.
//   - Each end refers to the deque, as a handle of a channel refers to its
//     channel, so the deque lives longer than each end.  The deque does not
//     move.
//
// T lives in an Atomic of the base, so it fits the word of one lock-free
// atomic instruction.  Anything larger is passed by an index into storage of
// its own.  A raw pointer moves an object to a thief thread with no transfer
// check, so DequeValue asks Sendable<T>, and a pointer fails it.

#include <fixy/Mutation.h>

#include <foundation/ChannelBinding.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/Transfer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The claim the deque makes that no lattice grades.  foundation/diag/RowHash.h
// folds the identity, so the deque takes a cache slot of its own rather than
// the zero a bare payload has.
namespace fixy::row_discipline {
struct chase_lev_deque;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// A value that a cell of the base holds with no lock, that a thief copies out
// with no destructor to run, and that moves to the thread of a thief
// (foundation/Transfer.h).
template <typename T>
concept DequeValue =
    ::foundation::core::AtomicValue<T> && std::is_trivially_destructible_v<T> && ::foundation::transfer::Sendable<T>;

// The role of the cell of bottom, which the proof of its writer names (THE
// CELL OF AN OWNER of foundation/core/Atomic.h).
struct DequeBottom final {};

template <DequeValue T, std::size_t Capacity>
class ChaseLevDeque;

// The brand of the Ref that each end of a deque of the type Deque holds.
template <class Deque>
struct DequeEndBrand final {};

// The two ends that mint_chase_lev_ends gives (THE ENDS).
template <DequeValue T, std::size_t Capacity>
struct DequeEnds;

// The one door to the ends of a deque (THE ENDS).  It takes the claim of the
// owner for the life of the deque, so a second mint of one deque ends the
// process.  Complexity: O(1).
template <DequeValue T, std::size_t Capacity>
[[nodiscard]] DequeEnds<T, Capacity> mint_chase_lev_ends(ChaseLevDeque<T, Capacity>& deque) noexcept;

template <DequeValue T, std::size_t Capacity>
class ChaseLevDeque : public ::foundation::Pinned<ChaseLevDeque<T, Capacity>> {
public:
    class OwnerEnd;
    class StealerEnd;

    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;
    using row_discipline = ::fixy::row_discipline::chase_lev_deque;
    using row_payload = T;

    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
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

    ChaseLevDeque() noexcept = default;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    using cell_index = ::foundation::core::Index<Capacity>;
    using bottom_proof = ::foundation::SoleWriterProof<OwnerEnd, DequeBottom>;
    using end_ref = ::foundation::core::Ref<ChaseLevDeque, DequeEndBrand<ChaseLevDeque>>;

    // The Ref of the deque that each end holds.
    [[nodiscard]] static end_ref lend_(ChaseLevDeque& deque) noexcept {
        return ::foundation::core::detail::RefDoor::lend<DequeEndBrand<ChaseLevDeque>>(deque);
    }

    // The acquire load of top pairs with the thieves' releasing CAS, so
    // the capacity check counts completed steals.  The release store of
    // the cell and the release store of bottom that advertises it keep
    // the cell ahead of bottom.  The owner is the one writer of bottom,
    // so its relaxed load of bottom reads its own last store.
    [[nodiscard]] bool push_bottom_(bottom_proof const& proof, T item) noexcept {
        const std::int64_t b = bottom_.load_owned(proof);
        const std::int64_t t = top_.get();
        if (b - t >= static_cast<std::int64_t>(Capacity)) [[unlikely]] {
            return false;
        }
        buffer_.with_element(cell_of_(b),
                             [item](::foundation::core::Atomic<T>& cell) noexcept { cell.store_release(item); });
        bottom_.store_release(proof, b + 1);
        return true;
    }

    // The owner is the one writer of bottom, and the relaxed loads of
    // bottom read its own last store.  A thief that takes the same cell
    // meets the owner at the seq_cst compare-and-swap of top.
    [[nodiscard]] ::foundation::core::Option<T> pop_bottom_(bottom_proof const& proof) noexcept {
        std::int64_t const b = bottom_.load_owned(proof) - 1;
        bottom_.store_release(proof, b);
        // The first of the two critical points.  It orders the bottom
        // decrement globally ahead of the load of top, so a thief
        // cannot slip in between and be missed.
        ::fixy::AtomicMonotonic<std::int64_t>::fence_seq_cst();
        // The thieves write top, so the owner is not its one writer, and the
        // read takes acquire order: a relaxed read is only for the own
        // variable of the thread.  The fence above carries the order of the
        // claim of the last item.  On x86-64 the acquire load is the same
        // plain load.
        std::int64_t t = top_.get();

        if (t > b) {
            bottom_.store_release(proof, b + 1);
            return ::foundation::core::none;
        }

        T const item = buffer_.at(cell_of_(b)).load_acquire();
        if (t < b) {
            // More than one element, so no thief can reach slot b.
            return ::foundation::core::Option<T>::some(item);
        }

        // One element left, and a thief may be claiming it.  A won CAS
        // takes it, a lost CAS means the thief already advanced top and
        // owns it.  Either way the deque ends up empty, so bottom is
        // restored on both paths.
        bool const has_won = top_.compare_exchange_advance_seq_cst(t, t + 1);
        bottom_.store_release(proof, b + 1);
        if (!has_won) return ::foundation::core::none;
        return ::foundation::core::Option<T>::some(item);
    }

    // Any thread.  Returns no value when the deque is empty and also when
    // another thief or the owner wins the race, which are not
    // distinguished.  This does not spin, so the caller retries.
    [[nodiscard]] ::foundation::core::Option<T> steal_top_() noexcept {
        std::int64_t t = top_.get();
        // The second critical point.  It keeps the bottom load from
        // moving ahead of the top load, where it would miss an owner
        // decrement that happened after that load.
        ::fixy::AtomicMonotonic<std::int64_t>::fence_seq_cst();
        // Acquire, to pair with the release store of bottom in push_bottom_,
        // and so observe the cell the owner just wrote.
        std::int64_t const b = bottom_.load_acquire();

        if (t >= b) {
            return ::foundation::core::none;
        }

        // Speculative: a lost CAS below means the owner or another
        // thief claimed slot t, and this value is discarded.  A thief does
        // not own the cell, so the read takes acquire order.
        T const item = buffer_.at(cell_of_(t)).load_acquire();

        if (!top_.compare_exchange_advance_seq_cst(t, t + 1)) {
            return ::foundation::core::none;
        }

        return ::foundation::core::Option<T>::some(item);
    }

    // Snapshots.  Both values can change before the call returns, so
    // these are for telemetry and polling decisions, never for a
    // correctness invariant in the caller.  The schedule of the owner and
    // the thieves decides the values, so each snapshot takes the bare tag
    // of Nondet.
    [[nodiscard]] std::size_t size_approx_(::foundation::effects::cap::Nondet /*nondet*/) const noexcept {
        const std::int64_t t = top_.get();
        const std::int64_t b = bottom_.load_acquire();
        const std::int64_t diff = b - t;
        return diff > 0 ? static_cast<std::size_t>(diff) : 0;
    }

    // The cell of a position: the low bits of the position, with no
    // compare.  A position is never negative, so its word keeps its value.
    [[nodiscard]] static constexpr cell_index cell_of_(std::int64_t position) noexcept {
        return cell_index::mask(static_cast<std::size_t>(position));
    }

    // Each member takes a cache line of its own.  The thieves' CAS
    // traffic on top must not invalidate the line the owner writes
    // bottom on, and neither counter must share a line with the cells.
    //
    // top only ever advances, and compare_exchange_advance is what
    // holds that at the type level.
    alignas(64)::fixy::AtomicMonotonic<std::int64_t> top_ = ::fixy::mint_atomic_monotonic<std::int64_t>(0);
    alignas(64)::foundation::core::Atomic<std::int64_t, bottom_proof> bottom_{0};
    alignas(64)::foundation::core::FixedArray<::foundation::core::Atomic<T>, Capacity> buffer_{};
    // The claim of the owner, which the mint takes for the life of the deque.
    ::foundation::EndpointClaim owner_claim_{};
};

// The end of the owner of a deque (THE ENDS).  Its Ref is const, so it
// reaches only the deque of its mint, and each proof of bottom that it makes
// writes that deque.
template <DequeValue T, std::size_t Capacity>
class [[nodiscard]][[= ::foundation::linear::linear{}]]
                   [[= ::foundation::transfer::not_shareable{}]] ChaseLevDeque<T, Capacity>::OwnerEnd {
    end_ref const deque_;

    // Takes the claim of the owner for the life of the deque, so a second
    // OwnerEnd of one deque ends the process here.
    explicit OwnerEnd(ChaseLevDeque& deque) noexcept : deque_{lend_(deque)} {
        deque_->owner_claim_.take("a second OwnerEnd of one work-stealing deque.  Mint the ends of a deque one time.");
    }

    friend DequeEnds<T, Capacity> mint_chase_lev_ends<T, Capacity>(ChaseLevDeque& deque) noexcept;

    // The proof of the one writer of bottom.  Only an OwnerEnd makes it, and
    // it lives for one call.
    [[nodiscard]] static constexpr bottom_proof prove_bottom_() noexcept { return bottom_proof{}; }

public:
    OwnerEnd(OwnerEnd const&) =
        delete("an OwnerEnd is the one writer of the bottom of its deque, and a copy is a second");
    OwnerEnd& operator=(OwnerEnd const&) = delete("an OwnerEnd names one deque for its life");
    // A move gives the deque to the target.  The static rule use-after-move
    // refuses a use of the moved-from local, as for a Permission.
    OwnerEnd(OwnerEnd&& other) noexcept : deque_{other.deque_} {}
    OwnerEnd& operator=(OwnerEnd&&) = delete("an OwnerEnd names one deque for its life");
    ~OwnerEnd() = default;

    // Pushes an item at the bottom, or gives false when the deque holds
    // Capacity items.  Complexity: O(1).
    [[nodiscard]] bool push_bottom(T item) noexcept { return deque_->push_bottom_(prove_bottom_(), item); }

    // Takes the last item that the owner pushed, or no value when the deque
    // is empty or a thief won the last item.  Complexity: O(1).
    [[nodiscard]] ::foundation::core::Option<T> pop_bottom() noexcept { return deque_->pop_bottom_(prove_bottom_()); }

    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return deque_->size_approx_(nondet);
    }

    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return deque_->size_approx_(nondet) == 0;
    }
};

// The end of a thief (THE ENDS).  It copies, and two threads can use one at
// the same time.
template <DequeValue T, std::size_t Capacity>
class [[nodiscard]] ChaseLevDeque<T, Capacity>::StealerEnd {
    end_ref const deque_;

    explicit StealerEnd(ChaseLevDeque& deque) noexcept : deque_{lend_(deque)} {}

    friend DequeEnds<T, Capacity> mint_chase_lev_ends<T, Capacity>(ChaseLevDeque& deque) noexcept;

public:
    StealerEnd(StealerEnd const& other) noexcept : deque_{other.deque_} {}
    StealerEnd& operator=(StealerEnd const&) = delete("a StealerEnd names one deque for its life");
    ~StealerEnd() = default;

    // Takes the oldest item, or no value when the deque is empty or another
    // thread won the item.  Complexity: O(1).
    [[nodiscard]] ::foundation::core::Option<T> steal_top() const noexcept { return deque_->steal_top_(); }

    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return deque_->size_approx_(nondet);
    }

    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        return deque_->size_approx_(nondet) == 0;
    }
};

// A binding takes the two ends by name.  The OwnerEnd is linear, so its
// holder ends it through the drop door.
template <DequeValue T, std::size_t Capacity>
struct [[nodiscard]] DequeEnds {
    typename ChaseLevDeque<T, Capacity>::OwnerEnd owner;
    typename ChaseLevDeque<T, Capacity>::StealerEnd stealer;
};

// The OwnerEnd comes first in the braced list, so the claim of the owner is
// taken before the StealerEnd exists.
template <DequeValue T, std::size_t Capacity>
[[nodiscard]] DequeEnds<T, Capacity> mint_chase_lev_ends(ChaseLevDeque<T, Capacity>& deque) noexcept {
    using Deque = ChaseLevDeque<T, Capacity>;
    return DequeEnds<T, Capacity>{typename Deque::OwnerEnd{deque}, typename Deque::StealerEnd{deque}};
}

}  // namespace fixy::concurrent
