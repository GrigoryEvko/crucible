// Negative compile test.  This file must fail to compile.
//
// AtomicSnapshot::load spins while a write is in flight, with no exit: a
// writer that stops inside publish leaves the counter odd.  A spin with no
// exit carries Div, so the load takes the context of the caller, and the
// context owns Div.  A load with no context would spin from each context.
// try_load is the read with no wait.
//
// Expected diagnostic: AtomicSnapshot::load has no candidate that takes no
// argument.

#include <fixy/concurrent/AtomicSnapshot.h>

#include <cstdint>

int main() {
    const ::fixy::concurrent::AtomicSnapshot<std::uint64_t> snapshot{};
    return static_cast<int>(snapshot.load());
}
