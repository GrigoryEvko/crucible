// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-Checked fixture #1: checked_shl<T> via the fixy::
// alias preserves the substrate's std::integral constraint and
// rejects a non-integral type.
//
// Violation: checked_shl is declared `template <std::integral T>`;
// passing `double` (non-integral) fails the concept gate.  Routing
// through `fixy::struct_::checked_shl` must reject identically.
//
// Expected diagnostic: "constraints not satisfied" or
// "no matching function for call to 'checked_shl<double>'".

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_checked_signed_shift {

// Per-fixture carrier struct (sentinel).
struct TypeStructCheckedSignedShift {};

}  // namespace neg_fixy_struct_checked_signed_shift

int main() {
    // Should FAIL: double does not satisfy std::integral.
    [[maybe_unused]] auto bad =
        fstr::checked_shl<double>(2.5, 1);
    return 0;
}
