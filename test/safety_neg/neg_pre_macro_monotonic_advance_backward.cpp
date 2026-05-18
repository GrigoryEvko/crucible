// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fixy-A1-007 Phase 2 (Mutation.h):
// Monotonic<T, Cmp>::advance this->-member CRUCIBLE_PRE — backward-
// advance rejection on `lattice_type::leq(impl_.peek(), new_value)`
// predicate.
//
// Distinct mismatch class: the predicate is a NESTED member-function
// call — `lattice_type::leq(impl_.peek(), new_value)` — where one arg
// is itself a member-function-on-member access through `this->`.
// Vanilla P2900 pre() bypasses at consteval for foldable Monotonic
// bodies; the body CRUCIBLE_PRE form fires.
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — backward advance trips lattice_type::leq's
// false return → CRUCIBLE_PRE fires at consteval.

#include <crucible/safety/Mutation.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::Monotonic<uint32_t> m{10u};
    m.advance(5u);  // CRUCIBLE_PRE(lattice_type::leq(10, 5)) MUST fire
    return 0;
}

static_assert(under_test() == 0,
    "CRUCIBLE_PRE on Monotonic::advance's lattice_type::leq member-fn "
    "predicate MUST fire at consteval when advance() steps backward.  "
    "If this static_assert evaluates successfully, the body-"
    "CRUCIBLE_PRE migration failed to close the consteval bypass for "
    "nested-member-function predicate shapes.");

}  // namespace

int main() { return 0; }
