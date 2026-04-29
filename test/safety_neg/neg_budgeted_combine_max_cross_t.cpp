// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling combine_max() (or accumulate()) with a
// Budgeted<T_other> argument when T_other != T.
//
// THE LOAD-BEARING REJECTION FOR THE COMPOSITION-SURFACE IDENTITY.
// Both combine_max() and accumulate() take `Budgeted<T> const&`
// in their argument position.  Passing a Budgeted<U> with U != T
// cannot bind — even if T and U have compatible value semantics
// (e.g., int vs unsigned), the lattice carriers are still tied
// to the value type at the wrapper level (Graded's value_type
// is propagated into the wrapper's identity).
//
// Production bug-class this prevents: a caller that keeps two
// budgets — one over GradientShard (Budgeted<GradientShard>) and
// one over OptimizerState (Budgeted<OptimizerState>) — and tries
// to fold them via combine_max would silently slip through if the
// composition surface admitted cross-T arguments.  The compile
// error pushes the maintainer to handle the value-type heterogeneity
// explicitly (e.g., extract the bytes / extract the budget).
//
// [GCC-WRAPPER-TEXT] — combine_max parameter-type rejection.

#include <crucible/safety/Budgeted.h>

using namespace crucible::safety;

int main() {
    Budgeted<int>    int_value{42, BitsBudget{100}, PeakBytes{1024}};
    Budgeted<double> dbl_value{3.14, BitsBudget{200}, PeakBytes{2048}};

    // Should FAIL: Budgeted<int>::combine_max takes
    // Budgeted<int> const&; dbl_value is Budgeted<double>.
    auto bad = int_value.combine_max(dbl_value);
    return bad.peek();
}
