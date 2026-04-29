// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: comparing MemOrder<TAG_A, T> with MemOrder<TAG_B, T>
// via operator==.
//
// The friend operator== is a non-template friend declared inside
// the class template.  Each (Tag, T) instantiation has its OWN
// friend taking two MemOrder<Tag, T>&.  Cross-tag comparison fails
// to find a viable operator==.
//
// [GCC-WRAPPER-TEXT] — operator== overload-resolution rejection.

#include <crucible/safety/MemOrder.h>

using namespace crucible::safety;

int main() {
    MemOrder<MemOrderTag_v::Relaxed, int> relax_value{42};
    MemOrder<MemOrderTag_v::SeqCst,  int> seqcst_value{42};

    // Should FAIL: operator== for MemOrder<Relaxed, int> takes two
    // MemOrder<Relaxed, int>&; seqcst_value is MemOrder<SeqCst, int>.
    return static_cast<int>(relax_value == seqcst_value);
}
