// Negative-compile test. This file must fail to compile.
//
// A sealed kernel table mints no mutable view, so mint_mutable_view
// returns an optional view.  A registration that skips the check and
// hands the optional itself to register_op does not compile, so no
// caller writes to a sealed table by forgetting the seal.

#include <crucible/CKernel.h>

// A write section of the table.  The fixture never links, so the function
// has no body.
crucible::WriteSection<crucible::CKernelTable>& held_section() noexcept;

int main() {
    crucible::CKernelTable table;
    table.register_op(held_section(),
                      table.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>()),
                      crucible::SchemaHash{0x42}, crucible::CKernelId::GEMM_MM);
    return 0;
}
