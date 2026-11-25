// Negative-compile test.  This file must fail to compile.
//
// with_foreign_views takes the list of the Views into its frame.  A list that stays
// with the caller could change in the body, and the end of the scope would then
// leave a View in the pool.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto const cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    ::fixy::HeapArray<crucible::ForeignView> views{};
    ctx.with_foreign_views<crucible::foreign_block_stays>(views, cv, [](crucible::CrucibleContext&) noexcept {});
    return 0;
}
