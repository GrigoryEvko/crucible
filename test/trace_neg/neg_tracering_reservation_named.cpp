// Negative-compile test. This file must fail to compile.
//
// A reservation of a slot of the ring lives only inside the body of one
// append door of the ring, from its room test to its commit.  So the
// reserved head is the copy of the head of the producer end, and the commit
// needs no check of it.  A caller that names the type of a reservation could
// hold one across another append, so the type is private.
//
// Expected diagnostic: TraceRing::Reservation is private within this context.

#include <crucible/TraceRing.h>

int main() {
    ::fixy::Option<crucible::TraceRing::Reservation> const held{::fixy::none};
    return held.is_some() ? 1 : 0;
}
