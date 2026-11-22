// Negative-compile test.  This file must fail to compile.
//
// A write ticket writes its cell one time (Proof of a ticket of
// fixy/concurrent/SequenceCells.h).  A second write with one ticket needs the
// ticket in a name, so that two calls can give it.  The ticket does not copy
// and does not move, so a door takes it only as the value that the mint
// gives, and a ticket in a name writes no time.  The Owner here keeps a ticket
// in a name for two writes, and its first write already stops the build.
//
// Expected diagnostic: the move constructor of the write ticket is deleted.

#include <fixy/concurrent/SequenceCells.h>

#include <cstdint>
#include <utility>

namespace {

struct Queue;

using Cells = ::fixy::concurrent::SequenceCells<int, 8, Queue>;

struct Queue final {
    static void write_with_a_kept_ticket(Cells& cells) noexcept {
        Cells::WriteTicket ticket = Cells::WriteTicket{::fixy::concurrent::CellTicket{std::uint64_t{0}}};
        cells.publish(std::move(ticket), 7);
    }
};

}  // namespace

int main() { return 0; }
