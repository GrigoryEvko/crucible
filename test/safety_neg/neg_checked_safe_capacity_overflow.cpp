// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: safe_capacity<A, B> is instantiated with operands whose
// product overflows std::size_t.  Per #408 SAFEINT-C19 this fires the
// framework-controlled `[Checked_Capacity_Overflow]` static_assert,
// turning a previously-silent compile-time multiplication bug into a
// build error with a grep-discoverable diagnostic prefix.

#include <crucible/safety/Checked.h>

#include <cstddef>

using namespace crucible::safety;

// SIZE_MAX * 2 unambiguously overflows std::size_t on every supported
// platform (64-bit unsigned).  The variable-template instantiation
// fires safe_mul_impl::_opt's static_assert.
inline constexpr std::size_t kBoom =
    safe_capacity<static_cast<std::size_t>(-1), 2u>;

int main() {
    return static_cast<int>(kBoom & 1u);
}
