// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for safety::Cyclic<T, N> (#1084 - Cyclic piece).
//
// Premise: Cyclic<T, N> constrains T to `std::unsigned_integral`.  A
// SIGNED counter type (here int32_t) MUST be a compile error, because
// the wrapper's defining mechanism — the free-running `++counter`
// wrap — is well-defined modular arithmetic ONLY for unsigned T.
// Signed overflow is undefined behaviour; a signed cursor would make
// `advance()` past INT_MAX UB, and `(counter - 1 - i)` could go
// negative and break the `& mask` slot computation.
//
// Without this rejection, `Cyclic<int32_t, 8>` would instantiate and
// the DetSafe wrap guarantee — "same inputs → same slot on any
// platform" — would silently rest on UB.  The std::unsigned_integral
// requires-clause forbids it at the type-system boundary.
//
// Expected diagnostic: "constraints not satisfied" / "associated
// constraints are not satisfied" / "no matching template" pointing at
// the Cyclic<int, 8> instantiation (unsigned_integral<int> is false).

#include <crucible/safety/Cyclic.h>

namespace saf = crucible::safety;

int main() {
    // Bridge fires: int is signed → std::unsigned_integral<int> is
    // false → the requires-clause rejects.
    saf::Cyclic<int, 8> bad{};
    (void)bad;
    return 0;
}
