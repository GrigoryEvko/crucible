// A pop of an MPSC ring is a merge: the schedule of the producers decides the
// order of the items.  So the pop of the raw ring takes the bare tag of
// Nondet, as the pop of the permissioned channel does, and a pop with no tag
// does not compile.
//
// Expected diagnostic: try_pop has no candidate with no argument.

#include <fixy/concurrent/MpscRing.h>

int main() {
    ::fixy::concurrent::MpscRing<int, 8> ring;
    return ring.try_pop().is_some() ? 0 : 1;
}
