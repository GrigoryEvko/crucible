// Negative-compile test.  This file must fail to compile.
//
// with_external_blocks lends the context to the body.  A temporary context
// releases its pool at the end of the statement.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto const cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    crucible::CrucibleContext{test.alloc}.with_external_blocks(crucible::ExternalBlocks{}, cv,
                                                               [](crucible::CrucibleContext&) noexcept {});
    return 0;
}
