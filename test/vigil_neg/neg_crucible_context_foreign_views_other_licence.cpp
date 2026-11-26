// Negative-compile test.  This file must fail to compile.
//
// Only foreign_block_stays states that the block of a foreign owner lives for the
// time of the scope.  A type of the caller states nothing.

#include <crucible/CrucibleContext.h>

namespace {

struct OwnStatement final {};

}  // namespace

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto given = ctx.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        auto kept = scope.with_foreign_views<OwnStatement>(::fixy::HeapArray<crucible::ForeignView>{},
                                                           [](crucible::CompiledScope&) noexcept {});
        static_cast<void>(kept);
    });
    static_cast<void>(given);
    return 0;
}
