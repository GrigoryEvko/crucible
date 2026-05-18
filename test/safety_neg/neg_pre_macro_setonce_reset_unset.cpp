// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 3 for fixy-A1-007 (#1549):
// SetOnce<T>::reset() this->-member CRUCIBLE_PRE — lifecycle reset
// of an unset slot rejection branch.
//
// Premise: like fixture #2, this exercises the this->-member
// consteval-bypass class — but through a different code path
// (lifecycle-reset rather than initial-set) and through a different
// predicate shape (`has_value()` is a member FUNCTION call, not a
// direct member access).  GCC 16.1.1's vanilla pre() bypasses both
// shapes; CRUCIBLE_PRE must fire on both.
//
// The `has_value()` call resolves to `return ptr_ != nullptr;` —
// foldable, constexpr-eligible.  Calling reset() on a fresh
// (never-set) SetOnce violates the pre because has_value() is false.
// CRUCIBLE_PRE plants `if consteval { __builtin_trap(); }`; consteval
// evaluator hits the trap.
//
// Distinct mismatch class from fixture #1 (parameter null) and
// fixture #2 (double-set on the SETTER path).  This fixture covers
// the LIFECYCLE-RESET path, which goes through reset() not set().
//
// Expected diagnostic: "non-constant condition for static assertion"
// / "__builtin_trap" — GCC's consteval evaluator hitting the trap
// planted in reset()'s CRUCIBLE_PRE(has_value()).

#include <crucible/handles/Once.h>

namespace {

// The function under test — constexpr-eligible after the fixy-A1-007
// `constexpr` qualifier was added to SetOnce::reset and has_value.
// Calls reset() on a never-set SetOnce; has_value() returns false →
// CRUCIBLE_PRE(has_value()) MUST trip at consteval.
[[nodiscard]] constexpr int under_test() noexcept {
    crucible::safety::SetOnce<int> s{};
    s.reset();  // CRUCIBLE_PRE(has_value()) MUST fire here
    return 0;
}

// Silent-bypass detector: if CRUCIBLE_PRE were inert at consteval for
// member-function predicates (which is the harder consteval-bypass
// class because the function call goes through name resolution + ADL
// before reaching the foldable body), under_test() would return 0
// and this static_assert would pass — silent false-green for the
// lifecycle-reset variant of the bug.
static_assert(under_test() == 0,
    "CRUCIBLE_PRE on has_value() lifecycle-reset check MUST fire at "
    "consteval when SetOnce::reset() is called on an unset slot.  If "
    "this static_assert evaluates successfully, the body-CRUCIBLE_PRE "
    "migration of fixy-A1-007 failed to close the GCC 16.1.1 consteval "
    "bypass for member-function predicates on lifecycle-reset paths.");

}  // namespace

int main() { return 0; }
