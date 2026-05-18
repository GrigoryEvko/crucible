// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 3 for fixy-A1-007 (#1549):
// SetOnce<T>::set(T*) this->-member CRUCIBLE_PRE — double-set
// rejection branch.
//
// Premise: this is the LOAD-BEARING fixture for fixy-A1-007.  Vanilla
// P2900 `pre(ptr_ == nullptr)` references a class member through
// implicit `this->`, the canonical consteval-bypass shape on GCC
// 16.1.1 — for foldable-body functions, the compiler silently
// bypasses the precondition at consteval, leaving every neg-compile
// fixture that depends on consteval-firing silently green when it
// should be red.  (See memory feedback_gcc16_c26_contract_gotchas
// and feedback_crucible_pre_post_macros for the full diagnosis.)
//
// CRUCIBLE_PRE plants `if consteval { __builtin_trap(); }` in the
// function body, bypassing the parser-special pre-clause machinery
// AND the constexpr-cache bypass that's the root of PR c++/124241.
// This fixture witnesses that the body-CRUCIBLE_PRE form fires
// identically across both compilers (vanilla GCC 16.1.1 and the
// patched g++-16p) for the this->-member predicate shape.
//
// Distinct mismatch class from fixture #1 (parameter-side null) and
// fixture #3 (lifecycle reset-unset).
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — GCC's consteval evaluator hitting the trap on
// the SECOND set() call's CRUCIBLE_PRE(ptr_ == nullptr) check.

#include <crucible/handles/Once.h>

namespace {

constexpr int x_a = 1;
constexpr int x_b = 2;

// The function under test — constexpr-eligible after the fixy-A1-007
// `constexpr` qualifier was added to SetOnce::set.  First set()
// succeeds (ptr_ transitions nullptr → &x_a); the SECOND set() MUST
// trip CRUCIBLE_PRE(ptr_ == nullptr) at consteval.
[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::SetOnce<int const> s{};
    s.set(&x_a);
    s.set(&x_b);  // CRUCIBLE_PRE(ptr_ == nullptr) MUST fire here
    return 0;
}

// Silent-bypass detector: if CRUCIBLE_PRE were inert at consteval for
// the this->-member predicate shape (the GCC 16 P2900 bug behavior),
// under_test() would return 0 and this static_assert would pass —
// silent false-green for the EXACT bug class fixy-A1-007 set out
// to close.
static_assert(under_test() == 0,
    "CRUCIBLE_PRE on this->-member double-set check MUST fire at "
    "consteval when SetOnce::set is called twice.  If this static_"
    "assert evaluates successfully, the body-CRUCIBLE_PRE migration "
    "of fixy-A1-007 failed to close the GCC 16.1.1 consteval bypass "
    "for class-member predicates — the entire bug class is back.");

}  // namespace

int main() { return 0; }
