// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fixy-A1-007 Phase 2 (Mutation.h):
// Monotonic<T>::bump this->-member CRUCIBLE_PRE — wraparound rejection
// on `impl_.peek() != std::numeric_limits<T>::max()` predicate.
//
// Distinct mismatch class: predicate compares a member-function call
// on a member (`impl_.peek()`) against a static numeric_limits sentinel.
// The this->-member shape combined with a foldable-body bump() is
// exactly the consteval-bypass class vanilla P2900 silently fails on.
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — bump at numeric_limits::max() trips the pre.

#include <crucible/safety/Mutation.h>
#include <cstdint>
#include <limits>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::Monotonic<uint8_t> m{std::numeric_limits<uint8_t>::max()};
    m.bump();  // CRUCIBLE_PRE(impl_.peek() != UINT8_MAX) MUST fire
    return 0;
}

static_assert(under_test() == 0,
    "CRUCIBLE_PRE on Monotonic::bump's wraparound check MUST fire at "
    "consteval when bump() is called at numeric_limits::max().  If "
    "this static_assert evaluates successfully, the body-CRUCIBLE_PRE "
    "migration failed to close the consteval bypass for member-fn-vs-"
    "static-sentinel predicate shapes.");

}  // namespace

int main() { return 0; }
