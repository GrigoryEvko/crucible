#pragma once

// One producer thread is the sole writer of head_, of own_head_, of
// cached_tail_ and of the slots in [tail, head).  One consumer thread is
// the sole writer of tail_, of own_tail_, of cached_head_ and of
// deferred_pops_, and the sole reader of those slots.  A second producer or a second consumer corrupts
// the ring.  The checked calls compare the published index with the copy of
// their side at each publish, so a second writer of one side ends the
// process at the next publish of the first writer.  A new owner of a side
// reads the copies of that side only after the last owner of the side
// ended, with a synchronization between the two, such as the claim of a role
// of fixy/concurrent/PermissionedChannel.h.
//
// THE CACHES
//   Each side keeps the last value of the index of the other side that it
//   loaded, on a cache line that only it touches.  A push reads the tail
//   of the consumer only when the cached tail says that the ring is full,
//   and a pop reads the head of the producer only when the cached head says
//   that the ring is empty.  So while the ring has room and items, a push
//   and a pop move only the line of their own index and the lines of the
//   cells between the two cores.  A cache is never ahead of the index that
//   it copies, because each index only grows, so a stale cache can only
//   report less room or fewer items than the ring holds.  A side reports a
//   full ring or an empty ring only after a fresh acquire load of the index
//   of the other side.  That load and its update of the cache are in a cold
//   function out of line, so the path of a push with room and a pop with an
//   item holds no call.
//
// THE OWN INDEX
//   Each side also keeps its own index on the same private line, and it
//   reads its position there.  The other side polls the published index, so
//   each load of that line by its writer can wait for a transfer of the
//   line.  A checked publish still loads the published index one time, for
//   its check.
//
// THE PROVEN FORM
//   A ring with an Owner also gives try_push_proven and try_pop_proven.
//   Each takes a SoleWriterProof of foundation/SoleWriterProof.h for its side, and only
//   Owner makes one: the type proves the one writer, so the publish loads
//   nothing from the published index (advance_sole_writer_by_proof).  A ring
//   with no Owner has no proven form.  The two forms keep the same copies,
//   so one ring can take each form on each call.  The two proven calls
//   always inline into their caller.  In the producer loop of the scaling
//   bench, GCC kept the push as an out-of-line call, and that call decreased
//   the throughput in one L3 cache by approximately 14%.  The checked calls
//   showed no such loss, so the compiler decides for them.
//
// THE DEFERRED TAIL
//   try_pop_deferred moves only the copy of the tail of the consumer, and it
//   counts its pops on the private line of the consumer.  The pop that makes
//   the count reach or pass its Interval publishes the tail with one release
//   store, and publish_tail publishes it at any time.  Each publish checks
//   the published tail against the copy minus the count, so a second
//   consumer ends the process at the next publish.  The producer sees the
//   free cells of a pop only after the publish of the tail.  When the ring is
//   full, a pop with a publish moves the line of the tail two times, and the
//   push after it reads that line again.  So a consumer that pops a full ring
//   publishes one time for each Interval pops, and the producer then reads
//   the line of the tail one time for Interval free cells.  This is the batch
//   update of the read index of MCRingBuffer (Lee, Bu and Chandranmenon,
//   2010).  A snapshot reads the published tail, so it counts the deferred
//   pops as items of the ring.  A pop of another form takes its own copy of
//   the tail as the published tail.  So after a deferred pop, publish_tail
//   runs before each pop of another form, or the check of that pop ends the
//   process.  A deferred pop can follow a pop of another form.

#include <fixy/Mutation.h>
#include <fixy/concurrent/RingValue.h>

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct spsc_ring;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The roles of the two proofs of the one writer of a ring (THE PROVEN FORM).
struct SpscProducerRole final {};
struct SpscConsumerRole final {};

template <RingValue T, std::size_t Capacity, class Owner = void>
class SpscRing : public ::foundation::Pinned<SpscRing<T, Capacity, Owner>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;
    using row_discipline = ::fixy::row_discipline::spsc_ring;
    using row_payload = T;
    using owner_type = Owner;
    // The proofs of the two sides, which only Owner makes (THE PROVEN FORM).
    using producer_proof = ::fixy::SoleWriterProof<Owner, SpscProducerRole>;
    using consumer_proof = ::fixy::SoleWriterProof<Owner, SpscConsumerRole>;

    static_assert(std::has_single_bit(Capacity), "fixy::concurrent::SpscRing<T, Capacity>: Capacity must be a power "
                                                 "of two.  The position-to-slot map is a bitwise AND with "
                                                 "Capacity - 1, which is the index modulo Capacity only when "
                                                 "Capacity is a power of two.");
    static_assert(Capacity > 0, "fixy::concurrent::SpscRing<T, Capacity>: Capacity must be greater than zero.  A "
                                "ring with no slots is permanently full and permanently empty at the same time.");

private:
    static constexpr std::uint64_t MASK = std::uint64_t{Capacity - 1};

public:
    SpscRing() noexcept = default;

    // The slot at (h & MASK) is free once h - t < Capacity holds: the
    // consumer last read that slot at tail position h - Capacity and
    // has moved past it.  So (h & MASK) and (t & MASK) never name the
    // same slot while both are in flight, and the non-atomic cell write
    // races with nothing.  The cached tail is at most the tail, so the
    // test on the cached tail implies the test on the tail (THE CACHES).
    [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
        const std::uint64_t h = own_head_.get();
        if (h - cached_tail_.get() >= Capacity) [[unlikely]] {
            if (free_after_tail_load_() == 0) return false;
        }
        buffer_[h & MASK] = item;
        head_.advance_sole_writer_from(prove_head_writer_(), h, h + 1);
        own_head_.advance(h + 1);
        return true;
    }

    // The push of the one producer that the proof names (THE PROVEN FORM).
    [[nodiscard]] CRUCIBLE_HOT bool try_push_proven(producer_proof const& /*proof*/, const T& item) noexcept
        requires(!std::is_void_v<Owner>)
    {
        const std::uint64_t h = own_head_.get();
        if (h - cached_tail_.get() >= Capacity) [[unlikely]] {
            if (free_after_tail_load_() == 0) return false;
        }
        buffer_[h & MASK] = item;
        head_.advance_sole_writer_by_proof(prove_head_writer_(), own_head_, h + 1);
        return true;
    }

    // The oldest item, or no value when the ring is empty.  The cached head
    // is at least the tail, and the compare is ordered, so a cache at or
    // behind the tail reads the head again.  The acquire load that gave the
    // cached head synchronizes with the release of the producer that
    // published the cell, so the cell read below sees the item.
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop() noexcept {
        const std::uint64_t t = own_tail_.get();
        if (cached_head_.get() <= t) [[unlikely]] {
            if (items_after_head_load_() == 0) return ::foundation::core::none;
        }
        const T result = buffer_[t & MASK];
        tail_.advance_sole_writer_from(prove_tail_writer_(), t, t + 1);
        own_tail_.advance(t + 1);
        return ::foundation::core::Option<T>::some(result);
    }

    // The pop of the one consumer that the proof names (THE PROVEN FORM).
    [[nodiscard]] CRUCIBLE_HOT ::foundation::core::Option<T> try_pop_proven(consumer_proof const& /*proof*/) noexcept
        requires(!std::is_void_v<Owner>)
    {
        const std::uint64_t t = own_tail_.get();
        if (cached_head_.get() <= t) [[unlikely]] {
            if (items_after_head_load_() == 0) return ::foundation::core::none;
        }
        const T result = buffer_[t & MASK];
        tail_.advance_sole_writer_by_proof(prove_tail_writer_(), own_tail_, t + 1);
        return ::foundation::core::Option<T>::some(result);
    }

    // The pop of THE DEFERRED TAIL.  It reads the cell as try_pop does, and it
    // moves only the copy of the tail.  It publishes the tail when the count
    // of the deferred pops reaches Interval or passes it, so the producer
    // waits for at most Interval - 1 pops that the consumer took.  A count
    // passes Interval only after pops with a larger Interval.  An Interval of
    // 1 publishes at each pop, as try_pop does.
    template <std::size_t Interval>
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop_deferred() noexcept {
        static_assert(Interval > 0 && Interval <= Capacity,
                      "fixy::concurrent::SpscRing::try_pop_deferred: the interval of the publish is at least one pop "
                      "and at most the capacity.  A larger interval would hold free cells of a full ring until the "
                      "ring is empty.");
        const std::uint64_t t = own_tail_.get();
        if (cached_head_.get() <= t) [[unlikely]] {
            if (items_after_head_load_() == 0) return ::foundation::core::none;
        }
        const T result = buffer_[t & MASK];
        own_tail_.advance(t + 1);
        const std::uint64_t deferred = deferred_pops_ + 1;
        if (deferred >= Interval) [[unlikely]] {
            tail_.advance_sole_writer_from(prove_tail_writer_(), t + 1 - deferred, t + 1);
            deferred_pops_ = 0;
        } else {
            deferred_pops_ = deferred;
        }
        return ::foundation::core::Option<T>::some(result);
    }

    // Publishes the copy of the tail, when a deferred pop moved it since the
    // last publish (THE DEFERRED TAIL).  The producer then sees each cell that
    // the consumer freed.  Only the consumer calls it.
    void publish_tail() noexcept {
        if (deferred_pops_ == 0) return;
        const std::uint64_t t = own_tail_.get();
        tail_.advance_sole_writer_from(prove_tail_writer_(), t - deferred_pops_, t);
        deferred_pops_ = 0;
    }

    // The bulk copies go through std::memcpy rather than a std::simd
    // store loop.  The compiler lowers a bounded contiguous memcpy to
    // the widest store the target ISA offers, so the explicit vector
    // form buys nothing and adds a header dependency.  The View of the
    // caller gives its first element through the door of the base.  The
    // push reads the tail again only when the cached tail leaves less room
    // than the batch asks, so it takes the same count as a read of the tail
    // at that point (THE CACHES).  The batch is visible to the consumer at
    // the one release store of the head.
    [[nodiscard, gnu::hot]] std::size_t try_push_batch(::foundation::core::View<T const> items) noexcept {
        if (items.size() == 0) [[unlikely]]
            return 0;
        const std::uint64_t h = own_head_.get();
        std::uint64_t free_slots = Capacity - (h - cached_tail_.get());
        if (free_slots < items.size()) [[unlikely]] {
            free_slots = free_after_tail_load_();
        }
        if (free_slots == 0) [[unlikely]]
            return 0;
        const std::size_t count = free_slots < items.size() ? free_slots : items.size();

        const std::uint64_t start_pos = h & MASK;
        const std::size_t room_to_end = Capacity - start_pos;
        const std::size_t first = room_to_end < count ? room_to_end : count;
        const std::size_t second = count - first;

        T const* const source = first_of_(::foundation::core::detail::ViewDoor::key(), items);
        std::memcpy(buffer_.data() + start_pos, source, first * sizeof(T));
        if (second > 0) [[unlikely]] {
            std::memcpy(buffer_.data(), source + first, second * sizeof(T));
        }

        head_.advance_sole_writer_from(prove_head_writer_(), h, h + count);
        own_head_.advance(h + count);
        return count;
    }

    // The pop reads the head again only when the cached head gives fewer
    // items than the View holds, for the reason of the push.  The cells are
    // free for the producer at the one release store of the tail.
    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(::foundation::core::View<T> out) noexcept {
        if (out.size() == 0) [[unlikely]]
            return 0;
        const std::uint64_t t = own_tail_.get();
        std::uint64_t available = cached_head_.get() - t;
        if (available < out.size()) [[unlikely]] {
            available = items_after_head_load_();
        }
        if (available == 0) [[unlikely]]
            return 0;
        const std::size_t count = available < out.size() ? available : out.size();

        const std::uint64_t start_pos = t & MASK;
        const std::size_t room_to_end = Capacity - start_pos;
        const std::size_t first = room_to_end < count ? room_to_end : count;
        const std::size_t second = count - first;

        T* const target = first_of_(::foundation::core::detail::ViewDoor::key(), out);
        std::memcpy(target, buffer_.data() + start_pos, first * sizeof(T));
        if (second > 0) [[unlikely]] {
            std::memcpy(target + first, buffer_.data(), second * sizeof(T));
        }

        tail_.advance_sole_writer_from(prove_tail_writer_(), t, t + count);
        own_tail_.advance(t + count);
        return count;
    }

    // A snapshot reads the index of the other side, and the schedule decides
    // when that side moves it.  So two runs with the same inputs can read two
    // values, and each snapshot takes the bare tag of Nondet.
    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet) const noexcept {
        return head_.get() == tail_.get();
    }

    // The count of items between a head and a tail that a reader loaded in
    // that order.  A reader on a third thread can load a tail that is newer
    // than its head, because the consumer can pop past that head between
    // the two loads.  The unsigned difference then wraps to a value near
    // 2^64, so the count is zero when the tail passed the head.  The clamp
    // to Capacity keeps the bound of each other order of the two loads.
    // size_t and uint64_t are the same type on every supported platform, so
    // the return converts exactly, and an explicit cast would trip
    // -Werror=useless-cast.
    [[nodiscard]] static constexpr std::size_t count_between(std::uint64_t head, std::uint64_t tail) noexcept {
        if (tail >= head) return 0;
        const std::uint64_t count = head - tail;
        return count > Capacity ? Capacity : count;
    }

    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet) const noexcept {
        const std::uint64_t h = head_.get();
        const std::uint64_t t = tail_.get();
        return count_between(h, t);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // The writers of the two published indexes (THE WRITER OF A COUNTER of
    // fixy/Mutation.h).  The ring is the owner of each proof, so only the ring
    // publishes an index.  Each call of a side makes the proof of that side:
    // a proven call after its Owner gave the proof of the side, and a checked
    // call, whose publish still compares the published index with the copy of
    // its side, so a second writer of one side ends the process at the next
    // publish of the first writer.
    using head_writer_ = ::fixy::SoleWriterProof<SpscRing, SpscProducerRole>;
    using tail_writer_ = ::fixy::SoleWriterProof<SpscRing, SpscConsumerRole>;

    [[nodiscard]] static constexpr head_writer_ prove_head_writer_() noexcept { return {}; }

    [[nodiscard]] static constexpr tail_writer_ prove_tail_writer_() noexcept { return {}; }

    // The free slots of the ring after a fresh acquire load of the tail into
    // the cached tail: the path of a push whose cache says that the ring has
    // too little room.  Only the producer calls it.  It is cold and out of
    // line, so the path of a push with room keeps the code of a push with no
    // cache.
    [[nodiscard]] CRUCIBLE_COLD std::uint64_t free_after_tail_load_() noexcept {
        cached_tail_.advance(tail_.get());
        return Capacity - (own_head_.get() - cached_tail_.get());
    }

    // The items of the ring after a fresh acquire load of the head into the
    // cached head: the path of a pop whose cache says that the ring has too
    // few items.  Only the consumer calls it, for the reason of the push.
    [[nodiscard]] CRUCIBLE_COLD std::uint64_t items_after_head_load_() noexcept {
        cached_head_.advance(head_.get());
        return cached_head_.get() - own_tail_.get();
    }

    // head_ and tail_ occupy separate cache lines.  On one shared line
    // every push would invalidate the consumer's copy and every pop the
    // producer's.  The separation is carried by AtomicMonotonic, which
    // is alignas(64) and at least a line in size and asserts both in
    // its own header; repeating alignas(64) here would read as though
    // the ring were what establishes it.
    //
    // The other side's index is read with get, which acquires, and
    // published with advance_sole_writer_from or advance_sole_writer_by_proof,
    // which release.  That pair is what carries the cell contents across.
    // Each index has one writer, so the publish is a plain store and not a
    // locked instruction.  The writer hands in the value of its own copy, so
    // the step check compares registers.
    //
    // The own index and the cache of each side are plain values on one line
    // of their own, next to the index of their side (THE CACHES, THE OWN
    // INDEX).  Only their side reads and writes them, so the other core
    // never pulls that line.

    ::fixy::AtomicMonotonic<std::uint64_t, head_writer_> head_ =
        ::fixy::mint_atomic_monotonic<std::uint64_t, head_writer_>(0);
    // The producer is the one reader and the one writer of the two.
    alignas(64)::fixy::Monotonic<std::uint64_t> own_head_ = ::fixy::mint_monotonic<std::uint64_t>(0);
    ::fixy::Monotonic<std::uint64_t> cached_tail_ = ::fixy::mint_monotonic<std::uint64_t>(0);
    ::fixy::AtomicMonotonic<std::uint64_t, tail_writer_> tail_ =
        ::fixy::mint_atomic_monotonic<std::uint64_t, tail_writer_>(0);
    // The consumer is the one reader and the one writer of the three.  The
    // count of the deferred pops is the copy of the tail minus the published
    // tail (THE DEFERRED TAIL), and it is zero after each pop of another form.
    alignas(64)::fixy::Monotonic<std::uint64_t> own_tail_ = ::fixy::mint_monotonic<std::uint64_t>(0);
    ::fixy::Monotonic<std::uint64_t> cached_head_ = ::fixy::mint_monotonic<std::uint64_t>(0);
    std::uint64_t deferred_pops_ = 0;

    alignas(64) std::array<T, Capacity> buffer_{};
};

}  // namespace fixy::concurrent
