// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing Budgeted<T_A> with Budgeted<T_B>
// when T_A != T_B.
//
// swap() is a member taking `Budgeted<T>&` and a friend free-swap
// taking the same.  Cross-T swap is rejected at overload resolution.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/Budgeted.h>

#include <utility>

using namespace crucible::safety;

int main() {
    Budgeted<int>    int_value{42, BitsBudget{1024}, PeakBytes{4096}};
    Budgeted<double> dbl_value{3.14, BitsBudget{2048}, PeakBytes{8192}};

    // Should FAIL: Budgeted<int>::swap takes Budgeted<int>&;
    // dbl_value is Budgeted<double>.
    int_value.swap(dbl_value);

    using std::swap;
    swap(int_value, dbl_value);

    return 0;
}
