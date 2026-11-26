// Negative-compile test.  This file must fail to compile.
//
// No check of the program proves that a block of a foreign owner lives for the
// time of the scope.  So with_foreign_views asks for the licence of the caller.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto given = ctx.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        scope.with_foreign_views(::fixy::HeapArray<crucible::ForeignView>{}, [](crucible::CompiledScope&) noexcept {});
    });
    static_cast<void>(given);
    return 0;
}
