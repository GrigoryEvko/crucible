// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: stacking AppendOnly<WriteOnce<T>>.  AppendOnly already
// promises that emplaced elements are never mutated, reassigned, or
// removed — layering WriteOnce inside adds no invariant and costs one
// std::optional tag byte per element.  Mutation.h's forward-declared
// is_writeonce trait + static_assert inside AppendOnly catches this
// structural redundancy at the instantiation site.

#include <crucible/safety/Mutation.h>

using crucible::safety::AppendOnly;
using crucible::safety::WriteOnce;

// Intent: an "immutable event log".  The naive reach for WriteOnce is
// the redundant pattern we want to forbid.
struct Event { int payload; };

using RedundantLog = AppendOnly<WriteOnce<Event>>;

// Instantiate to trigger the static_assert in AppendOnly's body.
RedundantLog g_log;

int main() { return 0; }
