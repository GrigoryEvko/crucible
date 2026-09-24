// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #5: Monotonic<T> rejects advance() to a smaller
// value at consteval.
//
// Violation: Monotonic::advance fires `pre(lattice_type::leq(old,
// new))` — calling advance(0) after current==5 violates the
// monotonicity invariant.  Surfaced through the fixy::wrap alias
// must reject the consteval evaluation as non-constant.
//
// Expected diagnostic: substring "contract" / "not a constant
// expression" / "pre".

#include <crucible/fixy/Wrap.h>

#include <cstdint>

namespace fw = crucible::fixy::wrap;

struct TypeFixyWrapMonotonicBackward {};

consteval std::uint32_t probe() {
    fw::Monotonic<std::uint32_t> m{5};
    m.advance(0U);  // backwards — fires precondition
    return m.get();
}

constexpr std::uint32_t kProbed = probe();

int main() {
    (void)kProbed;
    return 0;
}
