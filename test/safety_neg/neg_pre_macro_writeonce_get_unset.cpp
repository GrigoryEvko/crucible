// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fixy-A1-007 Phase 2 (Mutation.h):
// WriteOnce<T>::get this->-member CRUCIBLE_PRE — read-before-set
// rejection on `value_.has_value()` member-fn-on-member predicate.
//
// Distinct mismatch class: read-side complement to neg_pre_macro_
// writeonce_set_twice.  The same optional-has_value() predicate shape
// but exercised through the const accessor path rather than the
// mutating setter — verifies CRUCIBLE_PRE fires symmetrically on
// const member-function bodies, not just non-const setters.
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — CRUCIBLE_PRE in get() fires at consteval.

#include <crucible/safety/Mutation.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::WriteOnce<int> w{};
    // CRUCIBLE_PRE(value_.has_value()) MUST fire — slot is empty.
    return w.get();
}

static_assert(under_test() == 0,
    "CRUCIBLE_PRE on WriteOnce::get's value_.has_value() check MUST "
    "fire at consteval when get() is called on an unset slot.  If "
    "this static_assert evaluates successfully, the body-CRUCIBLE_PRE "
    "migration failed for const-member-function predicates.");

}  // namespace

int main() { return 0; }
