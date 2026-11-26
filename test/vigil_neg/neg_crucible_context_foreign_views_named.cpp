// Negative-compile test.  This file must fail to compile.
//
// with_foreign_views takes the list of the Views into its frame.  A list that stays
// with the caller could change in the body, and the end of the scope would then
// leave a View in the pool.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto given = ctx.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        ::fixy::HeapArray<crucible::ForeignView> views{};
        scope.with_foreign_views<crucible::foreign_block_stays>(views, [](crucible::CompiledScope&) noexcept {});
    });
    static_cast<void>(given);
    return 0;
}
