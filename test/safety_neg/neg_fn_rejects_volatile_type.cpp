// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-1 audit-round-2 fixture #4 of 5 for safety::fn::Fn
// (#1095) — proves that the Fn<Type, ...> aggregator rejects
// volatile-qualified Type.
//
// Why this matters: `volatile` is a hardware-memory annotation —
// it tells the compiler "the value can change behind your back"
// (memory-mapped I/O registers, signal-safe variables on a
// single thread).  It is NOT a property the Fn grade vector
// models; the 19 axes (Usage, EffectRow, Security, Lifetime,
// Source, Trust, Repr, Cost, Precision, Space, Overflow,
// Mutation, Reentrancy, Size, Version, Staleness) all describe
// software-level discipline and have no bearing on memory-
// mapped hardware.
//
// A reviewer who reaches for `Fn<volatile int>` to express
// "this is concurrently mutated" is confusing volatile (which
// does NOT provide atomicity or synchronization) with
// std::atomic<T> (which does both).  The static_assert points
// the reviewer at std::atomic<T> for concurrent access or at
// the Type definition site for genuinely-volatile hardware
// memory.
//
// Without the static_assert gate, `Fn<volatile int>` would
// compile but downstream operations on `value_` would
// generate volatile-load/store sequences that defeat the
// optimizer's ability to fold or hoist accessor calls,
// silently degrading the wrapper's zero-runtime-cost claim.
//
// Expected diagnostic: "Fn<volatile T, ...> is malformed.
// volatile is a hardware-memory annotation".

#include <crucible/safety/Fn.h>

namespace neg = crucible::safety::fn;

// Bridge fires: instantiating Fn<volatile int> trips the
// !std::is_volatile_v<Type> static_assert in the class-template
// body.  Compilation aborts with the assertion message.
[[maybe_unused]] neg::Fn<volatile int> the_fixture{};

int main() { return 0; }
