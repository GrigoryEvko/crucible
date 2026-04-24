// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: stacking AppendOnly<WriteOnceNonNull<T*>>.  AppendOnly
// already promises that emplaced elements are never mutated,
// reassigned, or removed — layering WriteOnceNonNull inside adds no
// invariant and costs the wrapper's machinery per element.  Mirrors
// the existing AppendOnly<WriteOnce<T>> rejection (#77, symmetric
// with neg_appendonly_over_writeonce.cpp).
//
// Fires `[AppendOnly_Over_WriteOnceNonNull_Redundant]` at the
// AppendOnly<> instantiation site.

#include <crucible/safety/Mutation.h>

using crucible::safety::AppendOnly;
using crucible::safety::WriteOnceNonNull;

struct Event {};

// Redundant: AppendOnly guarantees immutability; WriteOnceNonNull
// inside the element slot adds no invariant.
using RedundantLog = AppendOnly<WriteOnceNonNull<Event*>>;

RedundantLog g_log;

int main() { return 0; }
