// Negative-compile test.  This file must fail to compile.
//
// The result of the body of with_external_blocks leaves the scope, and a block can
// end after it.  So the result holds no View of a block.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto const cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    auto const read_input = [&cv](crucible::CrucibleContext& scoped) noexcept {
        return scoped.input_view<float>(crucible::InputPosition{0}, cv);
    };
    auto given = ctx.with_external_blocks(crucible::ExternalBlocks{}, cv, read_input);
    static_cast<void>(given);
    return 0;
}
