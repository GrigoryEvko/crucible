// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The mutable view of the kernel table asks for the foreground context of a
// Vigil's producer claim.  The context of another state's claim proves that
// its holder owns that state, not a Vigil.

#include <crucible/CKernel.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::CKernelTable table;
    const auto view = table.mint_mutable_view(::foundation::effects::testing::foreground<Stranger>());
    (void)view;
    return 0;
}
