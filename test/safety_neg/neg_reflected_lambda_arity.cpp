// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::reflected::for_each_enumerator
// (#1089).
//
// Premise: for_each_enumerator<E>([]{ ... }) — a callable taking ZERO
// arguments — MUST be a compile error.  The runtime callback contract
// is `f(E value, std::string_view name)`; passing a zero-arg lambda
// would silently iterate WITHOUT giving the body access to the
// enumerator value or its identifier.  This is the load-bearing
// runtime-vs-NTTP distinction documented in Reflected.h's design
// notes: callers used to write
//
//   for_each_enumerator<E>([&]<std::meta::info Info>{ ... });
//
// during the initial sketch.  That shape was rejected because
// std::meta::info is a consteval-only type (P3032) and forces
// immediate-escalation, which forbids any runtime-mutating body.
// The current shape passes value+name as ordinary parameters; this
// fixture pins the API by proving the old NTTP-only shape (or any
// other lambda that doesn't accept the (E, std::string_view) pair)
// is a hard compile error at the call site.
//
// Expected diagnostic: "no match for call" / "no matching function" /
// "candidate expects 0 arguments, 2 provided" / "wrong number of
// arguments to function" at the f(value, name) call inside
// for_each_enumerator's body.

#include <crucible/safety/Reflected.h>

namespace ref = crucible::safety::reflected;

enum class TestFlags : unsigned char {
    Alpha = 0x01,
    Beta  = 0x02,
};

int main() {
    // Bridge fires: f(value, name) call inside for_each_enumerator
    // cannot bind to a zero-arg lambda.  The old NTTP-info shape
    // (which was the original sketch) is the same failure mode —
    // both reject for the same reason.
    ref::for_each_enumerator<TestFlags>([]{ /* zero-arg, wrong shape */ });
    return 0;
}
