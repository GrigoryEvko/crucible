// Negative-compile test. This file must fail to compile.
//
// The room test of the ring and its commit are one door: try_append or
// try_record.  A caller that reserved a slot with try_reserve_ could append
// another entry before its commit, and the commit would publish a stale head.
// So the reservation door is private.
//
// Expected diagnostic: TraceRing::try_reserve_ is private within this context.

#include <crucible/TraceRing.h>

namespace {
[[maybe_unused]] int reserve_in(crucible::TraceRing::Producer& end) {
    return crucible::TraceRing::try_reserve_(end).is_some() ? 1 : 0;
}
}  // namespace

int main() { return 0; }
