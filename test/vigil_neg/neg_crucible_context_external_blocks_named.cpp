// Negative-compile test.  This file must fail to compile.
//
// with_external_blocks takes the blocks into its frame.  Blocks that stay with the
// caller could end in the body, and a read of a slot would read ended memory.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto const cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    crucible::ExternalBlocks blocks{};
    ctx.with_external_blocks(blocks, cv, [](crucible::CrucibleContext&) noexcept {});
    return 0;
}
