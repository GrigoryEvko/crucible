// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-D30 fixture (Budgeted, FIRST product wrapper) — pins
// constrained-extractor.  budgeted_value_t is constrained on
// `requires is_budgeted_v<T>`.
//
// Single fixture (vs two for NTTP wrappers) — Budgeted has no
// compile-time tag to extract; only value_type extraction exists.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure.

#include <crucible/safety/IsBudgeted.h>

int main() {
    using V = crucible::safety::extract::budgeted_value_t<int>;
    V const v{};
    (void)v;
    return 0;
}
