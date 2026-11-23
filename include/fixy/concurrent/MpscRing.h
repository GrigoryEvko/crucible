#pragma once

// Multi-producer single-consumer ring, on the cells of
// fixy/concurrent/SequenceCells.h: each cell holds a turn word beside its
// value, in the form of Dmitry Vyukov.
//
// A producer claims a ticket with a compare-and-swap on the head, which the
// producers share, and then it touches only the cell of its ticket.  The
// consumer reads the turn word of the cell at its tail.  So the producers
// share one word, the head, and a producer reads no line that the consumer
// writes on each pop.  The full test of a producer is the turn word of its
// cell: a cell that still holds the value of the round before shows a full
// ring at that ticket.
//
// Measured reason
//   An earlier form kept the ready signal out of band, one bit for each cell
//   in a packed bitmap, so that the cells stay packed.  Each producer and the
//   consumer then did a read-modify-write on one bitmap word for each 64
//   cells, and each producer read the tail, which the consumer writes on each
//   pop.  In bench_thread_scaling on 16 isolated cores, with the consumer on
//   its own core, the single push of the turn words in packed cells gave 3.6
//   times the throughput of the bitmap form with one producer, 1.3 times with
//   3, 1.5 times with 7 and 2.3 times with 15, and a batch push of 16 gave 1.0
//   to 1.8 times.
//
// Spacing of the cells
//   The ring takes the spacing of its cells as a parameter (the spacing of the
//   cells of fixy/concurrent/SequenceCells.h).  With packed cells, two
//   producers with adjacent tickets write one cache line.  With the spacing
//   line, each cell takes a line of its own, and the ring pays a line for each
//   item.  In bench_thread_scaling on isolated cores of one L3 group, the push
//   of packed cells gave 57 million items a second with one producer, 10.6
//   with three and 7.7 with seven.  The spacing line gave 28, 22 and 15.  The
//   pinned MPMCQueue of the bench, whose slots take 128 bytes each, gave 39 to
//   50, 11 to 17 and 9 to 10.  A batch push writes one line for each item
//   with the spacing line: with three producers, the batch push of 16 items
//   gave 235 million items a second with packed cells and 163 to 169 with the
//   spacing line.  A ring of many producers is the purpose of this type, so
//   the spacing line is the default.  A ring that mostly has one producer, or
//   that takes batch pushes, names CellSpacing::packed.
//
// Writers
//   Each part of the ring has its writer in a type, so two threads can write
//   the ring at the same time (foundation/Transfer.h).  The producers share
//   the head, an atomic counter of many writers.  A push writes the value of
//   a cell with the write ticket that its compare-and-swap of the head won.
//   The tail has one writer, the consumer, and each pop takes the proof of
//   the consumer, which only Owner makes.  A pop reads the value of a cell
//   and frees the cell with a read ticket, which the ring mints only under
//   that proof (the proof of a ticket of fixy/concurrent/SequenceCells.h).

#include <fixy/Mutation.h>
#include <fixy/concurrent/RingValue.h>
#include <fixy/concurrent/SequenceCells.h>

#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <cstddef>
#include <cstdint>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct mpsc_ring;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The role of the proof of the one writer of the tail of a ring, the
// consumer.
struct MpscConsumerRole final {};

template <RingValue T, std::size_t Capacity, class Owner, CellSpacing Spacing = CellSpacing::line>
class MpscRing : public ::foundation::Pinned<MpscRing<T, Capacity, Owner, Spacing>> {
    using cells_type = SequenceCells<T, Capacity, MpscRing, Spacing>;

public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;
    static constexpr CellSpacing cell_spacing = Spacing;
    using row_discipline = ::fixy::row_discipline::mpsc_ring;
    using row_payload = T;
    using owner_type = Owner;
    // The proof of the one consumer, which only Owner makes (the writers).
    using consumer_proof = ::fixy::SoleWriterProof<Owner, MpscConsumerRole>;

    static_assert(std::has_single_bit(Capacity), "fixy::concurrent::MpscRing<T, Capacity>: Capacity must be a power "
                                                 "of two.  The position-to-cell map is a bitwise AND with "
                                                 "Capacity - 1, which is the index modulo Capacity only when "
                                                 "Capacity is a power of two.");
    static_assert(Capacity >= 1, "fixy::concurrent::MpscRing<T, Capacity>: Capacity must be greater than zero.  A "
                                 "ring with no cells has no cell for a producer to fill.");

    MpscRing() noexcept = default;

    // The head is a counter of many writers.  Its load seeds the
    // compare-and-swap of the claim, and the claim carries no data: the
    // release store of the turn word of the cell publishes the item, and the
    // acquire load of the turn word by the consumer reads it.  The turn word
    // that a producer reads with acquire order before its claim shows that
    // the consumer read the cell of the round before.  A stale head names a
    // cell whose turn is ahead, and the producer reads the head again.

    [[nodiscard, gnu::hot]] bool try_push(T const& item) noexcept {
        // The copy reads the item before the claim of the ticket.  The claim
        // is an atomic step, so a read through the reference after it loads
        // the item from memory again.
        const T value = item;
        std::uint64_t pos = head_.load_relaxed();
        for (;;) {
            CellTicket const ticket{pos};
            CellTurn const turn = cells_.write_turn(ticket);
            if (turn == CellTurn::behind) [[unlikely]] {
                return false;
            }
            if (turn == CellTurn::ahead) [[unlikely]] {
                pos = head_.load_relaxed();
                continue;
            }
            std::uint64_t expected = pos;
            if (head_.compare_exchange_advance_weak_relaxed(expected, pos + 1)) [[likely]] {
                cells_.publish(write_ticket_(ticket), value);
                return true;
            }
            pos = expected;
        }
    }

    // All or nothing: a batch that does not fit pushes no items.  The
    // consumer frees the cells in the order of their tickets, so a last cell
    // that is free for its ticket shows each cell of the batch as free
    // (the turn of a writer of fixy/concurrent/SequenceCells.h).  One claim
    // takes the tickets of the whole batch.

    [[nodiscard, gnu::hot]] std::size_t try_push_batch(::foundation::core::View<T const> items) noexcept {
        const std::size_t N = items.size();
        if (N == 0) return 0;
        if (N > Capacity) [[unlikely]]
            return 0;
        T const* const source = first_of_(::foundation::core::detail::BaseLayerRead{}, items);
        std::uint64_t pos = head_.load_relaxed();
        for (;;) {
            CellTurn const turn = cells_.write_turn(CellTicket{pos + N - 1});
            if (turn == CellTurn::behind) [[unlikely]] {
                return 0;
            }
            if (turn == CellTurn::ahead) [[unlikely]] {
                pos = head_.load_relaxed();
                continue;
            }
            std::uint64_t expected = pos;
            if (head_.compare_exchange_advance_weak_relaxed(expected, pos + N)) [[likely]] {
                for (std::size_t i = 0; i < N; ++i) {
                    cells_.publish(write_ticket_(CellTicket{pos + i}), source[i]);
                }
                return N;
            }
            pos = expected;
        }
    }

    // Each pop takes the proof of the one consumer (the writers).  A pop is
    // a merge: the schedule of the producers decides the order of the items,
    // so two runs with the same inputs can pop two sequences.  Thus each pop
    // also takes the bare tag of Nondet.

    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop(consumer_proof const& /*proof*/,
                                                                  ::foundation::effects::cap::Nondet) noexcept {
        return pop_one_();
    }

    // Gives each item that the ring holds to drop, the oldest first, and
    // returns when the ring is empty.  The proof names the one consumer, and
    // the caller guarantees no concurrent push.  The order of the items reads
    // the merge, so the drop holds no state: its class is empty, and it can
    // only end each item.  A drop with no state can still write a global,
    // and no gate refuses that.  The drain borrows its drop and calls it for
    // each item, so the drop holds no linear value (HoldsNoLinearValue of
    // foundation/Linear.h).  An empty class holds none, so no deleted twin can
    // bind a drop that this gate refuses.  Complexity: linear in the count of
    // the items.
    template <typename Drop>
        requires(__is_empty(__remove_cvref(Drop))) && (__is_nothrow_invocable(Drop&, T))
             && ::foundation::linear::HoldsNoLinearValue<Drop>
    void drain(consumer_proof const& /*proof*/, Drop&& drop) noexcept {
        for (::foundation::core::Option<T> item = pop_one_(); item.is_some(); item = pop_one_()) {
            drop(static_cast<::foundation::core::Option<T>&&>(item).expect("the test of the loop found an item"));
        }
    }

    // A batch pop takes the proof of the one consumer, and it is a merge, as
    // a pop is, so it takes the bare tag of Nondet.  The pop takes the
    // published cells from the tail up to the first cell that is not
    // published, and at most out.size() of them.  The ring holds at most
    // Capacity items, so the scan stops after Capacity cells, before it wraps
    // to a cell that it took.  Complexity: linear in the items taken.

    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(consumer_proof const& /*proof*/,
                                                      ::foundation::effects::cap::Nondet,
                                                      ::foundation::core::View<T> out) noexcept {
        const std::size_t cap = Capacity < out.size() ? Capacity : out.size();
        if (cap == 0) return 0;

        const std::uint64_t pos0 = tail_.peek_relaxed();
        T* const target = first_of_(::foundation::core::detail::BaseLayerRead{}, out);
        std::size_t R = 0;
        while (R < cap) {
            CellTicket const ticket{pos0 + R};
            if (!cells_.is_published(ticket)) break;
            target[R] = cells_.take(read_ticket_(ticket));
            ++R;
        }
        if (R == 0) return 0;
        tail_.advance_sole_writer_from(prove_consumer_(), pos0, pos0 + R);
        return R;
    }

    // The pop into the run of a frame.  The door takes the ViewMut of the
    // frame by value, as fill does, so the pop is the one writer of the run.
    template <::foundation::brand::IsBrand Brand, std::size_t Extent>
    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(consumer_proof const& proof,
                                                      ::foundation::effects::cap::Nondet nondet,
                                                      ::foundation::core::ViewMut<T, Brand, Extent> out) noexcept {
        return try_pop_batch(proof, nondet, ::foundation::core::View<T>{::foundation::core::detail::run_of_(out)});
    }

    // A snapshot reads cells and indexes that the producers write, and the
    // schedule decides when they write them.  So two runs with the same
    // inputs can read two values, and each snapshot takes the bare tag of
    // Nondet.
    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet) const noexcept {
        return !cells_.is_published(CellTicket{tail_.get()});
    }

    // The two loads are not one snapshot, so head can outrun the tail
    // that was read for it.  The clamp absorbs that.
    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet) const noexcept {
        const std::uint64_t h = head_.get();
        const std::uint64_t t = tail_.get();
        const std::uint64_t d = (h > t) ? (h - t) : 0;
        return d > Capacity ? Capacity : static_cast<std::size_t>(d);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // The oldest item, or no value when its cell is not published.  Only a
    // door that takes the proof of the consumer calls it.  The acquire load of
    // the turn word pairs with the release store of the producer, so the read
    // of the value sees the published item.  The free of the cell comes after
    // the read, so the producer of the next round writes the cell after the
    // read.
    [[nodiscard, gnu::always_inline]] ::foundation::core::Option<T> pop_one_() noexcept {
        const std::uint64_t pos = tail_.peek_relaxed();
        CellTicket const ticket{pos};
        if (!cells_.is_published(ticket)) {
            return ::foundation::core::none;
        }
        const T item = cells_.take(read_ticket_(ticket));
        tail_.advance_sole_writer_from(prove_consumer_(), pos, pos + 1);
        return ::foundation::core::Option<T>::some(item);
    }

    // The write ticket of a ticket that the compare-and-swap of the head won,
    // and the read ticket of the ticket at the tail, under the proof of the
    // consumer (the proof of a ticket of fixy/concurrent/SequenceCells.h).
    [[nodiscard, gnu::always_inline]] static constexpr typename cells_type::WriteTicket
    write_ticket_(CellTicket ticket) noexcept {
        return typename cells_type::WriteTicket{ticket};
    }

    [[nodiscard, gnu::always_inline]] static constexpr typename cells_type::ReadTicket
    read_ticket_(CellTicket ticket) noexcept {
        return typename cells_type::ReadTicket{ticket};
    }

    // The writer of the tail, the consumer (the writer of a counter of
    // fixy/Mutation.h).  The ring is the owner of the proof, and only a pop
    // makes one.  The publish still compares the tail with the value that the
    // pop read, so a second consumer ends the process at the next publish of
    // the first one.
    using tail_writer_ = ::fixy::SoleWriterProof<MpscRing, MpscConsumerRole>;

    [[nodiscard]] static constexpr tail_writer_ prove_consumer_() noexcept { return {}; }

    // The producers write head_, and they read no line of the consumer.  The
    // consumer is the one writer of tail_, which only a snapshot reads apart
    // from the consumer.  Each counter takes a line of its own, which
    // AtomicMonotonic carries: it is alignas(64) and at least a line in
    // size, and asserts both in its own header.

    cells_type cells_{};
    ::fixy::AtomicMonotonic<std::uint64_t> head_ = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
    ::fixy::AtomicMonotonic<std::uint64_t, tail_writer_> tail_ =
        ::fixy::mint_atomic_monotonic<std::uint64_t, tail_writer_>(0);
};

}  // namespace fixy::concurrent
