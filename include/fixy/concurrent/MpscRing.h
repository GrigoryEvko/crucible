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
// THE MEASURED REASON
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
// THE SPACING OF THE CELLS
//   The ring takes the spacing of its cells as a parameter (THE SPACING OF THE
//   CELLS of fixy/concurrent/SequenceCells.h).  With packed cells, two
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

#include <fixy/Mutation.h>
#include <fixy/concurrent/RingValue.h>
#include <fixy/concurrent/SequenceCells.h>

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

template <RingValue T, std::size_t Capacity, CellSpacing Spacing = CellSpacing::line>
class MpscRing : public ::foundation::Pinned<MpscRing<T, Capacity, Spacing>> {
public:
    using value_type = T;
    static constexpr std::size_t channel_capacity = Capacity;
    static constexpr CellSpacing cell_spacing = Spacing;
    using row_discipline = ::fixy::row_discipline::mpsc_ring;
    using row_payload = T;

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
                cells_.publish(ticket, value);
                return true;
            }
            pos = expected;
        }
    }

    // All or nothing: a batch that does not fit pushes no items.  The
    // consumer frees the cells in the order of their tickets, so a last cell
    // that is free for its ticket shows each cell of the batch as free
    // (THE TURN OF A WRITER of fixy/concurrent/SequenceCells.h).  One claim
    // takes the tickets of the whole batch.

    [[nodiscard, gnu::hot]] std::size_t try_push_batch(::foundation::core::View<T const> items) noexcept {
        const std::size_t N = items.size();
        if (N == 0) return 0;
        if (N > Capacity) [[unlikely]]
            return 0;
        T const* const source = first_of_(::foundation::core::detail::ViewDoor::key(), items);
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
                    cells_.publish(CellTicket{pos + i}, source[i]);
                }
                return N;
            }
            pos = expected;
        }
    }

    // The caller guarantees no concurrent try_pop or try_pop_batch.  A pop
    // is a merge: the schedule of the producers decides the order of the
    // items, so two runs with the same inputs can pop two sequences.  Thus
    // each pop takes the bare tag of Nondet.

    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop(::foundation::effects::cap::Nondet) noexcept {
        return pop_one_();
    }

    // Gives each item that the ring holds to drop, the oldest first, and
    // returns when the ring is empty.  The caller guarantees no concurrent
    // push and no concurrent pop.  The order of the items reads the merge,
    // so the drop holds no state: its class is empty, and it can only end
    // each item.  A drop with no state can still write a global, and no gate
    // refuses that.  Complexity: linear in the count of the items.
    template <typename Drop>
        requires __is_empty
    (__remove_cvref(Drop)) && __is_nothrow_invocable(Drop&, T) void drain(Drop&& drop) noexcept {
        for (::foundation::core::Option<T> item = pop_one_(); item.is_some(); item = pop_one_()) {
            drop(static_cast<::foundation::core::Option<T>&&>(item).expect("the test of the loop found an item"));
        }
    }

    // The caller guarantees no concurrent try_pop or try_pop_batch.  A batch
    // pop is a merge, as a pop is, so it takes the bare tag of Nondet.  The
    // pop takes the published cells from the tail up to the first cell that
    // is not published, and at most out.size() of them.  The ring holds at
    // most Capacity items, so the scan stops after Capacity cells, before it
    // wraps to a cell that it took.  Complexity: linear in the items taken.

    [[nodiscard, gnu::hot]] std::size_t try_pop_batch(::foundation::effects::cap::Nondet,
                                                      ::foundation::core::View<T> out) noexcept {
        const std::size_t cap = Capacity < out.size() ? Capacity : out.size();
        if (cap == 0) return 0;

        const std::uint64_t pos0 = tail_.peek_relaxed();
        T* const target = first_of_(::foundation::core::detail::ViewDoor::key(), out);
        std::size_t R = 0;
        while (R < cap) {
            CellTicket const ticket{pos0 + R};
            if (!cells_.is_published(ticket)) break;
            target[R] = cells_.value_of(ticket);
            cells_.free(ticket);
            ++R;
        }
        if (R == 0) return 0;
        tail_.advance_sole_writer_from(pos0, pos0 + R);
        return R;
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
    // The oldest item, or no value when its cell is not published.  The
    // acquire load of the turn word pairs with the release store of the
    // producer, so the read of the value sees the published item.  The free
    // of the cell comes after the read, so the producer of the next round
    // writes the cell after the read.
    [[nodiscard, gnu::always_inline]] ::foundation::core::Option<T> pop_one_() noexcept {
        const std::uint64_t pos = tail_.peek_relaxed();
        CellTicket const ticket{pos};
        if (!cells_.is_published(ticket)) {
            return ::foundation::core::none;
        }
        const T item = cells_.value_of(ticket);
        cells_.free(ticket);
        tail_.advance_sole_writer_from(pos, pos + 1);
        return ::foundation::core::Option<T>::some(item);
    }

    // The producers write head_, and they read no line of the consumer.  The
    // consumer is the one writer of tail_, which only a snapshot reads apart
    // from the consumer.  Each counter takes a line of its own, which
    // AtomicMonotonic carries: it is alignas(64) and at least a line in
    // size, and asserts both in its own header.

    SequenceCells<T, Capacity, Spacing> cells_{};
    ::fixy::AtomicMonotonic<std::uint64_t> head_ = ::fixy::mint_atomic_monotonic<std::uint64_t>(0);
    ::fixy::AtomicMonotonic<std::uint64_t, ::fixy::CounterWriters::sole> tail_ =
        ::fixy::mint_atomic_monotonic<std::uint64_t, ::fixy::CounterWriters::sole>(0);
};

}  // namespace fixy::concurrent
