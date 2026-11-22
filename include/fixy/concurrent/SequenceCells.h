#pragma once

// The cells of a bounded queue in the form of Dmitry Vyukov: each cell holds a
// turn word beside its value, so a writer and a reader meet at the word of one
// cell and at no word that the other cells share.  The MPSC ring of
// fixy/concurrent/MpscRing.h claims its pushes on these cells.  A bounded
// queue of many producers and many consumers claims its pops on them too.
//
// THE TICKETS
//   The cells keep no index.  A queue claims tickets in order: ticket t names
//   the cell t mod Capacity and the round t / Capacity of that cell.  The
//   claim of a ticket is the business of the queue, such as a
//   compare-and-swap on a head that many producers share.  The holder of a
//   claim is the one writer of its ticket, and the holder of the next ticket
//   of the reader side is the one reader of it.
//
// THE PROOF OF A TICKET
//   The value of a cell is plain, and two threads write it in turn.  So each
//   write of a value takes a WriteTicket, and each read takes a ReadTicket.
//   Only the queue that names itself as Owner mints a ticket: a write ticket
//   after its claim of the ticket, such as the compare-and-swap of the head,
//   and a read ticket under the proof of its one reader.  A ticket does not
//   copy and does not move, so a door takes it only as the value that the
//   mint gives, and one ticket writes or reads one time.  So the cells state
//   synchronized{} (foundation/Transfer.h): the turn words are atomic, and
//   each door that writes a value takes a ticket.
//
// THE TURN WORD
//   The word of a cell reads 2 * r while the cell is free for the writer of
//   round r, and 2 * r + 1 while it holds the value of round r.  A writer
//   writes the value and then stores 2 * r + 1 with release order.  A reader
//   loads the word with acquire order, reads the value, and then stores
//   2 * r + 2, the free word of the next round, with release order.  So the
//   reader sees each write of the writer of the value, and the writer of the
//   next round sees the read of the reader before it writes.  The two values
//   of one round differ in the low bit, so a ring of one cell has distinct
//   words for each step too.
//
// THE TURN OF A WRITER
//   write_turn tells the writer of a ticket how its cell stands: free for the
//   ticket, behind (the cell still holds a round before it, so the queue is
//   full at the ticket), or ahead (a claim of a later round took the cell,
//   so the ticket is stale).  The answer is true at the load, because a word
//   only grows.  The reader side frees the cells in the order of their
//   tickets, so a cell that is free at ticket t also shows each ticket of the
//   round before t as read.  A batch of tickets then tests only its last
//   cell.
//
// THE SPACING OF THE CELLS
//   A packed cell takes its word and its value, so one cache line holds the
//   cells of some adjacent tickets.  A cell of the spacing line takes a line of
//   its own.  Writers of adjacent tickets then write different lines, and the
//   reader of a ticket reads no line that the writer of the next ticket
//   writes.  A packed line moves to the reader with the values of some
//   tickets at one time, so one writer gains from it.  Several writers that
//   claim adjacent tickets at the same time move one line between them.  The
//   MPSC ring of fixy/concurrent/MpscRing.h gives its measured numbers.

#include <fixy/concurrent/RingValue.h>

#include <foundation/ByteSeal.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Id.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fixy::concurrent {

struct CellTicketTag final {};

// A ticket of a queue (THE TICKETS).
using CellTicket = ::foundation::core::Id<CellTicketTag, std::uint64_t>;

// How the cell of a ticket stands for its writer (THE TURN OF A WRITER).
enum class CellTurn : std::uint8_t {
    free,
    behind,
    ahead,
};

// How the cells lie in memory (THE SPACING OF THE CELLS).
enum class CellSpacing : std::uint8_t {
    packed,
    line,
};

template <RingValue T, std::size_t Capacity, class Owner, CellSpacing Spacing = CellSpacing::line>
class[[= ::foundation::transfer::synchronized{}]] SequenceCells
    : public ::foundation::Pinned<SequenceCells<T, Capacity, Owner, Spacing>> {
    static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0,
                  "fixy::concurrent::SequenceCells<T, Capacity>: Capacity must be a power of two.  The cell of a "
                  "ticket is its low bits, and the round of a ticket is its high bits.");

    static constexpr std::uint64_t MASK = std::uint64_t{Capacity - 1};
    // The count of the low bits of a ticket that name its cell.
    static constexpr int ROUND_SHIFT = __builtin_ctzll(Capacity);

    // The word and the value of one cell.  A writer and a reader of one cell
    // touch its line, and a write of a word is a plain release store, not a
    // read-modify-write.  The two forms differ only in the alignment of the
    // cell (THE SPACING OF THE CELLS).
    struct PackedCell {
        ::foundation::core::Atomic<std::uint64_t> word{};
        T value{};
    };
    struct alignas(64) LineCell {
        ::foundation::core::Atomic<std::uint64_t> word{};
        T value{};
    };
    using Cell = std::conditional_t<Spacing == CellSpacing::line, LineCell, PackedCell>;

    // The free word of the round of a ticket (THE TURN WORD).
    [[nodiscard]] static constexpr std::uint64_t free_word_(CellTicket ticket) noexcept {
        return (ticket.raw() >> ROUND_SHIFT) << 1;
    }

    [[nodiscard]] Cell& cell_(CellTicket ticket) noexcept { return cells_[ticket.raw() & MASK]; }
    [[nodiscard]] Cell const& cell_(CellTicket ticket) const noexcept { return cells_[ticket.raw() & MASK]; }

public:
    // The proof of the one writer of the value of a ticket (THE PROOF OF A
    // TICKET).  Only Owner mints one, after its claim of the ticket.
    class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] WriteTicket final {
        CellTicket ticket_{std::uint64_t{0}};

        // The seal refuses each byte route, such as std::bit_cast, so no bytes
        // become a ticket.
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

        explicit constexpr WriteTicket(CellTicket ticket) noexcept : ticket_{ticket} {}
        friend Owner;
        friend class SequenceCells;

    public:
        WriteTicket(WriteTicket const&) = delete("a ticket writes its value one time");
        WriteTicket(WriteTicket&&) = delete("a ticket writes its value one time, as the value that its mint gives");
        WriteTicket& operator=(WriteTicket const&) = delete("a ticket writes its value one time");
        WriteTicket& operator=(WriteTicket&&) = delete("a ticket writes its value one time");
        ~WriteTicket() = default;
    };

    // The proof of the one reader of the value of a ticket (THE PROOF OF A
    // TICKET).  Only Owner mints one, under the proof of its one reader.
    class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] ReadTicket final {
        CellTicket ticket_{std::uint64_t{0}};

        // The seal refuses each byte route, such as std::bit_cast, so no bytes
        // become a ticket.
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

        explicit constexpr ReadTicket(CellTicket ticket) noexcept : ticket_{ticket} {}
        friend Owner;
        friend class SequenceCells;

    public:
        ReadTicket(ReadTicket const&) = delete("a ticket reads its value one time");
        ReadTicket(ReadTicket&&) = delete("a ticket reads its value one time, as the value that its mint gives");
        ReadTicket& operator=(ReadTicket const&) = delete("a ticket reads its value one time");
        ReadTicket& operator=(ReadTicket&&) = delete("a ticket reads its value one time");
        ~ReadTicket() = default;
    };

    SequenceCells() noexcept = default;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
    static constexpr CellSpacing spacing = Spacing;
    // The bytes of one cell in the array.
    static constexpr std::size_t cell_bytes = sizeof(Cell);

    // How the cell of the ticket stands for its writer, with acquire order:
    // a free cell also shows the read of the reader of the round before.
    // The difference is signed, because a word behind the free word of the
    // ticket is below it and a word ahead of it is above it.
    [[nodiscard]] CellTurn write_turn(CellTicket ticket) const noexcept {
        std::uint64_t const word = cell_(ticket).word.load_acquire();
        auto const distance = static_cast<std::int64_t>(word - free_word_(ticket));
        if (distance == 0) return CellTurn::free;
        return distance < 0 ? CellTurn::behind : CellTurn::ahead;
    }

    // Writes the value of the ticket and publishes it.  The ticket proves the
    // claim of the ticket, and write_turn gave free for its cell.
    void publish(WriteTicket ticket, T const& value) noexcept {
        Cell& cell = cell_(ticket.ticket_);
        cell.value = value;
        cell.word.store_release(free_word_(ticket.ticket_) + 1);
    }

    // True when the cell of the ticket holds the value of the ticket, with
    // acquire order, so a read of the value after it sees the write of the
    // writer.
    [[nodiscard]] bool is_published(CellTicket ticket) const noexcept {
        return cell_(ticket).word.load_acquire() == free_word_(ticket) + 1;
    }

    // Reads the value of the ticket, and then frees the cell for the writer of
    // its next round.  The ticket proves the one reader, and is_published gave
    // true.
    [[nodiscard]] T take(ReadTicket ticket) noexcept {
        Cell& cell = cell_(ticket.ticket_);
        T const value = cell.value;
        cell.word.store_release(free_word_(ticket.ticket_) + 2);
        return value;
    }

private:
    // The cells.  The word of each cell is atomic.  The value of a cell is
    // plain: the write ticket of its ticket admits its write, and the read
    // ticket admits its read and the free of its cell.
    alignas(64) std::array<Cell, Capacity> cells_{};
};

}  // namespace fixy::concurrent
