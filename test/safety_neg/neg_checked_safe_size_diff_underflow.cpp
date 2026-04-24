// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: safe_size_diff<A, B> with A < B is unsigned underflow.
// Per #408 SAFEINT-C19, the framework rejects via the
// `[Checked_Capacity_Overflow]` static_assert (the "Overflow"
// diagnostic name covers both directions of integer-domain
// violation).

#include <crucible/safety/Checked.h>

#include <cstddef>

using namespace crucible::safety;

// 10 - 100 underflows std::size_t (wraps to ~SIZE_MAX without the
// check).  safe_sub_impl's static_assert fires on the nullopt
// returned by checked_sub<std::size_t>(10, 100).
inline constexpr std::size_t kUnderflow = safe_size_diff<10u, 100u>;

int main() {
    return static_cast<int>(kUnderflow & 1u);
}
