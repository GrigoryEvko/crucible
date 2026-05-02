// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for safety::Saturated<T> (#1084 - Saturated piece).
//
// Premise: Saturated<T> requires `std::is_arithmetic_v<T>`.
// Non-arithmetic T (struct, pointer, enum, std::string) MUST be a
// compile error.  The wrapper exists to carry "did saturating
// arithmetic clamp this value?" — that semantic only applies to
// values where saturating arithmetic is meaningful (integers, floats).
//
// Without this rejection, `Saturated<MyStruct>` would silently
// instantiate, but:
//   - add_sat_checked / sub_sat_checked / mul_sat_checked use
//     __builtin_*_overflow which require integral types.
//   - The "clamped" flag would be meaningless for a struct (clamped
//     to what?  Struct types have no min/max).
//   - Implicit-from-T conversion would admit struct → Saturated<struct>,
//     polluting the type with the wrong semantics.
//
// The (std::is_arithmetic_v<T>) requires-clause prevents the
// instantiation at the type-system boundary.
//
// Expected diagnostic: "constraints not satisfied" / "associated
// constraints are not satisfied" / "no matching template" pointing
// at Saturated<MyStruct> instantiation.

#include <crucible/safety/Saturated.h>

namespace saf = crucible::safety;

struct NotArithmetic {
    int x;
    int y;
};

int main() {
    // Bridge fires: NotArithmetic is not std::is_arithmetic_v →
    // requires-clause rejects, no fallback specialization exists.
    saf::Saturated<NotArithmetic> bad{};
    (void)bad;
    return 0;
}
