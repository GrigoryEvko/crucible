// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The schemas come from the ops that a Vigil records, so the mutable view of
// the kernel table asks for the context of a Vigil's producer claim.  A
// context that names no claim is refused.

#include <crucible/CKernel.h>

int main() {
    crucible::CKernelTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground());
    (void)view;
    return 0;
}
