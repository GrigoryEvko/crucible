// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing Budgeted<T_A> with Budgeted<T_B> via
// operator== when T_A != T_B.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each Budgeted<T> instantiation has its OWN
// friend taking two Budgeted<T> const&.  Cross-T comparison fails
// to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/Budgeted.h>

using namespace crucible::safety;

int main() {
    Budgeted<int>    int_value{42, BitsBudget{1024}, PeakBytes{4096}};
    Budgeted<double> dbl_value{3.14, BitsBudget{1024}, PeakBytes{4096}};

    // Should FAIL: operator== for Budgeted<int> takes two
    // Budgeted<int> const&; dbl_value is Budgeted<double>.
    return static_cast<int>(int_value == dbl_value);
}
