// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing EpochVersioned<T_A> with EpochVersioned<T_B>
// via operator== when T_A != T_B.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/EpochVersioned.h>

using namespace crucible::safety;

int main() {
    EpochVersioned<int>    int_value{42, Epoch{1}, Generation{1}};
    EpochVersioned<double> dbl_value{3.14, Epoch{1}, Generation{1}};

    return static_cast<int>(int_value == dbl_value);
}
