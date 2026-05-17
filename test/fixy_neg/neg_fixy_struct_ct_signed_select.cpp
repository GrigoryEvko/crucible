// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-ConstantTime fixture #1: ct::select via the fixy::
// alias rejects a signed-integer argument.
//
// Violation: ct::select is declared
// `template <std::unsigned_integral T>`; passing `int` (signed)
// fails the concept gate.  Routing through
// `fixy::struct_::ct::select` must reject identically.
//
// Expected diagnostic: "constraints not satisfied" or
// "no matching function for call to 'select<int>'".

#include <crucible/fixy/Struct.h>

namespace fstr = crucible::fixy::struct_;

namespace neg_fixy_struct_ct_signed_select {

struct TypeStructCtSignedSelect {};

}  // namespace neg_fixy_struct_ct_signed_select

int main() {
    // Should FAIL: int is signed, ct::select requires unsigned.
    [[maybe_unused]] int bad = fstr::ct::select<int>(1, 0x10, 0x20);
    return 0;
}
