// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-1 audit-round-2 fixture #3 of 5 for safety::fn::Fn
// (#1095) — proves that the Fn<Type, ...> aggregator rejects
// const-qualified Type.
//
// Why this matters: a wrapper that takes `const T` for the value
// slot silently has its defaulted copy- and move-assignment
// operators DELETED (you cannot assign to a const member).  The
// user reaches for `Fn<const int>` thinking they want logical
// immutability, then discovers downstream that the wrapper can't
// be re-bound, can't be assigned through, and can't participate
// in normal value-semantic flows.
//
// The correct way to express logical immutability is via the
// MutationMode dimension: `Fn<int, ..., MutationMode::Immutable>`.
// The wrapper is then assignable, copyable, movable — and the
// MutationMode tag carries the immutability discipline at the
// type level so downstream consumers (mut-checking concepts,
// session protocols, F\* effect rows) reject in-place writes.
//
// Without the static_assert gate, instantiating `Fn<const int>`
// would silently produce a wrapper with a deleted assignment
// operator.  The diagnostic from a downstream `f = g;` would be
// "use of deleted function" pointing at the synthesized
// assignment, never at the root cause `Fn<const T>`.  This
// fixture catches the mistake at the wrapper's instantiation
// site.
//
// Expected diagnostic: "Fn<const T, ...> is malformed.  Const-
// qualifying the value type silently deletes copy- and move-
// assignment of the wrapper".

#include <crucible/safety/Fn.h>

namespace neg = crucible::safety::fn;

// Bridge fires: instantiating Fn<const int> trips the
// !std::is_const_v<Type> static_assert in the class-template body.
// Compilation aborts with the assertion message.
[[maybe_unused]] neg::Fn<const int> the_fixture{};

int main() { return 0; }
