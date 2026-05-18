// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fixy-A1-007 Phase 2 (Mutation.h):
// WriteOnce<T>::set this->-member CRUCIBLE_PRE — double-set rejection
// on std::optional<T>::has_value() member-function-on-member predicate.
//
// Distinct mismatch class from SetOnce fixtures: the predicate
// references an optional<T> member via member-function call —
// `value_.has_value()` — a member-on-member shape vanilla GCC 16.1.1
// P2900 pre() silently bypasses at consteval for foldable-body
// functions.
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — CRUCIBLE_PRE planted trap fires when set() is
// called on an already-set slot at consteval.

#include <crucible/safety/Mutation.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::WriteOnce<int> w{};
    w.set(1);
    w.set(2);  // CRUCIBLE_PRE(!value_.has_value()) MUST fire here
    return 0;
}

static_assert(under_test() == 0,
    "CRUCIBLE_PRE on WriteOnce::set's !value_.has_value() check MUST "
    "fire at consteval when set() is called twice.  If this static_"
    "assert evaluates successfully, the body-CRUCIBLE_PRE migration of "
    "fixy-A1-007 Phase 2 failed to close the consteval bypass for the "
    "optional-has_value() member-on-member predicate shape.");

}  // namespace

int main() { return 0; }
