// Negative-compile test.  This file must fail to compile.
//
// No check of the program proves that a block of a foreign owner lives for the
// time of the scope.  So with_foreign_views asks for the licence of the caller.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto const cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    ctx.with_foreign_views(::fixy::HeapArray<crucible::ForeignView>{}, cv, [](crucible::CrucibleContext&) noexcept {});
    return 0;
}
