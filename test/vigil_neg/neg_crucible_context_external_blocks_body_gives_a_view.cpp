// Negative-compile test.  This file must fail to compile.
//
// The result of the body of with_external_blocks leaves the scope, and a block can
// end after it.  So the result holds no View of a block.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto given = ctx.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        auto const read_input = [](crucible::CompiledScope& scoped) noexcept {
            return scoped.input_view<float>(crucible::InputPosition{0});
        };
        auto kept = scope.with_external_blocks(crucible::ExternalBlocks{}, read_input);
        static_cast<void>(kept);
    });
    static_cast<void>(given);
    return 0;
}
