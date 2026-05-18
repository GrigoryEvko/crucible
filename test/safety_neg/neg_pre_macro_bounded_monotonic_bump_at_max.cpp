// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fixy-A1-007 Phase 2 (Mutation.h):
// BoundedMonotonic<T, Max>::bump this->-member CRUCIBLE_PRE — bound-
// violation rejection on `inner_.get() < T(Max)` predicate.
//
// Distinct mismatch class: predicate compares a member-function call
// on a NESTED-WRAPPER member (`inner_.get()` where inner_ is a
// Monotonic wrapping the actual value) against a compile-time
// template-parameter bound.  Two-level member-fn-on-member chain
// through `this->inner_.get()`.
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — bump at compile-time Max trips the pre.

#include <crucible/safety/Mutation.h>
#include <cstdint>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::BoundedMonotonic<uint32_t, 8u> m{8u};
    m.bump();  // CRUCIBLE_PRE(inner_.get() < 8) MUST fire — at bound
    return 0;
}

static_assert(under_test() == 0,
    "CRUCIBLE_PRE on BoundedMonotonic::bump's bound check MUST fire "
    "at consteval when bump() is called at Max.  If this static_"
    "assert evaluates successfully, the body-CRUCIBLE_PRE migration "
    "failed to close the consteval bypass for nested-wrapper-member-"
    "function predicate chains.");

}  // namespace

int main() { return 0; }
