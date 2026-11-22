// Negative-compile test.  This file must fail to compile.
//
// Only the queue that the cells name as their Owner mints a write ticket,
// after its claim of the ticket (Proof of a ticket of
// fixy/concurrent/SequenceCells.h).  A stranger that builds a ticket would
// write a cell that another producer claimed.
//
// Expected diagnostic: the constructor of the write ticket is private.

#include <fixy/concurrent/SequenceCells.h>

#include <cstdint>

namespace {

struct Queue final {};

using Cells = ::fixy::concurrent::SequenceCells<int, 8, Queue>;

[[maybe_unused]] void write_without_a_claim(Cells& cells) noexcept {
    cells.publish(Cells::WriteTicket{::fixy::concurrent::CellTicket{std::uint64_t{0}}}, 7);
}

}  // namespace

int main() { return 0; }
