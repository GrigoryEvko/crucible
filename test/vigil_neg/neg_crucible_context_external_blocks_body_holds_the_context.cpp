// Negative-compile test.  This file must fail to compile.
//
// The body of with_external_blocks gets its context as an argument.  A body that
// holds the context by a reference could store a View of the scope in it, and the
// View would outlive the scope.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto const cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    auto given = ctx.with_external_blocks(crucible::ExternalBlocks{}, cv, [&ctx](crucible::CrucibleContext&) noexcept {
        static_cast<void>(ctx.is_compiled());
    });
    static_cast<void>(given);
    return 0;
}
