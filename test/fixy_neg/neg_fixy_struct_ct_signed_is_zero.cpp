// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-ConstantTime fixture #2: ct::is_zero via the fixy::
// alias rejects a signed-integer argument.
//
// Violation: ct::is_zero is declared
// `template <std::unsigned_integral T>`; passing `int` (signed)
// fails the concept gate.  Routing through
// `fixy::struct_::ct::is_zero` must reject identically.
//
// Expected diagnostic: "constraints not satisfied" or "no matching
// function for call to 'is_zero<int>'".

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_ct_signed_is_zero {

struct TypeStructCtSignedIsZero {};

}  // namespace neg_fixy_struct_ct_signed_is_zero

int main() {
    // Should FAIL: int is signed, ct::is_zero requires unsigned.
    [[maybe_unused]] int bad = fstr::ct::is_zero<int>(0);
    return 0;
}
