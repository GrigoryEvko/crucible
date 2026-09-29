// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A sealed kernel table mints no mutable view, so mint_mutable_view
// returns an optional view.  A registration that skips the check and
// hands the optional itself to register_op does not compile, so no
// caller writes to a sealed table by forgetting the seal.

#include <crucible/CKernel.h>

int main() {
    crucible::CKernelTable table;
    table.register_op(table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>()),
                      crucible::SchemaHash{0x42}, crucible::CKernelId::GEMM_MM);
    return 0;
}
