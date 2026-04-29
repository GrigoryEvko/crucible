// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: swap()-ing EpochVersioned<T_A> with EpochVersioned<T_B>
// when T_A != T_B.
//
// [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.

#include <crucible/safety/EpochVersioned.h>

#include <utility>

using namespace crucible::safety;

int main() {
    EpochVersioned<int>    int_value{42, Epoch{1}, Generation{1}};
    EpochVersioned<double> dbl_value{3.14, Epoch{2}, Generation{2}};

    int_value.swap(dbl_value);

    using std::swap;
    swap(int_value, dbl_value);

    return 0;
}
