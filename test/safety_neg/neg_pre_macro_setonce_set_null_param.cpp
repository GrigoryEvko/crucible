// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 3 for fixy-A1-007 (#1549):
// SetOnce<T>::set(T*) parameter-side CRUCIBLE_PRE — argument null
// rejection branch.
//
// Premise: SetOnce<T>::set has two body-CRUCIBLE_PRE clauses migrated
// from vanilla P2900 pre() in fixy-A1-007:
//   * CRUCIBLE_PRE(p != nullptr)    — parameter side (THIS fixture)
//   * CRUCIBLE_PRE(ptr_ == nullptr) — this-> member side (fixture #2)
//
// The parameter side is the EASY case — vanilla pre() handles it
// correctly on GCC 16.1.1, but we migrated it anyway for clause-shape
// uniformity with the this->-member side.  This fixture witnesses
// that CRUCIBLE_PRE still fires at consteval for the easy case.
// Companion fixtures #2 (double-set) and #3 (reset-unset) cover the
// harder consteval-bypass classes — distinct mismatch classes per
// HS14 mandate.
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — GCC's consteval evaluator hitting the trap
// planted by CRUCIBLE_PRE's `if consteval { __builtin_trap(); }`.

#include <crucible/handles/Once.h>

namespace {

// The function under test — constexpr-eligible after the fixy-A1-007
// `constexpr` qualifier was added to SetOnce::set.  Calls set(nullptr)
// which MUST trip the `p != nullptr` precondition at consteval.
[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::SetOnce<int> s{};
    s.set(nullptr);  // CRUCIBLE_PRE(p != nullptr) MUST fire here
    return 0;
}

// Silent-bypass detector: if CRUCIBLE_PRE were inert at consteval
// (the GCC 16 P2900 bug behavior for parameter-side predicates that
// reference foldable expressions), under_test() would return 0 and
// this static_assert would pass — silent false-green.
//
// CRUCIBLE_PRE plants `if consteval { __builtin_trap(); }` which is
// NOT a constant expression, so the consteval evaluator fails the
// surrounding static_assert.  Exactly what HS14 demands of a
// soundness-gate witness.
static_assert(under_test() == 0,
    "CRUCIBLE_PRE on parameter-side null check MUST fire at consteval "
    "when SetOnce::set is called with nullptr.  If this static_assert "
    "evaluates successfully, the Pre.h consteval-enforcement is broken "
    "for parameter predicates and the fixy-A1-007 migration is "
    "structurally unsound.");

}  // namespace

int main() { return 0; }
