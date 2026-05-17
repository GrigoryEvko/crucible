// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-WRAP fixture #8: BoundedMonotonic<T, Max>::advance rejects
// values > Max at consteval.
//
// Violation: BoundedMonotonic::advance fires `pre(!(T(Max) <
// new_value))` — advancing past the bound is the contract violation
// that BoundedMonotonic exists to prevent.  Routing through the
// fixy::wrap alias must reject the consteval evaluation.
//
// Expected diagnostic: substring "contract" / "not a constant
// expression" / "pre".

#include <crucible/fixy/Wrap.h>

#include <cstdint>

namespace fw = crucible::fixy::wrap;

struct TypeFixyWrapBoundedOverflow {};

consteval std::uint32_t probe() {
    fw::BoundedMonotonic<std::uint32_t, 4U> bm{0};
    bm.advance(99U);  // 99 > Max=4, fires pre(!(Max < new_value))
    return bm.get();
}

constexpr std::uint32_t kProbed = probe();

int main() {
    (void)kProbed;
    return 0;
}
