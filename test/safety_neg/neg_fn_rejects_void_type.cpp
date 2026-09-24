// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-1 fixture #1 of 2 for safety::fn::Fn (#1095) —
// proves that the Fn<Type, ...> aggregator rejects `void` as
// the Type slot.
//
// Why this matters: Fn carries `Type value_{}` as its sole
// runtime member.  A void value_ is malformed (you cannot
// declare a member of type void; you cannot brace-init it; you
// cannot move from it).  Without the static_assert gate, a
// reviewer who instantiates `Fn<void>` (intending to model a
// "no return value" Fixy binding) would get a cascade of
// downstream "void cannot be a member type" / "no matching
// constructor" / "use of deleted function" diagnostics that
// don't point at the root cause.
//
// The correct way to model a void-returning Fixy function is
// via the function pointer: `Fn<void(*)(Args...), ...>` — the
// Type slot is a pointer (object type), the return-void shows
// up in the function-pointer signature.  This fixture catches
// the mistake at the call site with a clear "requires Type to
// be a complete object type" message.
//
// Expected diagnostic: "requires Type to be a complete object
// type" / "Reject: void, reference types" pointing at the
// static_assert in safety/Fn.h's class-template body.

#include <crucible/safety/Fn.h>

namespace neg = crucible::safety::fn;

// Bridge fires: instantiating Fn<void> trips the static_assert
// in the class-template body before any member declaration is
// reached.  Compilation aborts with the assertion message.
[[maybe_unused]] neg::Fn<void> the_fixture{};

int main() { return 0; }
