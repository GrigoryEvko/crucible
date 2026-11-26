// Negative-compile test.  This file must fail to compile.
//
// with_external_blocks takes the blocks into its frame.  Blocks that stay with the
// caller could end in the body, and a read of a slot would read ended memory.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    crucible::ExternalBlocks blocks{};
    auto given = ctx.with_compiled(context_key, [&blocks](crucible::CompiledScope& scope) noexcept {
        scope.with_external_blocks(blocks, [](crucible::CompiledScope&) noexcept {});
    });
    static_cast<void>(given);
    return 0;
}
