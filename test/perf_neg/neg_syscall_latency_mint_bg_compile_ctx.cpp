// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_syscall_latency rejects BgCompileCtx.  That context holds the
// background capability and claims Row<Bg, Alloc, IO>, so it satisfies
// two of the three atoms the gate demands and fails on Block alone.
//
// This is a distinct mismatch class from the ColdInitCtx fixture: the
// capability source is one the gate does admit, and the row is still
// short.  The pair proves the gate reads the wait rather than the
// capability source.  Widening this context to Row<Bg, Alloc, IO, Block>
// is legal and is the production path.

#include <crucible/perf/SyscallLatency.h>

int main() {
    auto hub =
        crucible::perf::mint_syscall_latency(crucible::effects::BgCompileCtx{}, crucible::effects::testing::init());
    (void)hub;
    return 0;
}
